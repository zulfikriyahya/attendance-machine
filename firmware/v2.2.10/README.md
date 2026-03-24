# Sistem Presensi Pintar Berbasis IoT (Hybrid Edition)

**Attendance Machine** adalah solusi presensi cerdas berbasis _Internet of Things_ (IoT) yang dirancang untuk mengatasi tantangan infrastruktur jaringan yang tidak stabil. Dibangun di atas mikrokontroler ESP32-C3, sistem ini menerapkan arsitektur _Hybrid_ yang menggabungkan kemampuan pemrosesan daring (_online_) dan luring (_offline_) secara mulus.

Sistem ini beroperasi dengan filosofi _Self-Healing_ dan _Store-and-Forward_, menjamin integritas data kehadiran tanpa kehilangan (_zero data loss_) melalui mekanisme antrean terpartisi (_Partitioned Queue System_), NVS buffer internal, sinkronisasi otomatis, **operasi latar belakang yang tidak mengganggu pengguna** (_Non-Intrusive Background Operations_), **pembaruan firmware jarak jauh otomatis** (_Over-The-Air Update_), dan **validasi RFID lokal berbasis database terunduh** (_Local RFID Database_).

---

## Spesifikasi

| Field | Value |
|---|---|
| Project | Madrasah Universe |
| Author | Yahya Zulfikri |
| Device | ESP32-C3 Super Mini |
| Versi | 2.2.10 |
| IDE | Arduino IDE v2.3.6 |
| Dibuat | Juli 2025 |
| Diperbarui | Maret 2026 |

---

## Arsitektur Sistem

Sistem dirancang sebagai gerbang fisik data kehadiran yang agnostik terhadap status konektivitas dengan prioritas pada responsivitas dan user experience.

### Mekanisme Operasional Utama

1. **Identifikasi:** Pengguna memindai kartu RFID pada perangkat.
2. **Validasi Lokal RFID:** Saat ada SD card, perangkat memvalidasi UID terhadap database RFID lokal (`rfid_db.txt`) sebelum menyimpan data. Jika RFID tidak ditemukan, tap ditolak dengan pesan `HUBUNGI ADMIN`. Jika file database belum ada, validasi dilewati (fallback izinkan semua).
3. **Pengecekan Duplikasi:** Perangkat melakukan verifikasi _debounce_ dan pengecekan duplikasi data dalam interval waktu tertentu (default: 30 menit) langsung pada penyimpanan lokal untuk mencegah input ganda. Pengecekan mencakup 3 file antrean terakhir untuk menutup celah di boundary file.
4. **Manajemen Penyimpanan (Queue System):** Saat ada SD card dan RFID valid, data masuk ke antrean CSV (`queue_X.csv`) tanpa panggilan jaringan. Metadata antrean disimpan di `queue_meta.txt` untuk efisiensi akses.
5. **NVS Buffer (Fallback Tanpa SD Card):** Saat SD card tidak tersedia, data disimpan sementara di flash internal ESP32 (NVS) dengan kapasitas 20 record. Data di NVS disync ke server saat koneksi tersedia dan dihapus setelah berhasil. Jika NVS penuh, tap ditolak.
6. **Sinkronisasi Latar Belakang (_Background Sync_):** Sistem secara berkala (setiap 5 menit) mengirim data secara _batch_ ke server tanpa feedback visual. NVS buffer disync terlebih dahulu sebelum queue SD card.
7. **Reconnect Otomatis (_Silent Auto-Reconnect_):** Jika WiFi terputus, sistem mencoba reconnect setiap 5 menit di latar belakang. Setelah kembali online, NVS buffer dan queue SD disync otomatis.
8. **Manajemen OLED Cerdas:** Layar OLED otomatis mati pada jam tertentu untuk hemat daya, namun tetap menyala sementara saat ada tapping kartu.
9. **OTA Update Otomatis:** Perangkat memeriksa ketersediaan firmware terbaru ke server setiap 3 jam. Jika tersedia, firmware diunduh dan di-flash secara otomatis tanpa intervensi manual.
10. **Integrasi Hilir:** Server memproses data _batch_ untuk keperluan notifikasi WhatsApp, laporan digital, dan analisis kehadiran.

---

## Spesifikasi Teknis

### Perangkat Keras

| Komponen             | Spesifikasi              | Fungsi Utama                                                               |
| :------------------- | :----------------------- | :------------------------------------------------------------------------- |
| **Unit Pemroses**    | ESP32-C3 Super Mini      | Manajemen logika utama, konektivitas WiFi, dan sistem berkas.              |
| **Sensor Identitas** | RFID RC522 (13.56 MHz)   | Pembacaan UID kartu presensi (Protokol SPI).                               |
| **Penyimpanan**      | Modul MicroSD (SPI)      | Penyimpanan antrean data offline (CSV), database RFID lokal, dan log sistem. Opsional. |
| **Antarmuka Visual** | OLED 0.96 inci (SSD1306) | Visualisasi status koneksi, jam, dan penghitung antrean (_Queue Counter_). |
| **Indikator Audio**  | Buzzer Aktif 5V          | Umpan balik audio untuk status sukses, gagal, atau kesalahan sistem.       |
| **Catu Daya**        | 5V USB / 3.7V Li-ion     | Sumber daya operasional.                                                   |

### Pinout ESP32-C3

| Komponen       | Pin Modul | Pin ESP32-C3 | Protokol | Catatan                        |
| :------------- | :-------- | :----------- | :------- | :----------------------------- |
| **Bus SPI**    | SCK       | GPIO 4       | SPI      | Jalur Clock (Shared)           |
|                | MOSI      | GPIO 6       | SPI      | Jalur Data Master Out (Shared) |
|                | MISO      | GPIO 5       | SPI      | Jalur Data Master In (Shared)  |
| **RFID RC522** | SDA (SS)  | GPIO 7       | SPI      | Chip Select RFID               |
|                | RST       | GPIO 3       | Digital  | Reset Hardware                 |
| **SD Card**    | CS        | GPIO 1       | SPI      | Chip Select SD Card            |
| **OLED**       | SDA       | GPIO 8       | I2C      | Data Display                   |
|                | SCL       | GPIO 9       | I2C      | Clock Display                  |
| **Buzzer**     | (+)       | GPIO 10      | PWM      | Indikator Audio                |

---

## Fitur Perangkat Lunak (Firmware)

### Core Features

- **Offline-First Capability:** Prioritas penyimpanan data lokal saat jaringan tidak tersedia atau tidak stabil.
- **Partitioned Queue System:** Manajemen memori tingkat lanjut yang memecah penyimpanan data menjadi berkas-berkas kecil untuk mencegah _buffer overflow_.
- **NVS Buffer:** Penyimpanan fallback di flash internal ESP32 untuk kondisi tanpa SD card. Kapasitas 20 record, persisten melewati restart dan deep sleep.
- **Local RFID Database:** Database RFID valid diunduh dari server dan disimpan di SD card (`rfid_db.txt`). Saat boot, seluruh daftar RFID dimuat ke RAM (heap) sebagai array pointer. Validasi saat tap dilakukan di RAM — tanpa akses SD card, tanpa HTTP call — sehingga latency tap tetap < 50ms baik online maupun offline. Database diperbarui otomatis setiap 3 jam jika ada perubahan di server (berbasis perbandingan versi timestamp).
- **Smart Duplicate Prevention:** Algoritma _sliding window_ yang memindai 3 indeks antrean lokal terakhir untuk menolak pemindaian kartu yang sama dalam periode waktu yang dikonfigurasi (default: 30 menit).
- **Bulk Upload Efficiency:** Mengirimkan himpunan data dalam satu permintaan HTTP POST.
- **Hybrid Timekeeping:** Sinkronisasi waktu menggunakan NTP saat daring, estimasi waktu berbasis RTC internal saat luring, dengan dua lapis persistensi:
  - **RTC RAM + Kompensasi Sleep:** Durasi deep sleep disimpan ke RTC RAM sebelum tidur dan dikompensasikan ke `lastValidTime` saat bangun, mengatasi reset `millis()` setelah deep sleep.
  - **NVS Flash Persistence:** Setiap kali NTP berhasil sync, `lastValidTime` disimpan ke NVS flash. Saat RTC RAM hilang akibat reset paksa atau power putus, waktu dipulihkan dari NVS. Waktu dari NVS tidak tahu berapa lama power mati, namun lebih baik dari waktu invalid — langsung dikoreksi saat NTP sync berhasil.
- **Deep Sleep Scheduling:** Manajemen daya otomatis di luar jam operasional (default: 18:00–05:00). Safety net reinit WDT ditempatkan langsung setelah `esp_deep_sleep_start()` di dalam `loop()` untuk kondisi sleep gagal.
- **Dual Build Mode:** Mendukung mode `DEV_MODE` (HTTP, plain `WiFiClient`) dan production (HTTPS, `WiFiClientSecure`) melalui preprocessor `#define DEV_MODE`. Cukup comment/uncomment satu baris tanpa mengubah logika lain.
- **Single SSID:** Konfigurasi jaringan satu SSID dengan reconnect state machine 4 state.

### Advanced Features

- **OTA Update Otomatis:** Perangkat memeriksa firmware terbaru ke server setiap 3 jam. Pemeriksaan pertama dilakukan langsung saat boot (`lastOtaCheck = 0`). Jika tersedia, firmware diunduh dan di-flash tanpa intervensi manual. WDT dinonaktifkan selama proses download untuk mencegah false timeout.
- **Silent Background Sync:** Sinkronisasi data berjalan di latar belakang tanpa feedback visual atau audio. NVS buffer disync sebelum queue SD.
- **Non-Intrusive Reconnect:** Auto-reconnect WiFi tanpa menampilkan loading screen.
- **Zero-Interruption UX:** Tap kartu tidak pernah terblokir oleh proses background maupun feedback OLED. OTA update hanya dieksekusi saat tidak ada tap aktif.
- **Zero Network Latency on Tap:** Tidak ada panggilan jaringan saat tap — baik kondisi ada SD maupun kondisi tanpa SD dengan jaringan lambat/down.
- **Task Watchdog (WDT):** Pemulihan otomatis 60 detik. `esp_task_wdt_reset()` ditempatkan di setiap iterasi loop operasi panjang (scan file, baca baris, duplicate check, streaming download RFID DB) untuk mencegah false timeout. WDT dinonaktifkan sebelum deep sleep dan selama proses OTA download.
- **Queue Overwrite Protection:** Rotasi file antrean tidak menimpa file yang belum ter-sync.
- **Metadata-Driven Cache:** Pending count disimpan di `queue_meta.txt`, menghindari scan penuh 2000 file setiap boot.
- **Heap Fragmentation Prevention:** Seluruh operasi string menggunakan `char[]` di stack. Download RFID DB menggunakan streaming chunk tanpa buffering seluruh response ke heap.
- **OLED Auto Dim:** Display mati otomatis pukul 08:00 dan nyala kembali pukul 14:00.
- **Smart Wake-up on Tap:** Display menyala sementara saat ada tap kartu meski dalam periode dim, menggunakan `RfidFeedback` struct non-blocking.

---

## Konfigurasi Sistem

```cpp
// Build Mode
// #define DEV_MODE // Uncomment untuk development (HTTP), comment untuk production (HTTPS)

// Konfigurasi Jaringan
const char WIFI_SSID[]       PROGMEM = "SSID_WIFI";
const char WIFI_PASSWORD[]   PROGMEM = "PasswordWifi";
const char API_BASE_URL[]    PROGMEM = "https://zedlabs.id"; // otomatis "http://..." saat DEV_MODE
const char API_SECRET_KEY[]  PROGMEM = "SecretAPIToken";
const long GMT_OFFSET_SEC    = 25200; // WIB (UTC+7)

// Konfigurasi Antrean SD
const int MAX_RECORDS_PER_FILE      = 25;
const int MAX_QUEUE_FILES           = 2000;
const unsigned long SYNC_INTERVAL   = 300000;   // 5 menit

// Konfigurasi NVS Buffer
const int NVS_MAX_RECORDS           = 20;
const char NVS_NAMESPACE[]          = "presensi";

// Konfigurasi Validasi
const unsigned long MIN_REPEAT_INTERVAL = 1800; // 30 menit

// Konfigurasi Reconnect
const unsigned long RECONNECT_INTERVAL  = 300000;  // 5 menit
const unsigned long TIME_SYNC_INTERVAL  = 3600000; // 1 jam

// Konfigurasi OTA
const char FIRMWARE_VERSION[]           = "2.2.10";
const unsigned long OTA_CHECK_INTERVAL  = 10800000; // 3 jam

// Konfigurasi RFID Local DB
const char RFID_DB_FILE[]               = "/rfid_db.txt";
const char NVS_KEY_RFID_VER[]           = "rfid_db_ver";
const unsigned long RFID_DB_CHECK_INTERVAL = 10800000; // 3 jam

// Konfigurasi Sleep Mode
const int SLEEP_START_HOUR = 18;
const int SLEEP_END_HOUR   = 5;

// Konfigurasi OLED Auto Dim
const int OLED_DIM_START_HOUR = 8;
const int OLED_DIM_END_HOUR   = 14;

// Konfigurasi Watchdog
const int WDT_TIMEOUT_SEC = 60;
```

---

## Mekanisme Antrean (Queue Logic)

1. **Segmentasi:** Data disimpan dalam berkas kecil (`queue_N.csv`) berisi maksimal 25 baris.
2. **Rotasi dengan Proteksi:** Saat berkas `queue_N` penuh, sistem membuat `queue_N+1`. Jika file target masih berisi record belum ter-sync, rotasi dibatalkan.
3. **Metadata Cache:** Jumlah pending record dan indeks file aktif disimpan di `queue_meta.txt`. Count di-increment langsung saat `saveToQueue` berhasil.
4. **Sliding Window Duplicate Check:** Memeriksa 3 berkas antrean terakhir untuk akurasi di boundary file. `esp_task_wdt_reset()` ditempatkan di setiap iterasi file dan baris untuk mencegah WDT timeout.
5. **Sinkronisasi Background:** Data dikirim batch ke server tanpa feedback visual. Jika HTTP 200, berkas dihapus. Jika gagal, berkas dipertahankan.

## Mekanisme NVS Buffer

1. **Kapasitas:** 20 record, disimpan di flash internal ESP32 (namespace `presensi`).
2. **Persistensi:** Data bertahan melewati restart dan deep sleep.
3. **Prioritas Sync:** NVS buffer disync ke server sebelum queue SD card.
4. **Full Buffer:** Jika NVS penuh dan server masih tidak dapat dihubungi, tap ditolak dengan pesan `BUFFER PENUH!`.
5. **Pembersihan:** Semua record NVS dihapus setelah server merespons HTTP 200.

## Mekanisme Timekeeping (Hybrid)

Sistem menggunakan tiga lapis mekanisme untuk menjaga akurasi waktu:

1. **NTP Sync (Online):** Setiap kali terhubung ke internet, waktu disinkronkan ke server NTP. Hasil sync disimpan ke `lastValidTime` di RTC RAM dan sekaligus ke NVS flash (`last_time`).

2. **Kompensasi Deep Sleep (RTC RAM):** Sebelum masuk deep sleep, durasi tidur disimpan ke `sleepDurationSeconds` di RTC RAM. Saat bangun, `lastValidTime` dikompensasi dengan durasi tersebut sebelum digunakan, mengatasi `millis()` yang reset ke 0 setelah deep sleep.

3. **Recovery Reset Paksa (NVS Flash):** Jika RTC RAM hilang akibat power putus atau reset paksa, `lastValidTime` dipulihkan dari NVS flash. Waktu ini tidak tahu berapa lama power mati sehingga mungkin tidak presisi, namun mencegah kondisi `WAKTU INVALID` dan langsung dikoreksi begitu NTP berhasil.

```
Boot
├─ sleepDurationSeconds > 0? → lastValidTime += durasi sleep → simpan ke NVS → reset bootTime
└─ RTC RAM kosong? → baca lastValidTime dari NVS flash → set timeWasSynced = true
```

## Mekanisme RFID Local Database

1. **Download:** Saat boot (jika online dan SD tersedia), perangkat membandingkan versi database lokal (disimpan di NVS key `rfid_db_ver`) dengan versi di server via endpoint `/api/presensi/rfid-list/version`. Jika server lebih baru, download dilakukan.
2. **Format:** Server mengembalikan plain text. Baris pertama berformat `ver:{timestamp}`, baris berikutnya satu RFID per baris (10 digit angka). Total ukuran untuk 2000 RFID sekitar 22KB.
3. **Streaming Write:** Download ditulis langsung ke SD card per chunk tanpa memuat seluruh response ke heap. File ditulis ke `/rfid_db.tmp` lalu di-rename ke `/rfid_db.txt` setelah selesai untuk menghindari file korup jika download terputus.
4. **Validasi saat Tap:** Lookup dilakukan di RAM via `isRfidInCache()` — tanpa akses SD card dan tanpa HTTP call. Jika RFID tidak ditemukan di cache RAM, tap langsung ditolak dengan pesan `HUBUNGI ADMIN`.
5. **Fallback DB tidak ada:** Jika `rfid_db.txt` belum ada atau gagal dimuat ke RAM, `isRfidInCache()` mengembalikan `true` — semua tap diizinkan masuk ke queue, validasi diserahkan ke server saat sync.
6. **Siklus hidup cache RAM:** Cache dimuat saat boot setelah SD init, di-reload setelah download DB baru, di-reload setelah SD card kembali terbaca, dan di-free saat SD card terlepas.
7. **Pembaruan Berkala:** Setiap 3 jam, `checkAndUpdateRfidDb()` dipanggil dari loop. Cek versi dilakukan terlebih dahulu; download hanya dilakukan jika versi server lebih baru dari versi lokal.

## Mekanisme OTA Update

1. **Pemeriksaan:** Setiap 3 jam (dan langsung saat boot pertama), perangkat POST ke `/api/presensi/firmware/check` dengan versi firmware saat ini dan device ID.
2. **Perbandingan Versi:** Server membandingkan versi menggunakan `version_compare`. Jika versi server lebih baru, response menyertakan URL download.
3. **Notifikasi:** OLED menampilkan versi terbaru yang tersedia disertai bunyi notifikasi.
4. **Eksekusi:** Update dieksekusi di iterasi loop berikutnya, hanya jika tidak ada tap RFID aktif (`!rfidFeedback.active`).
5. **Download & Flash:** WDT dinonaktifkan, firmware diunduh via HTTP/HTTPS (sesuai build mode), dan di-flash ke partisi OTA.
6. **Hasil:** Sukses → restart otomatis dengan firmware baru. Gagal → `otaState.updateAvailable` di-reset, perangkat lanjut beroperasi normal.

## Dual Build Mode

Firmware mendukung dua mode build yang dikontrol lewat satu preprocessor define:

```cpp
// #define DEV_MODE  ← uncomment untuk development
```

| Aspek | DEV_MODE (aktif) | Production (default) |
| :---- | :--------------- | :------------------- |
| `API_BASE_URL` | `http://192.168.1.254:8000` | `https://zedlabs.id` |
| HTTP Client | `WiFiClient` (plain) | `WiFiClientSecure` (TLS) |
| OTA Client | `WiFiClient` | `WiFiClientSecure` |
| Logika lain | Tidak berubah | Tidak berubah |

---

## Alur Operasi

### Boot

```
Startup Animation
    └─ Recovery Waktu
        ├─ Bangun dari deep sleep → kompensasi lastValidTime dengan durasi sleep → simpan ke NVS
        └─ RTC RAM kosong (reset paksa) → pulihkan lastValidTime dari NVS flash
    └─ Init SD Card
        ├─ Ada SD  → Load metadata → tampil pending records → Load RFID cache ke RAM
        └─ Tidak ada SD → cek NVS buffer → tampil jika ada
    └─ Connect WiFi
        ├─ Berhasil → Ping API
        │   ├─ API OK → Sync NTP → Sync NVS buffer → Bulk sync SD queue
        │   │           → Cek versi RFID DB → Download jika ada update
        │   └─ API Gagal → Offline mode
        └─ Gagal → Offline mode
    └─ Init RFID
    └─ Sistem Siap
```

### Saat Kartu Di-tap

```
RFID terbaca
    └─ Ada SD card?
        ├─ Ya → isRfidInCache() — lookup di RAM, < 1ms
        │       ├─ Cache kosong/tidak ada → izinkan (fallback)
        │       ├─ Ditemukan → simpan ke queue SD ✓
        │       └─ Tidak ditemukan → tolak (HUBUNGI ADMIN) ✗
        └─ Tidak ada SD
            ├─ Online → kirimLangsung() ke API
            │   ├─ Berhasil → selesai
            │   └─ Gagal (timeout/down/lambat) → simpan ke NVS buffer
            └─ Offline → simpan ke NVS buffer

    Jika NVS penuh → tolak tap (BUFFER PENUH!)
```

### RFID DB Update Flow

```
Boot
    └─ initSDCard() selesai → loadRfidCacheFromFile()
        ├─ rfid_db.txt ada → muat ke RAM heap → tampil "X RFID"
        └─ rfid_db.txt tidak ada → cache kosong (fallback izinkan)

Boot / Loop (setiap 3 jam)
    └─ checkAndUpdateRfidDb()
        ├─ Tidak online atau tidak ada SD → skip
        ├─ checkRfidDbVersion() → ambil versi server
        │   ├─ Gagal atau versi sama → skip
        │   └─ Versi server lebih baru → downloadRfidDb()
        │       ├─ Streaming write ke /rfid_db.tmp
        │       ├─ Rename tmp → /rfid_db.txt
        │       ├─ Simpan versi baru ke NVS
        │       ├─ loadRfidCacheFromFile() → reload RAM cache
        │       └─ Tampil jumlah RFID + tone success

SD Card Terlepas → freeRfidCache() → cache dikosongkan
SD Card Kembali  → loadRfidCacheFromFile() → cache dimuat ulang
```

### OTA Update Flow

```
Loop (setiap 3 jam / boot pertama)
    └─ checkOtaUpdate()
        ├─ Tidak ada update → lanjut normal
        └─ Ada update → simpan ke otaState → tampil di OLED
            └─ Loop berikutnya (jika !rfidFeedback.active)
                └─ performOtaUpdate()
                    ├─ Sukses → ESP.restart() → boot dengan firmware baru
                    └─ Gagal  → reset otaState → lanjut normal
```

### State Machine Reconnect

```
RECONNECT_IDLE
    │ WiFi disconnect & interval tercapai
    ▼
RECONNECT_INIT ──► WiFi.begin()
    │
    ▼
RECONNECT_TRYING
    ├── Connected ──► RECONNECT_SUCCESS ──► Sync NVS + SD ──► IDLE
    └── Timeout   ──► RECONNECT_FAILED  ──► IDLE
```

### Timekeeping Recovery Flow

```
setup() dipanggil
    ├─ sleepDurationSeconds > 0?
    │   └─ Ya (bangun dari deep sleep)
    │       ├─ lastValidTime += sleepDurationSeconds
    │       ├─ nvsSaveLastTime(lastValidTime)
    │       ├─ bootTime = millis()
    │       └─ sleepDurationSeconds = 0
    └─ timeWasSynced == false || lastValidTime == 0?
        └─ Ya (reset paksa / power putus)
            ├─ saved = nvsLoadLastTime()
            └─ saved > 0?
                └─ Ya → lastValidTime = saved
                         timeWasSynced = true
                         bootTime = millis()
                         bootTimeSet = true
```

---

## OLED Auto Dim Schedule

| Waktu         | Status OLED | Keterangan                        |
| :------------ | :---------- | :-------------------------------- |
| 00:00 – 07:59 | ON          | Display aktif untuk presensi pagi |
| 08:00 – 13:59 | OFF         | Display mati untuk hemat daya     |
| 14:00 – 17:59 | ON          | Display aktif untuk presensi sore |
| 18:00 – 04:59 | SLEEP MODE  | Mesin deep sleep                  |

> Display tetap menyala sementara saat ada tapping RFID meskipun dalam periode dim.

---

## API Specification

### Endpoint Health Check
- **URL:** `/api/presensi/ping` — **Method:** `GET`

### Endpoint Kirim Langsung (Tanpa SD Card)
- **URL:** `/api/presensi` — **Method:** `POST`
- **Payload:**
  ```json
  {
    "rfid": "0012345678",
    "timestamp": "2026-03-10 07:30:00",
    "device_id": "ESP32_A1B2",
    "sync_mode": false
  }
  ```
- **Respons:** HTTP 200 (berhasil), HTTP 400 (duplikat), HTTP 404 (RFID tidak dikenali).

### Endpoint Sinkronisasi Bulk (SD Queue + NVS Buffer)
- **URL:** `/api/presensi/sync-bulk` — **Method:** `POST`
- **Payload:**
  ```json
  {
    "data": [
      {
        "rfid": "0012345678",
        "timestamp": "2026-03-10 07:30:00",
        "device_id": "ESP32_A1B2",
        "sync_mode": true
      }
    ]
  }
  ```
- **Respons:** HTTP 200. Record dengan `status: error` dicatat ke `failed_log.csv` (jika SD tersedia).

### Endpoint OTA Check
- **URL:** `/api/presensi/firmware/check` — **Method:** `POST`
- **Payload:**
  ```json
  {
    "version": "2.2.10",
    "device_id": "ESP32_A1B2"
  }
  ```
- **Respons:**
  ```json
  {
    "update": true,
    "version": "2.3.0",
    "url": "https://zedlabs.id/api/presensi/firmware/download/v2.3.0.bin",
    "changelog": "- Fitur baru\n- Perbaikan bug"
  }
  ```

### Endpoint OTA Download
- **URL:** `/api/presensi/firmware/download/{filename}` — **Method:** `GET`
- **Respons:** Binary stream (`application/octet-stream`). Dilindungi oleh middleware `api.secret`.

### Endpoint RFID List Version
- **URL:** `/api/presensi/rfid-list/version` — **Method:** `GET`
- **Respons:**
  ```json
  { "ver": 1741571400 }
  ```

### Endpoint RFID List
- **URL:** `/api/presensi/rfid-list` — **Method:** `GET`
- **Respons:** Plain text (`text/plain`). Baris pertama versi, sisanya satu RFID per baris.
  ```
  ver:1741571400
  0012345678
  0087654321
  0099887766
  ```

Semua request menggunakan header `X-API-KEY`.

---

## Struktur File SD Card

```
/
├── queue_0.csv        ← File antrean aktif
├── queue_1.csv
├── ...
├── queue_1999.csv
├── queue_meta.txt     ← Cache: pending count + indeks file aktif
├── rfid_db.txt        ← Database RFID valid (diunduh dari server)
└── failed_log.csv     ← Log record yang gagal disync
```

Format file antrean:
```
rfid,timestamp,device_id,unix_time
0012345678,2026-03-10 07:30:00,ESP32_A1B2,1741571400
```

Format `queue_meta.txt`:
```
1250,47
```
_(pending_count, current_queue_file_index)_

Format `rfid_db.txt`:
```
0012345678
0087654321
0099887766
```

---

## Dependensi Library

| Library | Versi yang diuji |
|---|---|
| MFRC522 | ≥ 1.4.10 |
| Adafruit SSD1306 | ≥ 2.5.7 |
| Adafruit GFX Library | ≥ 1.11.9 |
| ArduinoJson | ≥ 7.x |
| SdFat | ≥ 2.2.x |

Library bawaan ESP32 core (tidak perlu install terpisah): `WiFi`, `WiFiClientSecure`, `HTTPClient`, `HTTPUpdate`, `Wire`, `SPI`, `time`, `esp_task_wdt`, `Preferences`.

> **Catatan:** Gunakan ESP32 Arduino core v3.x. API `esp_task_wdt_init` pada core v3.x menggunakan struct `esp_task_wdt_config_t`.

---

## Technical Specifications

### Queue System

| Parameter             | Nilai           |
| :-------------------- | :-------------- |
| Max Records per File  | 25              |
| Max Queue Files       | 2.000           |
| Total Capacity        | 50.000 records  |
| Duplicate Check Range | 3 file terakhir |
| Sync Interval         | 300 detik       |
| Duplicate Interval    | 1.800 detik     |

### NVS Buffer

| Parameter        | Nilai              |
| :--------------- | :----------------- |
| Kapasitas        | 20 record          |
| Storage          | Flash internal NVS |
| Namespace        | `presensi`         |
| Persistensi      | Melewati restart & deep sleep |
| Perilaku penuh   | Tolak tap          |
| Sync priority    | Sebelum SD queue   |

### NVS Keys

| Key | Tipe | Fungsi |
| :-- | :--- | :----- |
| `nvs_count` | int | Jumlah record NVS buffer |
| `rec_N` | bytes | Record offline ke-N |
| `rfid_db_ver` | unsigned long | Versi database RFID lokal |
| `last_time` | unsigned long | Timestamp NTP terakhir valid (untuk recovery reset paksa) |

### Hybrid Timekeeping

| Kondisi | Mekanisme | Akurasi |
| :------ | :-------- | :------ |
| Online | NTP sync langsung | Presisi penuh |
| Offline setelah NTP sync | RTC RAM + estimasi `millis()` | Baik (drift kecil) |
| Bangun dari deep sleep | Kompensasi `sleepDurationSeconds` di RTC RAM | Baik |
| Reset paksa / power putus | Pulihkan dari NVS `last_time` | Tidak tahu durasi power mati, langsung dikoreksi saat NTP |

### RFID Local Database

| Parameter              | Nilai                                     |
| :--------------------- | :---------------------------------------- |
| File                   | `/rfid_db.txt`                            |
| Format                 | Plain text, satu RFID per baris           |
| Kapasitas              | Tidak terbatas (dibatasi ukuran SD)       |
| Ukuran estimasi 2000 RFID | ~22KB                                |
| Versi storage          | NVS key `rfid_db_ver` (unsigned long)     |
| Check interval         | 10.800 detik (3 jam)                      |
| Check saat boot        | Ya                                        |
| Download method        | Streaming chunk, tanpa heap buffer penuh  |
| Atomicity              | Tulis ke `.tmp` lalu rename               |
| RAM cache              | Array pointer `char**` di heap, dimuat saat boot |
| RAM usage (2000 RFID)  | ~22KB heap                                |
| Lookup method          | Scan linear di RAM, O(n), < 1ms           |
| Fallback jika tidak ada | Izinkan semua tap                        |
| Pesan tolak            | `HUBUNGI ADMIN`                           |

### OTA Update

| Parameter            | Nilai                          |
| :------------------- | :----------------------------- |
| Check Interval       | 10.800 detik (3 jam)           |
| Check saat boot      | Ya (langsung, `lastOtaCheck=0`) |
| Transport            | HTTPS (production) / HTTP (DEV_MODE) |
| WDT saat download    | Dinonaktifkan sementara        |
| Kondisi eksekusi     | Tidak ada tap aktif            |
| Perilaku gagal       | Reset state, lanjut normal     |

### WiFi Configuration

| Parameter          | Nilai                        |
| :----------------- | :--------------------------- |
| SSID Support       | 1                            |
| TX Power           | 19.5 dBm                     |
| Sleep Mode         | WIFI_PS_MAX_MODEM            |
| Sort Method        | WIFI_CONNECT_AP_BY_SIGNAL    |
| Reconnect States   | 4 (IDLE, INIT, TRYING, SUCCESS/FAILED) |
| Reconnect Interval | 300 detik                    |

### Reliability

| Parameter              | Nilai                    |
| :--------------------- | :----------------------- |
| Watchdog Timeout       | 60 detik                 |
| WDT Reset Coverage     | Setiap iterasi loop operasi panjang (scan file, baca baris, duplicate check, streaming RFID DB) |
| RFID Feedback          | Non-blocking             |
| String Handling        | Stack-based `char[]`     |
| Transport Security     | HTTPS production / HTTP DEV_MODE |
| Queue Overwrite Guard  | Enabled                  |
| Time Persistence       | RTC RAM + NVS flash (dual layer) |

### Performance Metrics

| Metrik                  | Nilai                              |
| :---------------------- | :--------------------------------- |
| Tap Latency (Ada SD, RFID valid di cache RAM)      | < 50ms (lookup RAM + tulis SD)           |
| Tap Latency (Ada SD, RFID tidak ada di cache RAM)  | < 50ms (lookup RAM, langsung tolak)      |
| Tap Latency (Tanpa SD, online, server OK) | < 10 detik              |
| Tap Latency (Tanpa SD, server down/lambat) | < 50ms (NVS)           |
| Tap Latency (Tanpa SD, offline) | < 50ms (NVS)                 |
| Background Sync         | Setiap 5 menit                     |
| RFID DB Update Check    | Setiap 3 jam                       |
| Queue Capacity          | 50.000 records (SD) + 20 (NVS)     |
| Power (Active)          | ~150mA                             |
| Power (Deep Sleep)      | < 5mA                              |

---

## Troubleshooting

**Masalah: Device restart saat boot setelah SD card terdeteksi**
Kemungkinan WDT trigger selama scan file. Pastikan menggunakan firmware v2.2.10 yang sudah menyertakan `esp_task_wdt_reset()` di setiap iterasi loop operasi panjang. Jika masih terjadi, naikkan `WDT_TIMEOUT_SEC` sementara ke 120 untuk diagnosis.

**Masalah: `esp_task_wdt_init` compilation error**
Pastikan menggunakan ESP32 Arduino core v3.x.

**Masalah: OLED tidak mati/menyala sesuai jadwal**
Pastikan waktu sistem sudah tersinkronisasi dengan NTP. Periksa nilai `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR`.

**Masalah: NVS buffer tidak terhapus setelah online**
Cek koneksi server. NVS hanya dihapus jika server merespons HTTP 200. Jika server menolak semua record, record akan tetap di NVS hingga berhasil dikirim.

**Masalah: Tap ditolak dengan pesan BUFFER PENUH!**
NVS buffer (20 record) penuh dan server belum bisa dihubungi. Pastikan koneksi WiFi dan server dalam kondisi baik. Data akan otomatis disync dan slot NVS dikosongkan saat server kembali online.

**Masalah: Tap ditolak dengan pesan HUBUNGI ADMIN**
RFID kartu tidak ditemukan di cache RAM. Pastikan kartu sudah didaftarkan di server dan database lokal sudah diperbarui. DB diperbarui otomatis setiap 3 jam, atau restart perangkat untuk memaksa download saat boot.

**Masalah: Waktu tidak akurat setelah bangun dari deep sleep**
Pastikan firmware v2.2.10 atau lebih baru digunakan. Versi ini menyimpan durasi sleep ke RTC RAM dan mengompensasinya ke `lastValidTime` saat bangun.

**Masalah: Waktu tidak akurat setelah reset paksa atau power putus**
Firmware v2.2.10 menyimpan `lastValidTime` ke NVS flash setiap NTP sync. Saat boot setelah reset paksa, waktu dipulihkan dari NVS. Waktu akan kurang presisi sampai NTP sync berhasil dilakukan kembali.

**Masalah: Perangkat menampilkan WAKTU INVALID setelah reset paksa tanpa internet**
Pastikan perangkat pernah melakukan NTP sync setidaknya sekali sebelumnya sehingga ada nilai tersimpan di NVS `last_time`. Jika belum pernah sync sama sekali, waktu memang tidak tersedia sampai koneksi internet kembali.

**Masalah: rfid_db.txt tidak terunduh meski online**
Cek endpoint `/api/presensi/rfid-list/version` dan `/api/presensi/rfid-list` dapat diakses dengan header `X-API-KEY` yang benar. Pastikan SD card tersedia dan tidak penuh.

**Masalah: Record tidak tersync meski online**
Cek `failed_log.csv` di SD card untuk melihat alasan penolakan dari server.

**Masalah: OTA update tidak berjalan meski ada versi baru**
Pastikan firmware aktif di panel Filament sudah di-toggle `is_active`. Cek juga koneksi WiFi dan header `X-API-KEY`. OTA hanya berjalan saat WiFi terhubung.

**Masalah: OTA update gagal dengan error code**
Error code ditampilkan di OLED (`ERR -xxx`). Pastikan URL download dapat diakses dan file `.bin` tidak korup. Perangkat akan lanjut beroperasi normal setelah gagal.

**Masalah: Device restart loop setelah OTA**
Kemungkinan file `.bin` korup atau tidak kompatibel dengan ESP32-C3. Upload ulang firmware yang benar melalui panel Filament dan aktifkan kembali.

**Masalah: SSL/TLS error saat development lokal**
Gunakan `DEV_MODE` dengan uncomment `#define DEV_MODE` di bagian atas firmware. Mode ini menggunakan `WiFiClient` plain tanpa SSL, kompatibel dengan `php artisan serve` atau server HTTP lokal lainnya.

---

## Changelog

### v2.2.10 (Maret 2026)
- Perbaikan bug waktu setelah bangun dari deep sleep: durasi sleep kini disimpan ke `sleepDurationSeconds` (RTC RAM) sebelum `esp_deep_sleep_start()` dan dikompensasikan ke `lastValidTime` saat `setup()`, mengatasi reset `millis()` yang menyebabkan estimasi waktu tidak akurat
- Tambah persistensi waktu ke NVS flash (`last_time`): `lastValidTime` disimpan ke NVS setiap kali NTP sync berhasil, sehingga waktu dapat dipulihkan setelah reset paksa atau power putus
- Tambah `nvsSaveLastTime()` dan `nvsLoadLastTime()` untuk baca/tulis NVS key `last_time`
- Tambah `NVS_KEY_LAST_TIME` ke konstanta NVS
- Tambah `sleepDurationSeconds` (RTC_DATA_ATTR uint64_t) ke RTC Memory
- Tambah blok recovery waktu di awal `setup()` sebelum `SPI.begin()` dengan dua mekanisme: kompensasi sleep dan recovery dari NVS
- Tambah `Dual Build Mode`: preprocessor `#define DEV_MODE` untuk switching HTTP/HTTPS tanpa mengubah logika; `getHttpClient()` menggantikan `getSecureClient()` di seluruh kodebase; `performOtaUpdate()` menggunakan conditional client
- URL API dikonfigurasi via `#ifdef DEV_MODE` secara terpisah dari `API_SECRET_KEY`
- Update versi string ke `2.2.10`

### v2.2.9 (Maret 2026)
- Tambah fitur RFID Local Database untuk validasi offline tanpa panggilan jaringan saat tap
- Tambah endpoint `/api/presensi/rfid-list` (plain text streaming) dan `/api/presensi/rfid-list/version` (JSON versi)
- Tambah fungsi `downloadRfidDb()` dengan streaming write ke SD untuk efisiensi heap
- Tambah fungsi `isRfidInCache()` untuk lookup lokal saat tap kartu
- Tambah fungsi `checkAndUpdateRfidDb()` dipanggil setiap 3 jam dan saat boot
- Versi database disimpan di NVS key `rfid_db_ver`; download hanya dilakukan jika versi server lebih baru
- Download menggunakan file sementara `/rfid_db.tmp` lalu di-rename untuk menjaga atomicity
- Tambah `lastRfidDbCheck` ke struct `Timers`
- Update versi string ke `2.2.9`

### v2.2.8 (Maret 2026)
- Tambah fitur OTA Update otomatis via endpoint `/api/presensi/firmware/check` dan `/firmware/download/{filename}`
- Tambah `OtaState` struct dan `lastOtaCheck` timer
- Pemeriksaan OTA langsung saat boot (`lastOtaCheck = 0`) dan setiap 3 jam
- WDT dinonaktifkan selama proses OTA download untuk mencegah false timeout
- Eksekusi OTA hanya saat tidak ada tap aktif (`!rfidFeedback.active`)
- Perbaikan WDT: tambah `esp_task_wdt_reset()` di setiap iterasi loop pada `countAllOfflineRecords`, `isDuplicateInternal`, `initSDCard`, `saveToQueue`, dan `readQueueFile`
- Perbaikan alur `kirimPresensi`: hapus pemanggilan `validateRfidOnline()` saat tanpa SD, langsung ke `kirimLangsung()`
- Perbaikan urutan sync di `RECONNECT_SUCCESS`: NVS buffer disync sebelum SD queue

### v2.2.7 (Maret 2026)
- Rilis awal sistem hybrid dengan Queue System + NVS Buffer
- Implementasi reconnect state machine 4 state
- Deep sleep scheduling dan OLED auto dim
- Bulk sync dengan chunked processing

---

## Lisensi

Hak Cipta 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
