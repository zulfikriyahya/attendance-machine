# Software Requirements Specification (SRS)
## Sistem Presensi Pintar (RFID) — Queue System
**Versi:** 2.2.8
**Device:** ESP32-C3 Super Mini
**Author:** Yahya Zulfikri
**Dibuat:** Juli 2025
**Diperbarui:** Maret 2026
**Status:** Final

---

## 1. PENDAHULUAN

### 1.1 Tujuan
Dokumen ini mendeskripsikan seluruh persyaratan fungsional dan non-fungsional untuk firmware Sistem Presensi Pintar berbasis RFID yang berjalan di atas mikrokontroler ESP32-C3 Super Mini. Sistem ini dirancang untuk mencatat kehadiran pegawai dan siswa melalui tap kartu RFID, dengan kemampuan menyimpan data secara offline dan mensinkronisasikannya ke server secara otomatis.

### 1.2 Ruang Lingkup
Firmware ini menangani:
- Pembacaan kartu RFID dan pencatatan presensi
- Penyimpanan data offline ke SD card dan NVS buffer
- Sinkronisasi data ke server via HTTPS
- Update firmware otomatis via OTA
- Manajemen daya dengan deep sleep dan OLED scheduling
- Feedback visual (OLED) dan audio (buzzer) ke pengguna

### 1.3 Definisi dan Singkatan

| Istilah | Definisi |
|---|---|
| RFID | Radio Frequency Identification — teknologi identifikasi nirsentuh |
| NVS | Non-Volatile Storage — penyimpanan internal ESP32 yang persisten |
| OTA | Over-The-Air — mekanisme update firmware via jaringan |
| WDT | Watchdog Timer — mekanisme reset otomatis jika sistem hang |
| Queue | Antrian file CSV di SD card untuk data presensi offline |
| Sync | Proses pengiriman data offline ke server |
| Deep Sleep | Mode tidur ESP32 dengan konsumsi daya sangat rendah |
| RTC Memory | Memori ESP32 yang tetap tersimpan saat deep sleep |

### 1.4 Konteks Sistem
- **Sumber daya:** Adaptor listrik PLN dengan baterai 1000–3000 mAh sebagai UPS
- **Konektivitas:** WiFi 2.4GHz, dengan kemampuan operasi offline penuh
- **Pengguna:** Pegawai dan siswa yang melakukan tap kartu RFID
- **Backend:** Server Laravel di `https://zedlabs.id` dengan autentikasi API Key

---

## 2. ALUR LOGIKA SISTEM

### 2.1 Fase Boot / Setup
```
[Power ON / Reboot]
  ├─ Init WDT (60 detik)
  ├─ Init I2C (OLED SDA:8, SCL:9) + Buzzer (PIN:10)
  ├─ Init OLED SSD1306 → tampilkan animasi startup + startup melody
  ├─ Baca MAC Address → generate deviceId = "ESP32_XXYY" (huruf kapital)
  ├─ Init SPI Bus (SCK:4, MISO:5, MOSI:6)
  │
  ├─ [Init SD Card]
  │    ├─ BERHASIL
  │    │    ├─ loadMetadata() → restore cachedPendingRecords + currentQueueFile
  │    │    ├─ Jika metadata tidak ada → cari file queue belum penuh / buat baru
  │    │    └─ Jika cachedPendingRecords > 0 → tampilkan "DATA OFFLINE: X TERSISA"
  │    └─ GAGAL
  │         ├─ Tampilkan "SD CARD TIDAK ADA"
  │         └─ Jika NVS count > 0 → tampilkan "NVS BUFFER: X TERSISA"
  │
  ├─ [Koneksi WiFi] (max 20 retry × 300ms)
  │    ├─ BERHASIL → [Ping API] (max 3 retry)
  │    │              ├─ API OK
  │    │              │    ├─ syncTimeWithFallback()
  │    │              │    ├─ Jika NVS count > 0 → nvsSyncToServer()
  │    │              │    └─ Jika SD ada + pending > 0 → chunkedSync()
  │    │              └─ API GAGAL → "OFFLINE MODE"
  │    └─ GAGAL → offlineBootFallback() → "NO WIFI / OFFLINE MODE"
  │
  ├─ [Init RFID RC522]
  │    └─ VersionReg = 0x00 atau 0xFF → tampilkan "RC522 GAGAL" → ESP.restart()
  │
  ├─ Tampilkan "SISTEM SIAP" + status ONLINE/OFFLINE
  ├─ Set semua timer
  └─ checkOLEDSchedule() → masuk LOOP
```

### 2.2 Fase Main Loop
```
[LOOP - berjalan terus menerus]
  │
  ├─ [1] esp_task_wdt_reset()
  │
  ├─ [2] Cek RFID Feedback Timer
  │       └─ Jika aktif dan ≥ 1800ms sejak ditampilkan
  │            ├─ rfidFeedback.active = false
  │            ├─ Jika wasOledOff → checkOLEDSchedule()
  │            └─ Reset previousDisplay (paksa redraw standby)
  │
  ├─ [3] checkOLEDSchedule() — setiap 60 detik
  │       ├─ Jam 08:00–14:00 → turnOffOLED() [hemat baterai]
  │       └─ Di luar jam itu  → turnOnOLED()
  │
  ├─ [4] checkSDHealth() — setiap 30 detik
  │       ├─ SD tidak ada → coba reinitSDCard()
  │       │    └─ Berhasil → tampilkan "SD CARD TERBACA KEMBALI"
  │       └─ SD ada → cek via fatType()
  │            └─ Tidak sehat → sdCardAvailable = false → "SD CARD TERLEPAS!"
  │
  ├─ [5] CEK KARTU RFID (PRIORITAS TERTINGGI)
  │       └─ PICC_IsNewCardPresent() && PICC_ReadCardSerial()
  │            └─ handleRFIDScan() → return (skip sisa loop iterasi ini)
  │
  ├─ [6] processReconnect() — state machine
  │       ├─ IDLE    → jika WiFi putus dan ≥ 5 menit → INIT
  │       ├─ INIT    → disconnect + WiFi.begin() → TRYING
  │       ├─ TRYING  → tunggu max 15 detik
  │       │    ├─ Connected → SUCCESS
  │       │    └─ Timeout   → FAILED → IDLE
  │       └─ SUCCESS
  │            ├─ isOnline = true
  │            ├─ syncTimeWithFallback()
  │            ├─ Jika NVS count > 0 → nvsSyncToServer()
  │            ├─ Jika SD + pending > 0 → chunkedSync()
  │            └─ → IDLE
  │
  ├─ [7] Update Display — setiap 1 detik
  │       ├─ updateCurrentDisplayState()
  │       │    ├─ Status WiFi, jam, pendingRecords (SD+NVS), sinyal WiFi
  │       └─ updateStandbySignal() — hanya jika state berubah
  │            ├─ Baris 1: CONNECTING... / SYNCING... / ONLINE / OFFLINE
  │            ├─ Baris 2: "TAP KARTU"
  │            ├─ Baris 3: jam HH:MM
  │            ├─ Baris 4: "Q:X" jika pending > 0
  │            └─ Pojok kanan: bar sinyal WiFi (4 bar)
  │
  ├─ [8] Periodic Check — setiap 1 detik
  │       ├─ [Jika WiFi connected]
  │       │    ├─ checkOtaUpdate() — guard internal 3 jam
  │       │    │    └─ Jika ada update → set otaState.updateAvailable = true
  │       │    ├─ Jika otaState.updateAvailable && !rfidFeedback.active
  │       │    │    └─ performOtaUpdate()
  │       │    ├─ Jika NVS count > 0 && ≥ 5 menit → nvsSyncToServer()
  │       │    └─ Jika SD ada
  │       │         ├─ syncState.inProgress → chunkedSync() (lanjutkan)
  │       │         └─ Jika ≥ 5 menit && pending > 0 → chunkedSync() (mulai baru)
  │       └─ periodicTimeSync() — setiap 1 jam
  │
  └─ [9] Sleep Mode Check
          └─ Jika jam 18:00–05:00 && waktu valid
               ├─ Jika sync sedang berjalan → chunkedSync() → return
               ├─ Hitung durasi sleep (min 60 detik, max 12 jam)
               ├─ Tampilkan "SLEEP FOR X Jam Y Menit"
               ├─ Matikan OLED
               ├─ esp_task_wdt_deinit()
               ├─ esp_sleep_enable_timer_wakeup()
               ├─ esp_deep_sleep_start()
               └─ [safety net jika sleep gagal]
                    ├─ esp_task_wdt_init()
                    └─ esp_task_wdt_add()
```

### 2.3 Fase RFID Scan Handler
```
[handleRFIDScan() dipanggil]
  ├─ deselectSD() → aktifkan RFID (PIN_RFID_SS LOW)
  ├─ uidToString() → konversi UID ke 10 digit desimal
  ├─ Debounce: UID sama dalam 150ms → abaikan, return
  ├─ Simpan lastUID + update lastScan timer
  ├─ Jika OLED mati → turnOnOLED() + catat wasOledOff = true
  ├─ Tampilkan "RFID | [UID]" + playToneNotify()
  ├─ kirimPresensi(rfid) → dapat message + success/fail
  ├─ Tampilkan "BERHASIL/INFO | [message]"
  ├─ playToneSuccess() atau playToneError()
  ├─ Set rfidFeedback.active = true, catat waktu + wasOledOff
  └─ PICC_HaltA() + PCD_StopCrypto1() + deaktifkan RFID

[kirimPresensi()]
  ├─ isTimeValid()? → Tidak → "WAKTU INVALID", return false
  ├─ getFormattedTimestamp() + time(nullptr)
  │
  ├─ PRIORITAS 1: SD tersedia
  │    ├─ saveToQueue()
  │    │    ├─ isDuplicateInternal() (3 file terakhir, window 30 menit)
  │    │    │    └─ Duplikat → return false
  │    │    ├─ Cek currentFile penuh (≥ 25 record)
  │    │    │    └─ Penuh → rotasi ke file berikutnya (circular 0–1999)
  │    │    │         └─ File berikutnya berisi data → return false
  │    │    ├─ Tulis ke CSV + file.sync()
  │    │    ├─ cachedPendingRecords++
  │    │    └─ saveMetadata()
  │    ├─ BERHASIL → "DATA TERSIMPAN" / "QUEUE HAMPIR PENUH!" (≥1600 file)
  │    └─ GAGAL
  │         ├─ isDuplicateInternal() → "CUKUP SEKALI!"
  │         └─ Bukan duplikat → "SD CARD ERROR"
  │
  ├─ PRIORITAS 2: SD tidak ada + WiFi connected
  │    ├─ kirimLangsung() → POST /api/presensi
  │    │    ├─ HTTP 200 → "PRESENSI OK", return true
  │    │    ├─ HTTP 400 → "CUKUP SEKALI!", return false
  │    │    ├─ HTTP 404 → "RFID UNKNOWN", return false
  │    │    └─ HTTP lain → "SERVER ERR X", return false
  │    └─ Jika kirimLangsung gagal → fallback NVS
  │         ├─ nvsIsDuplicate() → "CUKUP SEKALI!"
  │         ├─ nvsSaveToBuffer() → "BUFFER X/20"
  │         └─ NVS penuh → "BUFFER PENUH!"
  │
  └─ PRIORITAS 3: SD tidak ada + WiFi tidak ada
       ├─ nvsIsDuplicate() → "CUKUP SEKALI!"
       ├─ nvsSaveToBuffer() → "BUFFER X/20"
       └─ NVS penuh → "BUFFER PENUH!"
```

### 2.4 Fase Sync Data
```
[chunkedSync() — non-blocking]
  ├─ Syarat: SD ada + WiFi connected
  ├─ Satu siklus: max 5 file, max 15 detik
  ├─ Loop queue_0.csv → queue_1999.csv
  │    ├─ File ada + record > 0
  │    │    ├─ readQueueFile()
  │    │    │    └─ Filter: buang record > 30 hari
  │    │    ├─ POST bulk ke /api/presensi/sync-bulk
  │    │    │    ├─ HTTP 200
  │    │    │    │    ├─ Cek response per item → error → appendFailedLog()
  │    │    │    │    └─ sd.remove(filename) + pendingCacheDirty = true
  │    │    │    └─ Gagal → skip, coba siklus berikutnya
  │    │    └─ WiFi putus → syncState.inProgress = false, return
  │    └─ File kosong → sd.remove() langsung
  └─ Semua file selesai
       ├─ syncState.inProgress = false
       ├─ refreshPendingCache()
       └─ Jika pending = 0 → tampilkan "SYNC SELESAI!" + playToneSuccess()

[nvsSyncToServer()]
  ├─ Syarat: NVS count > 0 + WiFi connected
  ├─ POST bulk semua record NVS ke /api/presensi/sync-bulk
  ├─ HTTP 200
  │    ├─ Cek response per item → error → appendFailedLog()
  │    └─ Hapus semua record NVS + nvsSetCount(0)
  └─ Gagal → biarkan, coba lagi di interval berikutnya
```

### 2.5 Fase OTA Update
```
[checkOtaUpdate() — guard 3 jam]
  ├─ POST /api/presensi/firmware/check
  │    Body: { version: "2.2.8", device_id: "ESP32_XXYY" }
  ├─ Response: { update: true, version: "X.X.X", url: "https://..." }
  ├─ Simpan ke otaState
  └─ Tampilkan "UPDATE vX.X.X TERSEDIA" + playToneNotify()

[performOtaUpdate() — otomatis, tanpa konfirmasi]
  ├─ Syarat: otaState.updateAvailable + WiFi + !rfidFeedback.active
  ├─ esp_task_wdt_delete() + esp_task_wdt_deinit()
  ├─ GET binary dari otaState.url (timeout 60 detik)
  ├─ HTTP 200
  │    ├─ Update.begin(totalSize)
  │    │    └─ Gagal → "UPDATE GAGAL NO SPACE" → goto reinit_wdt
  │    ├─ Stream write 1024 byte/chunk
  │    ├─ Update.end() && isFinished()
  │    │    ├─ BERHASIL → "UPDATE OK" → ESP.restart()
  │    │    └─ GAGAL → "UPDATE GAGAL ERR X" → goto reinit_wdt
  │    └─ HTTP error → "UPDATE GAGAL HTTP ERR X" → goto reinit_wdt
  └─ reinit_wdt:
       ├─ esp_task_wdt_init()
       ├─ esp_task_wdt_add()
       └─ Reset previousDisplay (paksa redraw)
```

### 2.6 Fase Manajemen Waktu
```
[Hierarki sumber waktu]
  1. NTP aktif (getLocalTime() valid, tm_year ≥ 120)
     └─ Sync ke: pool.ntp.org → time.google.com → id.pool.ntp.org
        Timeout per server: 2500ms
        Berhasil → simpan lastValidTime ke RTC memory
  2. Estimasi RTC memory
     └─ lastValidTime + (millis() - bootTime) / 1000
        Valid maksimal 12 jam sejak sync terakhir
  3. Tidak ada → isTimeValid() = false → presensi ditolak

[Sync periodik]
  └─ Setiap 1 jam jika WiFi connected → syncTimeWithFallback()
```

---

## 3. ARSITEKTUR SISTEM

### 2.1 Hardware
| Komponen | Pin | Fungsi |
|---|---|---|
| ESP32-C3 Super Mini | — | Mikrokontroler utama |
| MFRC522 (RC522) | SCK:4, MOSI:6, MISO:5, SS:7, RST:3 | RFID reader |
| MicroSD Card | CS:1 (SPI shared) | Penyimpanan offline queue |
| SSD1306 OLED 128×64 | SDA:8, SCL:9 | Display feedback |
| Buzzer | PIN:10 | Feedback audio |

### 2.2 Lapisan Penyimpanan Data (Prioritas)
```
Tap Kartu
    │
    ├─ [1] SD Card    → Queue CSV (prioritas utama, kapasitas besar)
    ├─ [2] NVS Buffer → Internal ESP32 (fallback jika SD tidak ada, max 20 record)
    └─ [3] Langsung   → Server (jika SD tidak ada + WiFi tersedia)
```

### 2.3 Endpoint Server
| Endpoint | Method | Fungsi |
|---|---|---|
| `/api/presensi/ping` | GET | Health check koneksi API |
| `/api/presensi` | POST | Kirim presensi langsung (realtime) |
| `/api/presensi/sync-bulk` | POST | Sinkronisasi bulk data offline |
| `/api/presensi/firmware/check` | POST | Cek ketersediaan update OTA |

---

## 3. PERSYARATAN FUNGSIONAL

### 3.1 Boot & Inisialisasi

**FR-01 — Inisialisasi Hardware**
- Sistem HARUS menginisialisasi WDT dengan timeout 60 detik saat boot
- Sistem HARUS menginisialisasi I2C (OLED), SPI (RFID + SD), dan buzzer
- Sistem HARUS menampilkan animasi startup dan memainkan startup melody
- Sistem HARUS menghasilkan `deviceId` dari 2 byte terakhir MAC Address dengan format `ESP32_XXYY` (huruf kapital)

**FR-02 — Inisialisasi SD Card**
- Sistem HARUS mencoba menginisialisasi SD card saat boot
- Jika berhasil, sistem HARUS memuat metadata (cachedPendingRecords, currentQueueFile) dari `/queue_meta.txt`
- Jika metadata tidak ada, sistem HARUS mencari file queue yang belum penuh atau membuat file baru
- Jika SD tidak tersedia, sistem HARUS melanjutkan boot dalam mode tanpa SD
- Sistem HARUS menampilkan jumlah data offline yang tersisa jika lebih dari 0

**FR-03 — Koneksi WiFi**
- Sistem HARUS mencoba koneksi WiFi saat boot dengan maksimal 20 retry (±6 detik)
- Sistem HARUS menampilkan progress koneksi di OLED beserta nama SSID
- Jika berhasil, sistem HARUS melanjutkan ke ping API
- Jika gagal, sistem HARUS masuk ke offline mode dan melanjutkan boot

**FR-04 — Ping API**
- Sistem HARUS melakukan ping ke `/api/presensi/ping` maksimal 3 kali retry
- Jika berhasil, sistem HARUS melakukan sinkronisasi waktu NTP
- Jika gagal setelah 3 retry, sistem HARUS masuk ke offline mode

**FR-05 — Inisialisasi RFID**
- Sistem HARUS menginisialisasi modul RC522
- Sistem HARUS membaca VersionReg setelah inisialisasi
- Jika nilai register adalah `0x00` atau `0xFF`, sistem HARUS restart otomatis

---

### 3.2 Pembacaan & Pencatatan Presensi

**FR-06 — Pembacaan Kartu RFID**
- Sistem HARUS memindai kartu RFID di setiap iterasi loop dengan prioritas tertinggi
- UID kartu HARUS dikonversi ke format 10 digit desimal
- Sistem HARUS mengimplementasikan debounce 150ms untuk UID yang sama
- Jika OLED sedang mati, sistem HARUS menyalakannya sementara saat kartu di-tap

**FR-07 — Validasi Waktu**
- Sistem HARUS menolak pencatatan presensi jika waktu tidak valid
- Waktu dianggap valid jika: NTP berhasil sync, ATAU estimasi dari RTC memory tidak lebih dari 12 jam sejak sync terakhir
- Pesan yang ditampilkan jika waktu tidak valid: `"WAKTU INVALID"`

**FR-08 — Pencatatan ke SD Card (Prioritas Utama)**
- Jika SD tersedia, sistem HARUS menyimpan data ke queue CSV tanpa validasi ke server terlebih dahulu (prioritas kecepatan tap)
- Sistem HARUS memeriksa duplikat dalam window 30 menit dari 3 file queue terakhir sebelum menyimpan
- Jika duplikat ditemukan, sistem HARUS menampilkan `"CUKUP SEKALI!"`
- Setiap file queue menampung maksimal 25 record dengan header `rfid,timestamp,device_id,unix_time`
- Jika file penuh, sistem HARUS rotasi ke file berikutnya secara circular (queue_0.csv — queue_1999.csv)
- Jika file berikutnya masih berisi data, sistem HARUS menolak penyimpanan
- Setelah berhasil simpan, sistem HARUS menampilkan `"DATA TERSIMPAN"` atau `"QUEUE HAMPIR PENUH!"` jika jumlah file ≥ 1600

**FR-09 — Pencatatan Langsung ke Server (Fallback SD)**
- Jika SD tidak tersedia dan WiFi tersambung, sistem HARUS mengirim langsung ke `/api/presensi`
- Response HTTP 200 → tampilkan `"PRESENSI OK"`
- Response HTTP 400 → tampilkan `"CUKUP SEKALI!"`
- Response HTTP 404 → tampilkan `"RFID UNKNOWN"`
- Response lain → tampilkan `"SERVER ERR [kode]"`
- Jika pengiriman langsung gagal, sistem HARUS fallback ke NVS buffer

**FR-10 — Pencatatan ke NVS Buffer (Fallback Terakhir)**
- Jika SD tidak tersedia dan WiFi tidak tersambung (atau kirim langsung gagal), sistem HARUS menyimpan ke NVS buffer
- NVS buffer menampung maksimal 20 record
- Sistem HARUS memeriksa duplikat NVS dalam window 30 menit sebelum menyimpan
- Jika berhasil, tampilkan `"BUFFER X/20"`
- Jika penuh, tampilkan `"BUFFER PENUH!"`

**FR-11 — Feedback Pengguna**
- Sistem HARUS selalu menampilkan feedback di OLED setelah setiap tap kartu tanpa terkecuali
- Feedback HARUS disertai buzzer: 2x beep pendek untuk sukses, 3x beep untuk gagal/info
- Feedback HARUS ditampilkan minimal 1800ms sebelum layar kembali ke standby

---

### 3.3 Sinkronisasi Data

**FR-12 — Chunked Sync SD**
- Sistem HARUS melakukan sinkronisasi data SD ke server setiap 5 menit jika WiFi tersambung dan ada data pending
- Satu siklus sync dibatasi maksimal 5 file dan durasi 15 detik (non-blocking)
- Data lebih dari 30 hari HARUS dibuang, tidak dikirim ke server
- Setelah sync berhasil, file queue HARUS dihapus dari SD
- Sistem HARUS mencatat record yang gagal di server ke `/failed_log.csv`

**FR-13 — NVS Sync**
- Sistem HARUS mencoba mensinkronisasi NVS buffer ke server setiap 5 menit jika WiFi tersambung
- Setelah sync berhasil, seluruh record NVS HARUS dihapus
- NVS juga HARUS disync saat reconnect WiFi berhasil

**FR-14 — Sync saat Reconnect**
- Saat WiFi berhasil tersambung kembali, sistem HARUS langsung mensinkronisasi NVS dan SD queue secara berurutan

---

### 3.4 Manajemen Waktu

**FR-15 — Sinkronisasi NTP**
- Sistem HARUS mencoba sync NTP ke 3 server secara berurutan: `pool.ntp.org`, `time.google.com`, `id.pool.ntp.org`
- Timeout per server: 2500ms
- Jika berhasil, waktu HARUS disimpan ke RTC memory (`lastValidTime`)
- Sistem HARUS melakukan periodic NTP sync setiap 1 jam

**FR-16 — Fallback Waktu**
- Jika NTP tidak tersedia, sistem HARUS menggunakan estimasi waktu dari `lastValidTime + elapsed millis()`
- Estimasi waktu hanya valid maksimal 12 jam sejak sync NTP terakhir
- Jika tidak ada sumber waktu valid sama sekali, presensi HARUS ditolak

---

### 3.5 OTA Update

**FR-17 — Pengecekan Firmware**
- Sistem HARUS memeriksa ketersediaan firmware baru setiap 3 jam via `/api/presensi/firmware/check`
- Request HARUS menyertakan versi saat ini (`2.2.8`) dan `device_id`
- Jika ada update, sistem HARUS menampilkan notifikasi versi baru di OLED

**FR-18 — Eksekusi OTA**
- Sistem HARUS mengeksekusi update OTA secara otomatis tanpa konfirmasi (fully automatic by design)
- OTA HARUS dijalankan hanya jika tidak ada feedback RFID aktif
- WDT HARUS dinonaktifkan selama proses download dan flashing
- Jika berhasil, sistem HARUS restart otomatis
- Jika gagal, WDT HARUS diinisialisasi ulang dan sistem melanjutkan operasi normal

---

### 3.6 Manajemen Daya

**FR-19 — Deep Sleep**
- Sistem HARUS masuk deep sleep pada jam 18:00 – 05:00 WIB
- Durasi sleep HARUS dihitung tepat hingga jam 05:00, minimum 60 detik, maksimum 12 jam
- Jika ada proses sync yang sedang berjalan, sistem HARUS menyelesaikannya dulu sebelum tidur
- WDT HARUS dinonaktifkan sebelum `esp_deep_sleep_start()`

**FR-20 — OLED Scheduling**
- OLED HARUS dimatikan pada jam 08:00 – 14:00 untuk menghemat daya baterai
- Di luar jam tersebut (dan di luar jam sleep), OLED HARUS menyala
- Saat tap kartu dengan OLED mati, OLED HARUS menyala sementara untuk menampilkan feedback, lalu kembali ke state sesuai jadwal

---

### 3.7 Kesehatan Sistem

**FR-21 — SD Card Health Check**
- Sistem HARUS memeriksa kesehatan SD card setiap 30 detik via `fatType()`
- Jika SD terlepas, sistem HARUS menampilkan notifikasi dan mencoba reinisialisasi
- Jika SD kembali terbaca, sistem HARUS menampilkan notifikasi dan memperbarui cache

**FR-22 — WiFi Reconnect**
- Sistem HARUS mencoba reconnect WiFi setiap 5 menit jika koneksi terputus
- Timeout reconnect: 15 detik
- Reconnect diimplementasikan sebagai state machine: IDLE → INIT → TRYING → SUCCESS/FAILED

**FR-23 — Watchdog Timer**
- WDT HARUS direset (`esp_task_wdt_reset()`) secara rutin di setiap loop dan di setiap operasi yang berpotensi lama (scan file, sync, OTA)

---

### 3.8 Display Standby

**FR-24 — Informasi Standby**
- Saat tidak ada aktivitas, OLED HARUS menampilkan: status online/offline, jam saat ini, jumlah data pending (SD + NVS), dan kekuatan sinyal WiFi (4 bar)
- Display HARUS diperbarui setiap 1 detik, hanya jika ada perubahan state (efisiensi)
- Saat reconnect, OLED HARUS menampilkan `"CONNECTING..."`
- Saat sync berjalan, OLED HARUS menampilkan `"SYNCING..."`

---

## 4. PERSYARATAN NON-FUNGSIONAL

### 4.1 Performa
| Parameter | Nilai |
|---|---|
| Waktu feedback tap kartu | < 500ms sejak kartu terdeteksi |
| Timeout koneksi WiFi boot | ±6 detik (20 retry × 300ms) |
| Timeout HTTP request presensi | 10 detik |
| Timeout HTTP sync bulk | 30 detik |
| Timeout OTA download | 60 detik |
| Frekuensi scan RFID | Setiap iterasi loop (tidak ada delay tetap) |

### 4.2 Kapasitas Penyimpanan
| Parameter | Nilai |
|---|---|
| Maksimum file queue | 2000 file |
| Record per file queue | 25 record |
| Total kapasitas queue | 50.000 record |
| Ambang batas warning queue | 1600 file (80%) |
| NVS buffer | 20 record |
| Maksimum usia data sync | 30 hari |
| Maksimum usia estimasi waktu | 12 jam |

### 4.3 Keamanan
- Semua komunikasi ke server HARUS menggunakan HTTPS
- Setiap request HARUS menyertakan header `X-API-KEY`
- Verifikasi sertifikat SSL dinonaktifkan (`setInsecure()`) untuk kompatibilitas embedded

### 4.4 Keandalan
- Sistem HARUS tetap berfungsi penuh tanpa koneksi WiFi (offline mode)
- Sistem HARUS tetap berfungsi tanpa SD card (NVS fallback)
- Data presensi TIDAK BOLEH hilang karena putus listrik (tersimpan di SD/NVS sebelum konfirmasi)
- WDT memastikan sistem restart otomatis jika terjadi hang

### 4.5 Kompatibilitas
- Firmware dikompilasi untuk ESP32-C3
- Komunikasi SPI shared antara RC522 dan SD card dengan CS pin terpisah
- SD card diakses pada kecepatan 10MHz

---

## 5. TIMING & KONSTANTA SISTEM

| Konstanta | Nilai | Keterangan |
|---|---|---|
| `DEBOUNCE_TIME` | 150ms | Jeda minimum antara dua tap UID sama |
| `MIN_REPEAT_INTERVAL` | 1800s (30 menit) | Jeda minimum presensi sama dianggap duplikat |
| `SYNC_INTERVAL` | 300s (5 menit) | Interval sinkronisasi SD dan NVS |
| `TIME_SYNC_INTERVAL` | 3600s (1 jam) | Interval sync NTP periodik |
| `RECONNECT_INTERVAL` | 300s (5 menit) | Interval coba reconnect WiFi |
| `RECONNECT_TIMEOUT` | 15s | Timeout satu sesi reconnect |
| `OTA_CHECK_INTERVAL` | 10800s (3 jam) | Interval cek firmware baru |
| `MAX_SYNC_TIME` | 15s | Durasi maksimum satu siklus chunked sync |
| `RFID_FEEDBACK_DISPLAY_MS` | 1800ms | Durasi tampil feedback tap kartu |
| `SD_REDETECT_INTERVAL` | 30s | Interval health check SD card |
| `OLED_SCHEDULE_CHECK_INTERVAL` | 60s | Interval cek jadwal OLED |
| `MAX_OFFLINE_AGE` | 2592000s (30 hari) | Usia maksimum data yang boleh disync |
| `MAX_TIME_ESTIMATE_AGE` | 43200s (12 jam) | Usia maksimum estimasi waktu dari RTC |
| `WDT_TIMEOUT_SEC` | 60s | Timeout watchdog timer |
| `SLEEP_START_HOUR` | 18:00 | Jam mulai deep sleep |
| `SLEEP_END_HOUR` | 05:00 | Jam bangun dari deep sleep |
| `OLED_DIM_START_HOUR` | 08:00 | Jam OLED mulai dimatikan |
| `OLED_DIM_END_HOUR` | 14:00 | Jam OLED dinyalakan kembali |
| `GMT_OFFSET_SEC` | 25200 (UTC+7) | Offset zona waktu WIB |

---

## 6. ALUR RESPONS FEEDBACK OLED

| Kondisi | Baris 1 | Baris 2 | Buzzer |
|---|---|---|---|
| Presensi berhasil disimpan ke SD | `BERHASIL` | `DATA TERSIMPAN` | 2x beep |
| Queue hampir penuh (≥1600 file) | `BERHASIL` | `QUEUE HAMPIR PENUH!` | 2x beep |
| Kirim langsung berhasil | `BERHASIL` | `PRESENSI OK` | 2x beep |
| Tersimpan di NVS buffer | `BERHASIL` | `BUFFER X/20` | 2x beep |
| Duplikat presensi | `INFO` | `CUKUP SEKALI!` | 3x beep |
| RFID tidak dikenal (server) | `INFO` | `RFID UNKNOWN` | 3x beep |
| Server error | `INFO` | `SERVER ERR [kode]` | 3x beep |
| SD card error | `INFO` | `SD CARD ERROR` | 3x beep |
| NVS buffer penuh | `INFO` | `BUFFER PENUH!` | 3x beep |
| Waktu tidak valid | `INFO` | `WAKTU INVALID` | 3x beep |

---

## 7. FITUR YANG TIDAK ADA DI VERSI INI

| Fitur | Keterangan | Target |
|---|---|---|
| Validasi RFID lokal dari SD | Download daftar RFID aktif dari server untuk validasi offline | v2.2.9 |
| Reinit WDT setelah sleep gagal | Safety net jika `esp_deep_sleep_start()` tidak return | v2.2.9 |
| Modul fingerprint | Hardware belum tersedia | v2.3.x |

---

## 8. DEPENDENSI LIBRARY

| Library | Fungsi |
|---|---|
| `WiFi.h` | Koneksi WiFi |
| `WiFiClientSecure.h` | HTTPS client |
| `HTTPClient.h` | HTTP request |
| `HTTPUpdate.h` | OTA via HTTP |
| `Wire.h` | Komunikasi I2C |
| `MFRC522.h` | Driver RFID RC522 |
| `SPI.h` | Komunikasi SPI |
| `Adafruit_SSD1306.h` | Driver OLED |
| `ArduinoJson.h` | Parse/serialize JSON |
| `time.h` | Manajemen waktu POSIX |
| `SdFat.h` | Akses SD card |
| `esp_task_wdt.h` | Watchdog timer ESP32 |
| `Preferences.h` | NVS storage |
| `Update.h` | OTA flash writer |
