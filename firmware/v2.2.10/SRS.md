# Software Requirements Specification (SRS)
## Sistem Presensi Pintar Berbasis IoT — Hybrid Edition
**Project:** Madrasah Universe
**Versi:** 2.2.10
**Device:** ESP32-C3 Super Mini
**Author:** Yahya Zulfikri
**IDE:** Arduino IDE v2.3.6
**Dibuat:** Juli 2025
**Diperbarui:** Maret 2026
**Status:** Final

---

## 1. PENDAHULUAN

### 1.1 Tujuan
Dokumen ini mendeskripsikan seluruh persyaratan fungsional, non-fungsional, dan alur logika untuk firmware Sistem Presensi Pintar berbasis RFID yang berjalan di atas mikrokontroler ESP32-C3 Super Mini. Sistem ini dirancang untuk mencatat kehadiran pegawai dan siswa melalui tap kartu RFID dengan filosofi _Self-Healing_ dan _Store-and-Forward_, menjamin integritas data kehadiran tanpa kehilangan (_zero data loss_).

### 1.2 Ruang Lingkup
Firmware ini menangani:
- Pembacaan kartu RFID dan pencatatan presensi
- Validasi RFID lokal berbasis RAM cache yang dimuat dari `rfid_db.txt` saat boot — lookup O(n) di RAM tanpa akses SD card dan tanpa HTTP call, latency < 1ms
- Penyimpanan data offline ke SD card (Queue System) dan NVS buffer internal
- Sinkronisasi data latar belakang ke server via HTTPS (non-blocking)
- Update firmware otomatis via OTA
- Manajemen daya dengan deep sleep dan OLED scheduling
- Feedback visual (OLED) dan audio (buzzer) ke pengguna
- Reconnect WiFi otomatis via state machine
- Pemulihan waktu dua mekanisme: kompensasi durasi deep sleep dan restore dari NVS saat reset paksa

### 1.3 Riwayat Versi

| Versi | Tanggal | Perubahan |
|---|---|---|
| v2.2.7 | Maret 2026 | Rilis awal sistem hybrid: Queue System + NVS Buffer, reconnect state machine, deep sleep, OLED auto dim, bulk sync chunked |
| v2.2.8 | Maret 2026 | Tambah OTA update otomatis, perbaikan WDT coverage, hapus `validateRfidOnline()` dari alur tap SD, tambah WDT safety net setelah deep sleep, prioritas kecepatan tap queue-first |
| v2.2.9 | Maret 2026 | Tambah RFID Local Database: download `rfid_db.txt` dari server, muat ke RAM cache saat boot, lookup O(n) di RAM saat tap tanpa akses SD/HTTP, pembaruan DB otomatis setiap 3 jam berbasis versi timestamp, free/reload cache mengikuti siklus hidup SD card |
| v2.2.10 | Maret 2026 | Tambah recovery waktu dua mekanisme: (1) kompensasi `lastValidTime` dengan `sleepDurationSeconds` setelah bangun dari deep sleep karena `millis()` reset ke 0, (2) restore `lastValidTime` dari NVS flash saat RTC RAM hilang akibat reset paksa atau power putus; `nvsSaveLastTime()` kini dipanggil setiap kali NTP sync berhasil |

### 1.4 Definisi dan Singkatan

| Istilah | Definisi |
|---|---|
| RFID | Radio Frequency Identification — teknologi identifikasi nirsentuh |
| NVS | Non-Volatile Storage — penyimpanan internal ESP32 yang persisten melewati restart dan deep sleep |
| OTA | Over-The-Air — mekanisme update firmware via jaringan |
| WDT | Watchdog Timer — mekanisme reset otomatis jika sistem hang |
| Queue | Antrian file CSV di SD card untuk data presensi offline |
| Sync | Proses pengiriman data offline ke server secara batch |
| Deep Sleep | Mode tidur ESP32 dengan konsumsi daya sangat rendah |
| RTC Memory | Memori ESP32 yang tetap tersimpan saat deep sleep, namun hilang saat reset paksa atau power putus |
| Queue-First | Strategi simpan ke SD langsung tanpa validasi jaringan saat tap |
| Store-and-Forward | Data disimpan lokal, dikirim ke server saat koneksi tersedia |
| RFID Local DB | File `rfid_db.txt` di SD card berisi daftar RFID valid yang diunduh dari server |
| RFID RAM Cache | Array pointer `char**` di heap ESP32 berisi seluruh RFID valid, dimuat dari `rfid_db.txt` saat boot untuk lookup cepat tanpa akses SD |
| `sleepDurationSeconds` | Variabel RTC RAM yang menyimpan durasi sleep yang direncanakan, digunakan untuk mengkompensasi `lastValidTime` saat bangun karena `millis()` reset ke 0 setelah deep sleep |

### 1.5 Konteks Sistem
- **Sumber daya:** Adaptor listrik PLN dengan baterai 1000–3000 mAh sebagai UPS
- **Konektivitas:** WiFi 2.4GHz single SSID, dengan kemampuan operasi offline penuh
- **Pengguna:** Pegawai dan siswa yang melakukan tap kartu RFID
- **Backend:** Server Laravel di `https://zedlabs.id` dengan autentikasi API Key (`X-API-KEY`)
- **Integrasi hilir:** Notifikasi WhatsApp, laporan digital, analisis kehadiran

---

## 2. ARSITEKTUR SISTEM

### 2.1 Hardware

| Komponen | Pin ESP32-C3 | Protokol | Fungsi |
|---|---|---|---|
| ESP32-C3 Super Mini | — | — | Mikrokontroler utama |
| RFID RC522 | SS:7, RST:3, SCK:4, MOSI:6, MISO:5 | SPI | Pembaca kartu RFID 13.56MHz |
| MicroSD Card | CS:1, SCK:4, MOSI:6, MISO:5 | SPI (shared) | Penyimpanan offline queue, RFID DB, dan log (opsional) |
| OLED SSD1306 0.96" | SDA:8, SCL:9 | I2C | Display feedback & status |
| Buzzer Aktif 5V | PIN:10 | PWM | Feedback audio |

### 2.2 Lapisan Penyimpanan Data

```
Tap Kartu
    │
    ├─ [1] SD Card (Prioritas Utama)
    │       Validasi via RAM cache → Queue CSV
    │       Max 50.000 record, queue-first, lookup < 1ms
    │
    ├─ [2] NVS Buffer (Fallback SD tidak ada)
    │       Flash internal ESP32, max 20 record, persisten
    │
    └─ [3] Kirim Langsung (SD tidak ada + WiFi tersedia)
            POST /api/presensi, fallback ke NVS jika gagal
```

### 2.3 Endpoint Server

| Endpoint | Method | Fungsi |
|---|---|---|
| `/api/presensi/ping` | GET | Health check koneksi API |
| `/api/presensi` | POST | Kirim presensi langsung (tanpa SD) |
| `/api/presensi/sync-bulk` | POST | Sinkronisasi bulk data offline (SD + NVS) |
| `/api/presensi/firmware/check` | POST | Cek ketersediaan update OTA |
| `/api/presensi/firmware/download/{file}` | GET | Download binary firmware OTA |
| `/api/presensi/rfid-list/version` | GET | Cek versi timestamp RFID DB di server |
| `/api/presensi/rfid-list` | GET | Download daftar RFID valid (plain text streaming) |
| `/api/presensi/validate` | POST | Fallback validasi RFID online jika tidak ada di DB lokal |

### 2.4 Struktur File SD Card

```
/
├── queue_0.csv         ← File antrean aktif (max 25 record/file)
├── queue_1.csv
├── ...
├── queue_1999.csv      ← Max 2000 file
├── queue_meta.txt      ← Cache: "pending_count,current_file_index"
├── rfid_db.txt         ← Database RFID valid (diunduh dari server)
└── failed_log.csv      ← Log record ditolak server saat sync
```

---

## 3. ALUR LOGIKA SISTEM

### 3.1 Fase Boot / Setup
```
[Power ON / Reboot]
  ├─ Init WDT (60 detik, trigger_panic = true)
  ├─ Init I2C (SDA:8, SCL:9) + Buzzer (PIN:10)
  ├─ Init OLED SSD1306 0x3C → animasi startup + startup melody
  ├─ Baca MAC Address → generate deviceId = "ESP32_XXYY" (kapital)
  │
  ├─ [Recovery Waktu — 2 Mekanisme]
  │    ├─ [1] Bangun dari deep sleep (RTC RAM valid, sleepDurationSeconds > 0)
  │    │       → lastValidTime += sleepDurationSeconds
  │    │       → nvsSaveLastTime(lastValidTime) — persist ke NVS
  │    │       → bootTime = millis()
  │    │       → sleepDurationSeconds = 0 (reset flag)
  │    └─ [2] Reset paksa / power putus (RTC RAM hilang: !timeWasSynced || lastValidTime == 0)
  │            → nvsLoadLastTime() → jika > 0:
  │                 lastValidTime = saved
  │                 timeWasSynced = true
  │                 bootTime = millis(), bootTimeSet = true
  │
  ├─ Init SPI Bus (SCK:4, MISO:5, MOSI:6)
  │
  ├─ [Init SD Card] — showProgress "INIT SD CARD"
  │    ├─ BERHASIL
  │    │    ├─ loadMetadata() → restore cachedPendingRecords + currentQueueFile
  │    │    ├─ Jika metadata tidak ada → scan queue files → cari yang belum penuh
  │    │    │    └─ Tidak ada → buat queue_0.csv baru
  │    │    ├─ Jika cachedPendingRecords > 0 → tampilkan "DATA OFFLINE: X TERSISA"
  │    │    └─ loadRfidCacheFromFile() → muat RAM cache dari rfid_db.txt
  │    └─ GAGAL
  │         ├─ Tampilkan "SD CARD TIDAK ADA"
  │         └─ Jika nvsGetCount() > 0 → tampilkan "NVS BUFFER: X TERSISA"
  │
  ├─ [Koneksi WiFi] — showProgress "CONNECTING WIFI" (max 20×300ms)
  │    ├─ BERHASIL → [Ping API] max 3 retry
  │    │    ├─ API OK
  │    │    │    ├─ syncTimeWithFallback() → NTP sync
  │    │    │    │    └─ Berhasil → nvsSaveLastTime() — persist untuk ketahanan reset paksa
  │    │    │    ├─ Jika NVS count > 0 → nvsSyncToServer()
  │    │    │    ├─ Jika SD ada + pending > 0 → chunkedSync()
  │    │    │    └─ Jika SD ada → [Sync RFID DB]
  │    │    │         ├─ checkRfidDbVersion() → bandingkan localVer vs serverVer
  │    │    │         ├─ serverVer > localVer → downloadRfidDb()
  │    │    │         │    ├─ GET /api/presensi/rfid-list (streaming)
  │    │    │         │    ├─ Tulis ke /rfid_db.tmp per chunk
  │    │    │         │    ├─ Rename tmp → rfid_db.txt
  │    │    │         │    └─ Simpan versi baru ke NVS key "rfid_db_ver"
  │    │    │         └─ serverVer sama → "RFID DB UP TO DATE"
  │    │    └─ GAGAL 3x → "API GAGAL / OFFLINE MODE"
  │    └─ GAGAL → offlineBootFallback() → "NO WIFI / OFFLINE MODE"
  │
  ├─ [Init RFID RC522] — showProgress "INIT RFID"
  │    └─ VersionReg = 0x00 atau 0xFF → "RC522 GAGAL" → delay 3s → ESP.restart()
  │
  ├─ Tampilkan "SISTEM SIAP" + ONLINE/OFFLINE + playToneSuccess()
  ├─ Set semua timer (lastOtaCheck = 0, lastRfidDbCheck = millis())
  └─ checkOLEDSchedule() → masuk LOOP
```

### 3.2 Fase Main Loop
```
[LOOP - berjalan terus menerus]
  │
  ├─ [1] esp_task_wdt_reset()
  │
  ├─ [2] Cek RFID Feedback Timer
  │       └─ Jika aktif dan ≥ 1800ms
  │            ├─ rfidFeedback.active = false
  │            ├─ Jika wasOledOff → checkOLEDSchedule()
  │            └─ Reset previousDisplay (paksa redraw standby)
  │
  ├─ [3] checkOLEDSchedule() — setiap 60 detik
  │       ├─ Jam 08:00–14:00 → turnOffOLED()
  │       └─ Di luar jam itu  → turnOnOLED()
  │
  ├─ [4] checkSDHealth() — setiap 30 detik
  │       ├─ SD tidak ada → reinitSDCard()
  │       │    └─ Berhasil → "SD CARD TERBACA KEMBALI" + playToneSuccess()
  │       │         └─ loadRfidCacheFromFile() — reload RAM cache
  │       └─ SD ada → cek via sd.vol()->fatType()
  │            └─ Tidak sehat → sdCardAvailable=false → freeRfidCache()
  │                 → "SD CARD TERLEPAS!"
  │
  ├─ [5] ⭐ CEK KARTU RFID (PRIORITAS TERTINGGI)
  │       └─ PICC_IsNewCardPresent() && PICC_ReadCardSerial()
  │            └─ handleRFIDScan() → return (skip sisa loop)
  │
  ├─ [6] processReconnect() — state machine 5 state
  │       ├─ IDLE    → WiFi putus + ≥5 menit → INIT
  │       ├─ INIT    → WiFi.disconnect() + WiFi.begin() → TRYING
  │       ├─ TRYING  → tunggu max 15 detik
  │       │    ├─ Connected → SUCCESS
  │       │    └─ Timeout   → FAILED → IDLE
  │       └─ SUCCESS
  │            ├─ isOnline = true
  │            ├─ syncTimeWithFallback()
  │            ├─ Jika NVS count > 0 → nvsSyncToServer()
  │            ├─ Jika SD ada + pending > 0 → chunkedSync()
  │            └─ → IDLE
  │
  ├─ [7] Update Display — setiap 1 detik, hanya jika state berubah
  │       ├─ Baris kiri atas: CONNECTING... / SYNCING... / ONLINE / OFFLINE
  │       ├─ Tengah: "TAP KARTU"
  │       ├─ Tengah bawah: jam HH:MM
  │       ├─ Bawah: "Q:X" jika pending > 0
  │       └─ Pojok kanan atas: bar sinyal WiFi (4 level)
  │
  ├─ [8] Periodic Check — setiap 1 detik
  │       ├─ [Jika WiFi connected]
  │       │    ├─ checkOtaUpdate() — guard internal 3 jam
  │       │    │    ├─ POST /firmware/check dengan versi + device_id
  │       │    │    └─ Ada update → simpan ke otaState → "UPDATE vX.X.X TERSEDIA"
  │       │    ├─ checkAndUpdateRfidDb() — guard internal 3 jam
  │       │    │    ├─ Syarat: sdCardAvailable + WiFi connected
  │       │    │    ├─ checkRfidDbVersion() → bandingkan localVer vs serverVer
  │       │    │    └─ serverVer > localVer → downloadRfidDb()
  │       │    ├─ Jika otaState.updateAvailable && !rfidFeedback.active
  │       │    │    └─ performOtaUpdate()
  │       │    ├─ Jika NVS count > 0 && ≥5 menit → nvsSyncToServer()
  │       │    └─ Jika SD ada
  │       │         ├─ syncState.inProgress → chunkedSync() lanjut
  │       │         └─ ≥5 menit && pending > 0 → chunkedSync() mulai baru
  │       └─ periodicTimeSync() — setiap 1 jam
  │
  └─ [9] Sleep Mode Check
          └─ Jam 18:00–05:00 && waktu valid
               ├─ Jika sync sedang berjalan → chunkedSync() → return
               ├─ Tampilkan "SLEEP MODE..."
               ├─ Hitung durasi sleep (min 60 detik, max 12 jam)
               ├─ Tampilkan "SLEEP FOR X Jam Y Menit"
               ├─ Simpan sleepDurationSeconds ke RTC RAM
               ├─ Matikan OLED
               ├─ esp_task_wdt_deinit()
               ├─ esp_sleep_enable_timer_wakeup()
               ├─ esp_deep_sleep_start()
               └─ [Safety net jika sleep gagal]
                    ├─ esp_task_wdt_init()
                    └─ esp_task_wdt_add()
```

### 3.3 Fase RFID Scan Handler
```
[handleRFIDScan()]
  ├─ deselectSD() → aktifkan RFID (PIN_RFID_SS LOW)
  ├─ uidToString() → UID ke 10 digit desimal
  ├─ Debounce: UID sama dalam 150ms → abaikan, return
  ├─ Simpan lastUID + update lastScan
  ├─ Jika OLED mati → turnOnOLED(), wasOledOff = true
  ├─ Tampilkan "RFID | [UID]" + playToneNotify()
  ├─ kirimPresensi(rfid) → dapat message + bool success
  ├─ Tampilkan "BERHASIL/INFO | [message]"
  ├─ playToneSuccess() atau playToneError()
  ├─ Set rfidFeedback { active=true, shownAt=now, wasOledOff }
  └─ PICC_HaltA() + PCD_StopCrypto1() + deaktifkan RFID

[kirimPresensi()]
  ├─ isTimeValid()? → Tidak → "WAKTU INVALID", return false
  ├─ getFormattedTimestamp() + time(nullptr) → currentUnixTime
  │
  ├─ PRIORITAS 1: sdCardAvailable = true
  │    ├─ isRfidInCache(rfid) — lookup di RAM, O(n), < 1ms
  │    │    ├─ Cache kosong (rfid_db.txt tidak ada) → izinkan (fallback, lanjut ke saveToQueue)
  │    │    ├─ Ditemukan → lanjut ke saveToQueue
  │    │    └─ Tidak ditemukan → "HUBUNGI ADMIN", return false
  │    └─ saveToQueue(rfid, timestamp, unixTime)
  │         ├─ Acquire SD mutex
  │         ├─ isDuplicateInternal() — 3 file terakhir, window 30 menit
  │         │    └─ Duplikat → release → return false
  │         ├─ Cek currentFile penuh (≥25 record)
  │         │    └─ Penuh → rotasi ke (currentQueueFile+1) % 2000
  │         │         └─ File berikutnya berisi data → return false
  │         ├─ Append ke CSV: rfid,timestamp,device_id,unix_time
  │         ├─ file.sync() + file.close()
  │         ├─ Release SD mutex
  │         ├─ cachedPendingRecords++ + saveMetadata()
  │         └─ return true
  │    ├─ BERHASIL → "DATA TERSIMPAN" / "QUEUE HAMPIR PENUH!" (≥1600 file)
  │    └─ GAGAL → isDuplicateInternal() lagi
  │         ├─ Duplikat → "CUKUP SEKALI!"
  │         └─ Bukan duplikat → "SD CARD ERROR"
  │
  ├─ PRIORITAS 2: SD tidak ada + WiFi connected
  │    ├─ kirimLangsung() → POST /api/presensi
  │    │    ├─ HTTP 200 → "PRESENSI OK", return true
  │    │    ├─ HTTP 400 → "CUKUP SEKALI!", return false
  │    │    ├─ HTTP 404 → "RFID UNKNOWN", return false
  │    │    └─ HTTP lain → "SERVER ERR X", return false
  │    └─ Jika gagal (timeout/error) → fallback NVS
  │         ├─ nvsIsDuplicate() → "CUKUP SEKALI!"
  │         ├─ nvsSaveToBuffer() → "BUFFER X/20"
  │         └─ NVS penuh → "BUFFER PENUH!"
  │
  └─ PRIORITAS 3: SD tidak ada + WiFi tidak ada
       ├─ nvsIsDuplicate() → "CUKUP SEKALI!"
       ├─ nvsSaveToBuffer() → "BUFFER X/20"
       └─ NVS penuh → "BUFFER PENUH!"
```

### 3.4 Fase RFID Local Database
```
[downloadRfidDb()]
  ├─ Syarat: WiFi connected + sdCardAvailable
  ├─ Tampilkan "RFID DB MENGUNDUH..."
  ├─ GET /api/presensi/rfid-list via HTTPS
  ├─ HTTP != 200 → "RFID DB GAGAL UNDUH" + playToneError(), return false
  ├─ Buka /rfid_db.tmp untuk tulis (truncate jika ada)
  ├─ Streaming read per chunk 256 byte
  │    ├─ Baris pertama "ver:XXXXXXXXXX" → parse serverVer
  │    └─ Baris berikutnya: validasi 10 digit angka → tulis ke file
  │         └─ esp_task_wdt_reset() per iterasi chunk
  ├─ file.sync() + file.close()
  ├─ sd.remove(RFID_DB_FILE) jika ada
  ├─ sd.rename("/rfid_db.tmp", "/rfid_db.txt")
  ├─ nvsSetRfidDbVer(serverVer)
  ├─ loadRfidCacheFromFile() → reload RAM cache
  └─ Tampilkan "RFID DB X RFID" + playToneSuccess()

[loadRfidCacheFromFile()]
  ├─ freeRfidCache() → free alokasi heap sebelumnya
  ├─ Buka rfid_db.txt → hitung jumlah baris valid (10 digit)
  ├─ malloc array pointer char** sejumlah baris valid
  ├─ Baca ulang file → malloc 11 byte per RFID → isi array
  │    └─ esp_task_wdt_reset() per baris
  ├─ rfidCacheLoaded = true
  └─ return true

[isRfidInCache(rfid)]
  ├─ Cache tidak ada/kosong → return true (fallback izinkan)
  ├─ Loop array RAM → strcmp per entry
  ├─ Match → return true
  └─ Tidak match → return false

[freeRfidCache()]
  ├─ Loop → free tiap pointer
  ├─ free array utama
  └─ rfidCacheCount = 0, rfidCacheLoaded = false

[checkAndUpdateRfidDb()]
  ├─ Guard: sdCardAvailable + WiFi connected + interval 3 jam
  ├─ nvsGetRfidDbVer() → localVer
  ├─ checkRfidDbVersion() → GET /rfid-list/version → serverVer
  ├─ serverVer == 0 || serverVer <= localVer → return (skip)
  └─ serverVer > localVer → downloadRfidDb()
```

### 3.5 Fase Manajemen Waktu
```
[Hierarki sumber waktu — getTimeWithFallback()]
  1. NTP aktif: getLocalTime() valid && tm_year ≥ 120
     └─ Sync ke: pool.ntp.org → time.google.com → id.pool.ntp.org
        Timeout per server: 2500ms, GMT offset: UTC+7
        Berhasil → simpan lastValidTime ke RTC memory
                 → nvsSaveLastTime() ke NVS flash (ketahanan reset paksa)
  2. Estimasi RTC memory:
     └─ est = lastValidTime + (millis() - bootTime) / 1000
        Valid maksimal 12 jam sejak sync terakhir (MAX_TIME_ESTIMATE_AGE)
  3. Tidak ada sumber valid → return false
     └─ kirimPresensi() → "WAKTU INVALID", tolak presensi

[Recovery waktu saat boot]
  ├─ Bangun dari deep sleep (sleepDurationSeconds > 0):
  │    lastValidTime += sleepDurationSeconds
  │    → nvsSaveLastTime(lastValidTime)
  │    → sleepDurationSeconds = 0
  └─ Reset paksa / power putus (!timeWasSynced || lastValidTime == 0):
       → nvsLoadLastTime() → jika > 0: pulihkan lastValidTime + set flag

[Sync periodik]
  └─ Setiap 1 jam jika WiFi connected → syncTimeWithFallback()
```

### 3.6 Fase Sync Data
```
[chunkedSync() — non-blocking, background]
  ├─ Syarat: sdCardAvailable + WiFi connected
  ├─ Satu siklus: max 5 file, max 15 detik
  ├─ Loop queue_0.csv → queue_1999.csv
  │    ├─ File ada
  │    │    ├─ countRecordsInFile() > 0 → syncQueueFile()
  │    │    │    ├─ readQueueFile() → filter record > 30 hari
  │    │    │    ├─ POST bulk ke /api/presensi/sync-bulk
  │    │    │    │    ├─ HTTP 200
  │    │    │    │    │    ├─ Per item error → appendFailedLog()
  │    │    │    │    │    └─ sd.remove(filename) + pendingCacheDirty=true
  │    │    │    │    └─ Gagal → skip, coba siklus berikutnya
  │    │    │    └─ WiFi putus → syncState.inProgress=false, return
  │    │    └─ File kosong → sd.remove() langsung
  │    └─ yield() + esp_task_wdt_reset() per iterasi
  └─ Semua file selesai
       ├─ syncState.inProgress = false
       ├─ refreshPendingCache() + saveMetadata()
       └─ Jika pending = 0 → "SYNC SELESAI!" + playToneSuccess()

[nvsSyncToServer() — NVS buffer, prioritas sebelum SD]
  ├─ Syarat: nvsGetCount() > 0 + WiFi connected
  ├─ Serialize semua record NVS ke JSON array
  ├─ POST bulk ke /api/presensi/sync-bulk
  ├─ HTTP 200
  │    ├─ Per item error → appendFailedLog()
  │    └─ Hapus semua record NVS + nvsSetCount(0)
  └─ Gagal → pertahankan, coba di interval berikutnya
```

### 3.7 Fase OTA Update
```
[checkOtaUpdate() — guard 3 jam, langsung saat boot]
  ├─ POST /api/presensi/firmware/check
  │    Body: { version: "2.2.10", device_id: "ESP32_XXYY" }
  ├─ HTTP 200 + response { update: true, version, url }
  │    ├─ Simpan ke otaState { updateAvailable, version, url }
  │    └─ Tampilkan "UPDATE vX.X.X TERSEDIA" + playToneNotify()
  └─ Tidak ada update / error → return

[performOtaUpdate() — otomatis, tanpa konfirmasi, by design]
  ├─ Guard: otaState.updateAvailable + WiFi + !rfidFeedback.active
  ├─ Tampilkan "UPDATE OTA vX.X.X" → "MENGUNDUH MOHON TUNGGU..."
  ├─ esp_task_wdt_delete() + esp_task_wdt_deinit()
  ├─ GET binary dari otaState.url via HTTPS (timeout 60 detik)
  ├─ HTTP 200
  │    ├─ Update.begin(totalSize)
  │    │    └─ Gagal → "UPDATE GAGAL NO SPACE" → goto reinit_wdt
  │    ├─ Stream write 1024 byte/chunk sampai selesai
  │    └─ Update.end() && isFinished()
  │         ├─ BERHASIL → "UPDATE OK RESTART..." → playToneSuccess() → ESP.restart()
  │         └─ GAGAL → "UPDATE GAGAL ERR X" → goto reinit_wdt
  ├─ HTTP error → "UPDATE GAGAL HTTP ERR X" → goto reinit_wdt
  └─ reinit_wdt:
       ├─ otaState.updateAvailable = false
       ├─ esp_task_wdt_init() + esp_task_wdt_add()
       └─ Reset previousDisplay (paksa redraw standby)
```

---

## 4. PERSYARATAN FUNGSIONAL

### 4.1 Boot & Inisialisasi

**FR-01** — WDT diinisialisasi pertama kali di `setup()` dengan timeout 60 detik dan `trigger_panic = true`.

**FR-02** — `deviceId` di-generate dari 2 byte terakhir MAC Address format `ESP32_XXYY` huruf kapital.

**FR-03** — SD card diinisialisasi dengan `SD_SCK_MHZ(10)`. Jika berhasil, metadata dimuat dari `queue_meta.txt`. Jika metadata tidak ada, sistem mencari file queue belum penuh atau membuat baru. Setelah SD berhasil init, `loadRfidCacheFromFile()` dipanggil segera untuk memuat RAM cache.

**FR-04** — WiFi dikoneksikan dengan maksimal 20 retry × 300ms. Jika berhasil, ping API dilakukan maksimal 3 retry. Jika API OK, urutan: sync NTP → `nvsSaveLastTime()` → sync NVS → sync SD queue → cek & update RFID DB.

**FR-05** — Setelah init RFID, VersionReg dibaca. Nilai `0x00` atau `0xFF` memicu `ESP.restart()`.

**FR-06** — `timers.lastOtaCheck = 0` saat setup, memastikan OTA check dijalankan segera saat pertama kali WiFi terhubung.

**FR-07** — `timers.lastRfidDbCheck = millis()` saat setup. RFID DB di-sync saat boot (bukan dari timer guard, tapi dari blok setup eksplisit).

### 4.2 Recovery Waktu

**FR-08** — Saat boot, sistem menjalankan dua mekanisme recovery waktu secara berurutan sebelum inisialisasi SPI.

**FR-09** — Mekanisme 1 (bangun dari deep sleep): Jika `timeWasSynced == true`, `lastValidTime > 0`, dan `sleepDurationSeconds > 0` di RTC RAM, maka `lastValidTime` ditambah `sleepDurationSeconds` untuk mengkompensasi reset `millis()` setelah deep sleep. Hasil diperbarui ke NVS via `nvsSaveLastTime()`, lalu `sleepDurationSeconds` di-reset ke 0.

**FR-10** — Mekanisme 2 (reset paksa / power putus): Jika `!timeWasSynced || lastValidTime == 0` (RTC RAM hilang), sistem memanggil `nvsLoadLastTime()`. Jika nilai yang tersimpan > 0, `lastValidTime` dipulihkan dari NVS dan `timeWasSynced` di-set `true`.

**FR-11** — `nvsSaveLastTime()` dipanggil setiap kali NTP sync berhasil di `syncTimeWithFallback()`, menjamin nilai NVS selalu up-to-date untuk keperluan recovery.

**FR-12** — Sebelum masuk deep sleep, nilai durasi sleep disimpan ke `sleepDurationSeconds` di RTC RAM agar tersedia saat bangun.

### 4.3 Pembacaan & Pencatatan Presensi

**FR-13** — RFID dipindai setiap iterasi loop dengan prioritas tertinggi. Jika ada kartu, `handleRFIDScan()` dipanggil dan loop di-return.

**FR-14** — UID dikonversi ke 10 digit desimal via konversi little-endian 4 byte.

**FR-15** — Debounce 150ms untuk UID yang sama diterapkan sebelum proses apapun.

**FR-16** — Presensi ditolak dengan `"WAKTU INVALID"` jika tidak ada sumber waktu valid.

**FR-17** — Jika SD tersedia, sistem melakukan validasi via `isRfidInCache()` — lookup di RAM heap yang sudah dimuat saat boot. Tidak ada akses SD card dan tidak ada HTTP call saat tap.

**FR-18** — Jika RFID tidak ditemukan di cache RAM dan cache tidak kosong, tap langsung ditolak dengan pesan `"HUBUNGI ADMIN"` tanpa fallback apapun.

**FR-19** — Jika cache RAM kosong (rfid_db.txt belum pernah diunduh atau gagal dimuat), `isRfidInCache()` mengembalikan `true` — semua tap diizinkan masuk ke queue (fallback).

**FR-20** — Cache RAM dimuat dari `rfid_db.txt` saat boot setelah SD init, di-reload setelah `downloadRfidDb()` selesai, di-reload setelah SD card kembali terbaca, dan di-free saat SD card terlepas.

**FR-21** — Duplicate check dilakukan pada 3 file queue terakhir dengan window 30 menit dan batas 100 baris per file.

**FR-22** — Rotasi file dilakukan saat file aktif mencapai 25 record. Rotasi dibatalkan jika file target masih berisi data belum ter-sync.

**FR-23** — Jika SD tidak ada dan WiFi tersedia, data dikirim langsung ke `/api/presensi`. Jika gagal, data di-fallback ke NVS buffer.

**FR-24** — Jika SD tidak ada dan WiFi tidak ada, data disimpan ke NVS buffer (max 20 record).

**FR-25** — Setiap tap kartu SELALU menghasilkan feedback OLED + buzzer tanpa terkecuali. Feedback ditampilkan minimal 1800ms.

### 4.4 RFID Local Database

**FR-26** — Database RFID lokal disimpan di `/rfid_db.txt` dengan format satu RFID (10 digit) per baris.

**FR-27** — Versi database disimpan di NVS key `rfid_db_ver` sebagai `unsigned long` (timestamp Unix dari server).

**FR-28** — Sebelum download, perangkat membandingkan versi lokal dengan server via GET `/api/presensi/rfid-list/version`. Download hanya dilakukan jika `serverVer > localVer`.

**FR-29** — Download dilakukan dengan streaming chunk 256 byte langsung ke file sementara `/rfid_db.tmp` tanpa memuat seluruh response ke heap. Setelah selesai, file di-rename ke `/rfid_db.txt` untuk menjaga atomicity.

**FR-30** — Hanya baris yang valid (tepat 10 karakter digit) yang ditulis ke file. Baris tidak valid diabaikan.

**FR-31** — Setelah download selesai, `loadRfidCacheFromFile()` dipanggil otomatis untuk memperbarui RAM cache tanpa perlu restart.

**FR-32** — `loadRfidCacheFromFile()` mengalokasikan array pointer `char**` di heap, kemudian mengalokasikan 11 byte per RFID. Total penggunaan heap untuk 2000 RFID sekitar 22KB.

**FR-33** — `checkAndUpdateRfidDb()` dipanggil dari loop periodic check setiap 3 jam jika WiFi tersambung dan SD tersedia.

### 4.5 Sinkronisasi Data

**FR-34** — NVS buffer disync ke server SEBELUM SD queue (prioritas NVS lebih tinggi).

**FR-35** — Chunked sync SD berjalan non-blocking: max 5 file per siklus, max 15 detik per siklus.

**FR-36** — Record lebih dari 30 hari dibuang saat sync, tidak dikirim ke server.

**FR-37** — Record ditolak server dicatat ke `failed_log.csv` di SD card.

**FR-38** — Sync berjalan di latar belakang tanpa feedback visual atau audio ke pengguna.

### 4.6 OTA Update

**FR-39** — OTA check dilakukan setiap 3 jam dan langsung saat boot pertama (`lastOtaCheck = 0`).

**FR-40** — OTA dieksekusi otomatis tanpa konfirmasi, hanya jika tidak ada tap RFID aktif (`!rfidFeedback.active`).

**FR-41** — WDT dinonaktifkan selama proses download dan flashing OTA.

**FR-42** — Jika OTA gagal, WDT diinisialisasi ulang dan sistem melanjutkan operasi normal.

### 4.7 Manajemen Daya

**FR-43** — OLED dimatikan jam 08:00–14:00 untuk menghemat daya baterai UPS.

**FR-44** — Saat OLED mati dan ada tap kartu, OLED menyala sementara untuk feedback lalu kembali ke state jadwal.

**FR-45** — Deep sleep aktif jam 18:00–05:00. Durasi dihitung tepat ke jam 05:00, min 60 detik, max 12 jam.

**FR-46** — Sebelum masuk deep sleep, durasi sleep disimpan ke `sleepDurationSeconds` di RTC RAM untuk digunakan sebagai kompensasi waktu saat bangun.

**FR-47** — Jika ada sync berjalan saat jam sleep, sync diselesaikan dulu sebelum masuk sleep.

**FR-48** — WDT dinonaktifkan sebelum `esp_deep_sleep_start()`. Safety net reinit WDT ditempatkan setelah pemanggilan tersebut untuk kondisi sleep gagal.

### 4.8 Kesehatan Sistem

**FR-49** — SD card di-health check setiap 30 detik via `fatType()`.

**FR-50** — WiFi reconnect dicoba setiap 5 menit via state machine: IDLE → INIT → TRYING → SUCCESS/FAILED.

**FR-51** — `esp_task_wdt_reset()` ditempatkan di setiap iterasi loop operasi panjang: `countAllOfflineRecords`, `isDuplicateInternal`, `initSDCard`, `saveToQueue`, `readQueueFile`, `chunkedSync`, `downloadRfidDb` (per chunk), `loadRfidCacheFromFile` (per baris).

---

## 5. PERSYARATAN NON-FUNGSIONAL

### 5.1 Performa

| Metrik | Nilai |
|---|---|
| Tap latency (ada SD, RFID valid di cache RAM)       | < 50ms                  |
| Tap latency (ada SD, RFID tidak ada di cache RAM)   | < 50ms (langsung tolak) |
| Tap latency (tanpa SD, online, server OK) | < 10 detik |
| Tap latency (tanpa SD, server down/lambat) | < 50ms (NVS) |
| Tap latency (offline) | < 50ms (NVS) |
| Timeout koneksi WiFi boot | ~6 detik (20×300ms) |
| Timeout HTTP presensi langsung | 10 detik |
| Timeout HTTP sync bulk | 30 detik |
| Timeout OTA download | 60 detik |
| Timeout HTTP validasi RFID online | 8 detik |

### 5.2 Kapasitas Penyimpanan

| Parameter | Nilai |
|---|---|
| Max records per file queue | 25 |
| Max file queue | 2.000 |
| Total kapasitas queue SD | 50.000 record |
| Ambang warning queue | 1.600 file (80%) |
| NVS buffer | 20 record |
| Usia maksimum data untuk sync | 30 hari |
| Usia maksimum estimasi waktu RTC | 12 jam |
| Estimasi ukuran rfid_db.txt (2000 RFID) | ~22KB |
| RAM cache usage (2000 RFID) | ~22KB heap |

### 5.3 Keamanan

- Semua komunikasi ke server menggunakan HTTPS via `WiFiClientSecure`
- Setiap request menyertakan header `X-API-KEY`
- SSL certificate verification dinonaktifkan (`setInsecure()`) untuk kompatibilitas embedded

### 5.4 Keandalan

- Sistem beroperasi penuh tanpa WiFi (offline mode)
- Sistem beroperasi tanpa SD card (NVS fallback)
- Data tidak hilang karena putus listrik — tersimpan di SD/NVS sebelum konfirmasi server
- Waktu sistem tetap valid setelah deep sleep via kompensasi `sleepDurationSeconds` di RTC RAM
- Waktu sistem dapat dipulihkan setelah reset paksa atau power putus via NVS flash (`last_time`)
- WDT memastikan restart otomatis jika sistem hang > 60 detik
- Queue overwrite protection mencegah data belum ter-sync tertimpa
- Download RFID DB menggunakan file sementara untuk mencegah file korup jika download terputus

### 5.5 Kompatibilitas

- ESP32 Arduino Core v3.x (API `esp_task_wdt_config_t` yang digunakan)
- SPI shared antara RC522 dan SD card dengan CS pin terpisah (SS:7, CS:1)
- SD card diakses pada kecepatan 10MHz

---

## 6. TIMING & KONSTANTA SISTEM

| Konstanta | Nilai | Keterangan |
|---|---|---|
| `DEBOUNCE_TIME` | 150ms | Jeda minimum dua tap UID sama |
| `MIN_REPEAT_INTERVAL` | 1800s (30 menit) | Window duplikat presensi |
| `SYNC_INTERVAL` | 300s (5 menit) | Interval sync SD dan NVS |
| `TIME_SYNC_INTERVAL` | 3600s (1 jam) | Interval NTP periodik |
| `RECONNECT_INTERVAL` | 300s (5 menit) | Interval reconnect WiFi |
| `RECONNECT_TIMEOUT` | 15s | Timeout satu sesi reconnect |
| `OTA_CHECK_INTERVAL` | 10800s (3 jam) | Interval cek firmware baru |
| `RFID_DB_CHECK_INTERVAL` | 10800s (3 jam) | Interval cek & update RFID DB |
| `MAX_SYNC_TIME` | 15s | Durasi max satu siklus chunked sync |
| `RFID_FEEDBACK_DISPLAY_MS` | 1800ms | Durasi tampil feedback tap |
| `SD_REDETECT_INTERVAL` | 30s | Interval health check SD |
| `OLED_SCHEDULE_CHECK_INTERVAL` | 60s | Interval cek jadwal OLED |
| `MAX_OFFLINE_AGE` | 2592000s (30 hari) | Usia max data untuk sync |
| `MAX_TIME_ESTIMATE_AGE` | 43200s (12 jam) | Usia max estimasi waktu RTC |
| `WDT_TIMEOUT_SEC` | 60s | Timeout watchdog |
| `SLEEP_START_HOUR` | 18:00 | Mulai deep sleep |
| `SLEEP_END_HOUR` | 05:00 | Bangun dari deep sleep |
| `OLED_DIM_START_HOUR` | 08:00 | OLED mulai dimatikan |
| `OLED_DIM_END_HOUR` | 14:00 | OLED dinyalakan kembali |
| `GMT_OFFSET_SEC` | 25200 (UTC+7) | Offset zona waktu WIB |

---

## 7. JADWAL OPERASIONAL

| Waktu | Status OLED | Status Sistem |
|---|---|---|
| 00:00 – 07:59 | ON | Aktif, presensi pagi |
| 08:00 – 13:59 | OFF (hemat daya) | Aktif, OLED menyala sementara saat tap |
| 14:00 – 17:59 | ON | Aktif, presensi sore |
| 18:00 – 04:59 | OFF | Deep sleep |

---

## 8. TABEL FEEDBACK OLED

| Kondisi | Baris 1 | Baris 2 | Buzzer |
|---|---|---|---|
| Simpan ke SD berhasil | `BERHASIL` | `DATA TERSIMPAN` | 2× beep |
| Queue hampir penuh (≥1600 file) | `BERHASIL` | `QUEUE HAMPIR PENUH!` | 2× beep |
| Kirim langsung berhasil | `BERHASIL` | `PRESENSI OK` | 2× beep |
| Tersimpan di NVS buffer | `BERHASIL` | `BUFFER X/20` | 2× beep |
| Duplikat presensi | `INFO` | `CUKUP SEKALI!` | 3× beep |
| RFID tidak terdaftar (lokal) | `INFO` | `HUBUNGI ADMIN` | 3× beep |
| RFID tidak dikenal server (tanpa SD) | `INFO` | `RFID UNKNOWN` | 3× beep |
| Server error | `INFO` | `SERVER ERR X` | 3× beep |
| SD card error | `INFO` | `SD CARD ERROR` | 3× beep |
| NVS buffer penuh | `INFO` | `BUFFER PENUH!` | 3× beep |
| Waktu tidak valid | `INFO` | `WAKTU INVALID` | 3× beep |

---

## 9. FITUR YANG TIDAK ADA DI VERSI INI

| Fitur | Keterangan | Target |
|---|---|---|
| Modul fingerprint | Hardware belum tersedia | v2.3.x |

---

## 10. DEPENDENSI LIBRARY

| Library | Versi | Keterangan |
|---|---|---|
| MFRC522 | ≥ 1.4.10 | Driver RFID RC522 |
| Adafruit SSD1306 | ≥ 2.5.7 | Driver OLED |
| Adafruit GFX Library | ≥ 1.11.9 | Grafis OLED |
| ArduinoJson | ≥ 7.x | Parse/serialize JSON |
| SdFat | ≥ 2.2.x | Akses SD card |
| WiFi | ESP32 core | Koneksi WiFi |
| WiFiClientSecure | ESP32 core | HTTPS client |
| HTTPClient | ESP32 core | HTTP request |
| HTTPUpdate | ESP32 core | OTA via HTTP |
| Wire | ESP32 core | I2C |
| SPI | ESP32 core | SPI bus |
| time.h | ESP32 core | POSIX time |
| esp_task_wdt | ESP32 core | Watchdog timer |
| Preferences | ESP32 core | NVS storage |
| Update | ESP32 core | OTA flash writer |

> **Catatan:** Gunakan ESP32 Arduino core v3.x. API `esp_task_wdt_init` menggunakan struct `esp_task_wdt_config_t` yang hanya tersedia di core v3.x.
