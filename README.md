# Sistem Presensi Pintar Berbasis IoT (Hybrid Edition)
> Upgrade dari v2.2.5? Lihat bagian [Changelog v2.2.6](#riwayat-perubahan-changelog) untuk daftar lengkap perubahan.

**Attendance Machine** adalah solusi presensi cerdas berbasis _Internet of Things_ (IoT) yang dirancang untuk mengatasi tantangan infrastruktur jaringan yang tidak stabil. Dibangun di atas mikrokontroler ESP32-C3, sistem ini menerapkan arsitektur _Hybrid_ yang menggabungkan kemampuan pemrosesan daring (_online_) dan luring (_offline_) secara mulus.

Sistem ini beroperasi dengan filosofi _Self-Healing_ dan _Store-and-Forward_, menjamin integritas data kehadiran tanpa kehilangan (_zero data loss_) melalui mekanisme antrean terpartisi (_Partitioned Queue System_), sinkronisasi otomatis, dan **operasi latar belakang yang tidak mengganggu pengguna** (_Non-Intrusive Background Operations_).

---

## Spesifikasi

| Field | Value |
|---|---|
| Project | Madrasah Universe |
| Author | Yahya Zulfikri |
| Device | ESP32-C3 Super Mini |
| Versi | 2.2.6 |
| IDE | Arduino IDE v2.3.6 |
| Dibuat | Juli 2025 |
| Diperbarui | Maret 2026 |

---

## Arsitektur Sistem

Sistem dirancang sebagai gerbang fisik data kehadiran yang agnostik terhadap status konektivitas dengan prioritas pada responsivitas dan user experience.

### Mekanisme Operasional Utama

1. **Identifikasi:** Pengguna memindai kartu RFID pada perangkat.
2. **Validasi Lokal:** Perangkat melakukan verifikasi _debounce_ dan pengecekan duplikasi data dalam interval waktu tertentu (default: 30 menit) langsung pada penyimpanan lokal untuk mencegah input ganda. Pengecekan mencakup 3 file antrean terakhir untuk menutup celah di boundary file.
3. **Manajemen Penyimpanan (Queue System):** Data masuk ke sistem antrean berkas CSV (`queue_X.csv`), dipecah menjadi berkas-berkas kecil (maksimal 25 rekaman per berkas) untuk menjaga stabilitas memori RAM mikrokontroler. Metadata antrean (jumlah record dan indeks file aktif) disimpan terpisah di `queue_meta.txt` untuk efisiensi akses. Saat ada SD card, data **selalu** masuk ke queue terlebih dahulu tanpa validasi jaringan — validasi RFID dilakukan server saat proses sync bulk.
4. **Sinkronisasi Latar Belakang (_Background Sync_):** Sistem secara berkala (setiap 5 menit) memeriksa keberadaan berkas antrean. Jika koneksi internet tersedia, data dikirim secara _batch_ ke server **tanpa menampilkan proses atau mengganggu pengguna**. Berkas lokal dihapus secara otomatis hanya jika server memberikan respons sukses (HTTP 200).
5. **Reconnect Otomatis (_Silent Auto-Reconnect_):** Jika WiFi terputus, sistem mencoba reconnect setiap 5 menit secara otomatis di latar belakang. Setelah kembali online, sistem otomatis melakukan sync data offline yang tertunda.
6. **Manajemen OLED Cerdas:** Layar OLED otomatis mati pada jam tertentu untuk hemat daya dan memperpanjang umur display, namun tetap menyala sementara saat ada tapping kartu.
7. **Integrasi Hilir:** Server memproses data _batch_ untuk keperluan notifikasi WhatsApp, laporan digital, dan analisis kehadiran.

---

## Spesifikasi Teknis

### Perangkat Keras

Sistem ini menggunakan arsitektur bus SPI bersama (_Shared SPI Bus_) untuk efisiensi pin pada ESP32-C3.

| Komponen             | Spesifikasi              | Fungsi Utama                                                               |
| :------------------- | :----------------------- | :------------------------------------------------------------------------- |
| **Unit Pemroses**    | ESP32-C3 Super Mini      | Manajemen logika utama, konektivitas WiFi, dan sistem berkas.              |
| **Sensor Identitas** | RFID RC522 (13.56 MHz)   | Pembacaan UID kartu presensi (Protokol SPI).                               |
| **Penyimpanan**      | Modul MicroSD (SPI)      | Penyimpanan antrean data offline (CSV) dan log sistem.                     |
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

> **Penting:** Pastikan implementasi perangkat keras mendukung penggunaan GPIO 4, 5, dan 6 secara paralel untuk dua perangkat SPI berbeda.

---

## Fitur Perangkat Lunak (Firmware)

### Core Features

- **Offline-First Capability:** Prioritas penyimpanan data lokal saat jaringan tidak tersedia atau tidak stabil.
- **Partitioned Queue System:** Manajemen memori tingkat lanjut yang memecah penyimpanan data menjadi berkas-berkas kecil untuk mencegah _buffer overflow_ pada mikrokontroler.
- **Smart Duplicate Prevention:** Algoritma _sliding window_ yang memindai 3 indeks antrean lokal terakhir untuk menolak pemindaian kartu yang sama dalam periode waktu yang dikonfigurasi (default: 30 menit).
- **Bulk Upload Efficiency:** Mengoptimalkan penggunaan _bandwidth_ dengan mengirimkan himpunan data dalam satu permintaan HTTP POST.
- **Hybrid Timekeeping:** Sinkronisasi waktu presisi menggunakan NTP saat daring, dan estimasi waktu berbasis RTC internal saat luring.
- **Deep Sleep Scheduling:** Manajemen daya otomatis untuk menonaktifkan sistem di luar jam operasional (default: 18:00–05:00).

### Advanced Features (v2.2.2+)

- **Silent Background Sync:** Sinkronisasi data berjalan di latar belakang tanpa feedback visual atau audio yang mengganggu.
- **Non-Intrusive Reconnect:** Auto-reconnect WiFi tanpa menampilkan loading screen atau progress bar.
- **Zero-Interruption UX:** Pengguna dapat melakukan tapping RFID kapan saja tanpa terblokir oleh proses background.
- **Smart Display Management:** Layar OLED hanya menampilkan informasi penting (status, waktu, queue counter) tanpa distraksi proses teknis.
- **Legacy Storage Support:** Dukungan penuh untuk SD Card lama (128MB–256MB) menggunakan pustaka SdFat.

### WiFi Optimization Features (v2.2.3+)

- **Maximum TX Power (19.5 dBm):** Jangkauan sinyal WiFi yang lebih jauh dan penetrasi lebih baik melalui penghalang.
- **Modem Sleep Mode:** Efisiensi daya tanpa mengorbankan responsivitas jaringan.
- **Signal-Based AP Selection:** Koneksi otomatis ke Access Point dengan sinyal terkuat (`WIFI_CONNECT_AP_BY_SIGNAL`).
- **Persistent Connection:** Konfigurasi WiFi tersimpan di flash memory untuk boot yang lebih cepat.
- **RSSI Monitoring:** Tampilan kekuatan sinyal (dBm) saat startup untuk keperluan diagnostik.

### OLED Auto Dim (v2.2.4+)

- **Scheduled Display Control:** OLED otomatis mati pukul 08:00 dan nyala kembali pukul 14:00 (dapat dikonfigurasi).
- **Smart Wake-up on Tap:** Display menyala sementara saat ada kartu yang di-tap, lalu kembali mati sesuai jadwal.
- **Power Saving:** Mengurangi konsumsi daya display hingga 25% dan memperpanjang umur OLED 30–40%.
- **Seamless Operation:** Proses tapping RFID tetap berjalan normal meskipun display mati.

### Reliability & Security (v2.2.6)

- **Task Watchdog (WDT):** Pemulihan otomatis jika sistem hang lebih dari 60 detik, aktif selama operasi normal dan dinonaktifkan sebelum deep sleep.
- **HTTPS Enforcement:** Semua komunikasi API menggunakan `WiFiClientSecure` untuk enkripsi transport layer.
- **Non-Blocking RFID Feedback:** Feedback OLED pasca-tap tidak memblokir pembacaan kartu berikutnya — diimplementasikan via `RfidFeedback` struct tanpa `delay()`.
- **Queue-First Validation:** Saat ada SD card, data langsung masuk queue tanpa memanggil server. Validasi RFID dilakukan server saat proses sync bulk, sehingga tap kartu tidak pernah terhambat latensi jaringan.
- **Metadata-Driven Cache:** Pending count dan indeks file aktif disimpan di `queue_meta.txt`, menghindari scan penuh 2000 file setiap boot.
- **Heap Fragmentation Prevention:** Seluruh operasi string menggunakan `char[]` di stack, menggantikan Arduino `String` yang rawan fragmentasi heap pada operasi berulang.
- **Single SSID Simplification:** Konfigurasi jaringan disederhanakan menjadi satu SSID dengan state machine reconnect 4 state.

---

## Konfigurasi Sistem

Parameter operasional utama didefinisikan pada bagian awal kode sumber:

```cpp
// Konfigurasi Jaringan
const char WIFI_SSID[]       PROGMEM = "SSID_WIFI";
const char WIFI_PASSWORD[]   PROGMEM = "PasswordWifi";
const char API_BASE_URL[]    PROGMEM = "https://zedlabs.id";
const char API_SECRET_KEY[]  PROGMEM = "SecretAPIToken";
const char NTP_SERVER_1[]    PROGMEM = "pool.ntp.org";
const char NTP_SERVER_2[]    PROGMEM = "time.google.com";
const char NTP_SERVER_3[]    PROGMEM = "id.pool.ntp.org";
const long GMT_OFFSET_SEC    = 25200; // WIB (UTC+7)

// Konfigurasi Antrean
const int MAX_RECORDS_PER_FILE      = 25;       // Optimasi RAM untuk ESP32-C3
const int MAX_QUEUE_FILES           = 2000;     // Kapasitas total 50.000 record
const unsigned long SYNC_INTERVAL   = 300000;   // Sinkronisasi setiap 5 menit (background)

// Konfigurasi Validasi
const unsigned long MIN_REPEAT_INTERVAL = 1800; // Debounce 30 menit untuk kartu sama

// Konfigurasi Reconnect
const unsigned long RECONNECT_INTERVAL  = 300000;  // Auto-reconnect setiap 5 menit
const unsigned long TIME_SYNC_INTERVAL  = 3600000; // Time sync setiap 1 jam

// Konfigurasi Sleep Mode
const int SLEEP_START_HOUR = 18;  // Pukul 18:00 - Mesin Mati
const int SLEEP_END_HOUR   = 5;   // Pukul 05:00 - Mesin Nyala

// Konfigurasi OLED Auto Dim
const int OLED_DIM_START_HOUR = 8;  // Pukul 08:00 - OLED Mati
const int OLED_DIM_END_HOUR   = 14; // Pukul 14:00 - OLED Nyala

// Konfigurasi Display
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Update setiap 1 detik

// Konfigurasi Watchdog
const int WDT_TIMEOUT_SEC = 60; // Restart otomatis jika hang > 60 detik
```

---

## Mekanisme Antrean (Queue Logic)

1. **Segmentasi:** Data disimpan dalam berkas kecil (`queue_N.csv`) berisi maksimal 25 baris untuk mencegah _buffer overflow_.
2. **Rotasi dengan Proteksi:** Saat berkas `queue_N` penuh, sistem membuat `queue_N+1`. Jika file target masih berisi record yang belum ter-sync, rotasi dibatalkan untuk mencegah kehilangan data.
3. **Metadata Cache:** Jumlah pending record dan indeks file aktif disimpan di `queue_meta.txt`. Count di-increment langsung saat `saveToQueue` berhasil, tanpa perlu scan ulang seluruh SD card.
4. **Sliding Window Duplicate Check:** Memeriksa 3 berkas antrean terakhir untuk akurasi di boundary file, dengan tetap menjaga performa.
5. **Sinkronisasi Background:**
   - Sistem mencari berkas antrean yang ada secara sekuensial.
   - Data dibaca, diparsing ke JSON, dan dikirim ke server tanpa feedback visual.
   - Jika server merespons HTTP 200, berkas `queue_N` dihapus dari SD Card.
   - Jika gagal, berkas dipertahankan untuk percobaan berikutnya.
   - Proses ini **tidak mengganggu** operasi tapping RFID.

---

## Background Operations Architecture

### Filosofi Desain

Sistem menerapkan prinsip **"Invisible Infrastructure"** — semua operasi teknis (network, sync, reconnect) harus tidak terlihat oleh pengguna akhir. User experience fokus pada satu hal: **tap kartu → lihat feedback → selesai**.

### Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     USER INTERACTION                        │
│                                                             │
│  [Tap RFID Card] → [Save to Queue] → [Success Feedback]     │
│                                                             │
└─────────────────────────────────────────────────────────────┘
                              ▲
                              │ Non-blocking
                              │
┌─────────────────────────────────────────────────────────────┐
│                  BACKGROUND OPERATIONS                      │
│                                                             │
│  ┌─────────────────┐         ┌──────────────────┐           │
│  │ WiFi Reconnect  │◄────────┤  Every 5 minutes │           │
│  │   (Silent)      │         └──────────────────┘           │
│  └────────┬────────┘                                        │
│           │                                                 │
│           ▼                                                 │
│  ┌─────────────────┐         ┌──────────────────┐           │
│  │ Data Sync       │◄────────┤  Auto-triggered  │           │
│  │   (Silent)      │         │  after online    │           │
│  └─────────────────┘         └──────────────────┘           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### State Machine Reconnect (v2.2.6)

```
RECONNECT_IDLE
    │ WiFi disconnect & interval tercapai
    ▼
RECONNECT_INIT ──► WiFi.begin()
    │
    ▼
RECONNECT_TRYING
    ├── Connected ──► RECONNECT_SUCCESS ──► Sync time + data ──► IDLE
    └── Timeout   ──► RECONNECT_FAILED  ──► IDLE
```

---

## Alur Operasi

### Boot

```
Startup Animation
    └─ Init SD Card
        ├─ Ada SD  → Load metadata (queue_meta.txt) → tampil pending records
        └─ Tidak ada SD → lanjut tanpa queue
    └─ Connect WiFi
        ├─ Berhasil → Ping API
        │   ├─ API OK → Sync NTP → Bulk sync pending records
        │   └─ API Gagal → Offline mode
        └─ Gagal → Offline mode
    └─ Init RFID
    └─ Sistem Siap
```

### Saat Kartu Di-tap

```
RFID terbaca
    └─ Ada SD card?
        ├─ Ya → Langsung simpan ke queue SD (tanpa validasi jaringan)
        │       Server menolak RFID invalid saat sync → dicatat failed_log.csv
        └─ Tidak ada SD
            ├─ Online → Validasi RFID → Kirim langsung ke API
            └─ Offline → Tolak (NO SD & WIFI)
```

---

## OLED Auto Dim Schedule

| Waktu         | Status OLED | Keterangan                          |
| :------------ | :---------- | :---------------------------------- |
| 00:00 – 07:59 | ON          | Display aktif untuk presensi pagi   |
| 08:00 – 13:59 | OFF         | Display mati untuk hemat daya       |
| 14:00 – 17:59 | ON          | Display aktif untuk presensi sore   |
| 18:00 – 04:59 | SLEEP MODE  | Mesin deep sleep (termasuk display) |

> Display tetap menyala sementara saat ada tapping RFID meskipun dalam periode dim.

---

## API Specification

### Endpoint Health Check

- **URL:** `/api/presensi/ping`
- **Method:** `GET`
- **Header:** `X-API-KEY: [SECRET_KEY]`
- **Respons:** HTTP 200 jika server aktif.

### Endpoint Validasi RFID

- **URL:** `/api/presensi/validate`
- **Method:** `POST`
- **Header:** `Content-Type: application/json`, `X-API-KEY: [SECRET_KEY]`
- **Payload:** `{ "rfid": "0012345678" }`
- **Respons:** HTTP 200 (valid), HTTP 404 (tidak dikenali).

### Endpoint Kirim Langsung (Tanpa SD Card)

- **URL:** `/api/presensi`
- **Method:** `POST`
- **Header:** `Content-Type: application/json`, `X-API-KEY: [SECRET_KEY]`
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

### Endpoint Sinkronisasi Bulk

- **URL:** `/api/presensi/sync-bulk`
- **Method:** `POST`
- **Header:** `Content-Type: application/json`, `X-API-KEY: [SECRET_KEY]`
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
- **Respons:** HTTP 200. Server mengembalikan status per-record. Record dengan `status: error` dicatat ke `failed_log.csv`.

---

## Struktur File SD Card

```
/
├── queue_0.csv        ← File antrean aktif
├── queue_1.csv
├── ...
├── queue_1999.csv     ← Maksimum 2000 file
├── queue_meta.txt     ← Cache: pending count + indeks file aktif
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

---

## Prasyarat Instalasi Perangkat Lunak

Install pustaka berikut melalui Arduino Library Manager:

| Library | Versi yang diuji |
|---|---|
| MFRC522 | ≥ 1.4.10 |
| Adafruit SSD1306 | ≥ 2.5.7 |
| Adafruit GFX Library | ≥ 1.11.9 |
| ArduinoJson | ≥ 7.x |
| SdFat | ≥ 2.2.x |

Library bawaan ESP32 core (tidak perlu install terpisah): `WiFi`, `WiFiClientSecure`, `HTTPClient`, `Wire`, `SPI`, `time`, `esp_task_wdt`.

> **Catatan:** Gunakan ESP32 Arduino core v3.x. API `esp_task_wdt_init` pada core v3.x menggunakan struct `esp_task_wdt_config_t`, berbeda dari core v2.x.

---

## Kompatibilitas Kartu SD

Berkat implementasi `SdFat`, sistem mendukung berbagai jenis kartu memori:

- **SDSC** (Standard Capacity) ≤ 2GB, termasuk kartu lama 128MB/256MB.
- **SDHC** (High Capacity) 4GB – 32GB.
- Format sistem berkas: **FAT16** dan **FAT32**.

---

## Technical Specifications

### Queue System

| Parameter             | Nilai          |
| :-------------------- | :------------- |
| Max Records per File  | 25             |
| Max Queue Files       | 2.000          |
| Total Capacity        | 50.000 records |
| Duplicate Check Range | 3 file terakhir |
| Sync Interval         | 300 detik      |
| Duplicate Interval    | 1.800 detik    |

### WiFi Configuration

| Parameter          | Nilai                      |
| :----------------- | :------------------------- |
| TX Power           | 19.5 dBm (maksimal)        |
| Sleep Mode         | WIFI_PS_MAX_MODEM          |
| Sort Method        | WIFI_CONNECT_AP_BY_SIGNAL  |
| Persistent         | Enabled                    |
| Auto-Reconnect     | Enabled                    |
| Reconnect Interval | 300 detik (5 menit)        |
| Reconnect States   | 4 (IDLE, INIT, TRYING, SUCCESS/FAILED) |

### Display & Power

| Parameter              | Nilai                     |
| :--------------------- | :------------------------ |
| Display Update         | 1.000 ms (1 detik)        |
| RSSI Bars              | 4 levels                  |
| OLED Auto Dim          | 08:00 – 14:00 (default)   |
| Deep Sleep             | 18:00 – 05:00 (default)   |
| Modem Sleep            | WIFI_PS_MAX_MODEM         |

### Reliability

| Parameter              | Nilai                     |
| :--------------------- | :------------------------ |
| Watchdog Timeout       | 60 detik                  |
| RFID Feedback          | Non-blocking              |
| String Handling        | Stack-based `char[]`      |
| Transport Security     | HTTPS (WiFiClientSecure)  |
| Queue Overwrite Guard  | Enabled                   |

### RSSI Interpretation

| RSSI          | Kualitas         |
| :------------ | :--------------- |
| ≥ -67 dBm     | Kuat (4 bar)     |
| -70 dBm       | Baik (3 bar)     |
| -80 dBm       | Cukup (2 bar)    |
| -90 dBm       | Lemah (1 bar)    |
| < -90 dBm     | Sangat Lemah     |

### Performance Metrics

| Metrik                     | Nilai                      |
| :------------------------- | :------------------------- |
| Tap-to-Feedback Latency    | < 200ms                    |
| Background Sync Frequency  | Setiap 5 menit             |
| Auto-Reconnect Interval    | Setiap 5 menit             |
| Queue Capacity             | 50.000 records             |
| Time Sync Accuracy         | ±1 detik (NTP)             |
| Power (Active)             | ~150mA                     |
| Power (Deep Sleep)         | < 5mA                      |

---

## Troubleshooting

**Masalah: Device restart saat boot setelah SD card terdeteksi**
Kemungkinan WDT trigger selama scan file. Pastikan `esp_task_wdt_reset()` dipanggil di dalam loop `countAllOfflineRecords()` dan `initSDCard()`. Naikkan `WDT_TIMEOUT_SEC` sementara ke 120 untuk diagnosis.

**Masalah: WiFi sering disconnect**
Pastikan RSSI di atas -70 dBm. Periksa interferensi pada frekuensi 2.4 GHz dan stabilitas power supply (minimal 5V 1A).

**Masalah: OLED tidak mati/menyala sesuai jadwal**
Pastikan waktu sistem sudah tersinkronisasi dengan NTP server. Periksa nilai `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR` di konfigurasi.

**Masalah: Record tidak tersync meski online**
Cek `failed_log.csv` di SD card. Jika ada entri di sana, kemungkinan record ditolak server karena RFID tidak terdaftar atau data kadaluarsa (> 30 hari).

**Masalah: `esp_task_wdt_init` compilation error**
Pastikan menggunakan ESP32 Arduino core v3.x. Core v2.x menggunakan API lama `esp_task_wdt_init(timeout, panic)`.

---

## Struktur Repositori

```text
.
├── firmware/
│   ├── v1.0.0/     # Versi Awal (Online Only)
│   ├── v2.0.0/     # Versi Hibrida Awal (Single CSV)
│   ├── v2.1.0/     # Queue System Base
│   ├── v2.2.0/     # Ultimate: SdFat, Legacy Card Support
│   ├── v2.2.1/     # Auto-Reconnect & Bug Fixes
│   ├── v2.2.2/     # Background Operations & Enhanced UX
│   ├── v2.2.3/     # WiFi Signal Optimization
│   ├── v2.2.4/     # OLED Auto Dim
│   ├── v2.2.5/     # Stability & Multi-SSID
│   └── v2.2.6/     # Reliability, Security & Non-Blocking UX
└── LICENSE
```

---

## Riwayat Perubahan (Changelog)

### v2.2.6 (Maret 2026) — Reliability, Security & Non-Blocking UX
- **Non-Blocking RFID Feedback:** Tap kartu tidak pernah terblokir meski pesan masih tampil di OLED. Diimplementasikan via `RfidFeedback` struct tanpa `delay()` di handler.
- **Heap Fragmentation Prevention:** Seluruh operasi string dimigrasi dari Arduino `String` ke `char[]` di stack.
- **Metadata-Driven Queue Cache:** `queue_meta.txt` menyimpan pending count dan indeks file aktif. Count di-increment manual, `countAllOfflineRecords()` hanya dipanggil saat benar-benar dibutuhkan.
- **Queue Overwrite Protection:** Rotasi file antrean tidak menimpa file yang masih berisi record belum ter-sync.
- **Enhanced Duplicate Check:** Cakupan diperluas dari 2 menjadi 3 file terakhir untuk menutup celah di boundary file.
- **Task Watchdog (WDT):** Pemulihan otomatis 60 detik dengan `esp_task_wdt_config_t` (kompatibel ESP32 core v3.x).
- **HTTPS Enforcement:** Semua request API menggunakan `WiFiClientSecure`.
- **Single SSID:** Konfigurasi jaringan disederhanakan dari 2 SSID menjadi 1 SSID dengan reconnect state machine 4 state.
- **RTC Queue File Validation:** Flag `rtcQueueFileValid` mencegah konflik antara nilai RTC dan metadata SD saat boot setelah cabut-pasang SD card.
- **DynamicJsonDocument:** Menggantikan `StaticJsonDocument<4096>` di `syncQueueFile()` untuk menghindari stack pressure.
- **Struct Encapsulation:** Variabel global dikelompokkan dalam `struct Timers`, `struct SyncState`, `struct DisplayState`, `struct OfflineRecord`, dan `struct RfidFeedback`.
- **pingAPI Timeout:** Dikurangi dari 10s/5s menjadi 5s/3s untuk boot yang lebih responsif.
- **PROGMEM Local Variable Fix:** `showStartupAnimation()` menggunakan `static const char[]` yang benar untuk ESP32.

### v2.2.5 (Maret 2026) — Stability & Multi-SSID
- Dual SSID Failover: reconnect otomatis ke SSID sekunder jika SSID utama gagal, dengan state machine 7 state.
- Perbaikan stabilitas operasi jangka panjang dari v2.2.4.
- Peningkatan akurasi duplicate check dan penanganan edge case timestamp.

### v2.2.4 (Desember 2025) — OLED Auto Dim
- Scheduled Display Control: OLED otomatis mati pukul 08:00, nyala pukul 14:00.
- Smart Wake-up: Display menyala sementara saat ada kartu yang di-tap.
- Implementasi flag `oledIsOn` untuk tracking status display.

### v2.2.3 (Desember 2025) — WiFi Signal Optimization
- Maximum TX Power 19.5 dBm, Modem Sleep Mode, Signal-Based AP Selection.
- Persistent WiFi dan RSSI display saat startup.

### v2.2.2 (Desember 2025) — Background Operations
- Background WiFi Reconnect dan Bulk Sync tanpa tampilan visual.
- Enhanced UX: display fokus pada informasi penting.

### v2.2.1 (Desember 2025) — Stability & Auto-Reconnect
- Penambahan fitur autoconnect WiFi dan perbaikan bug v2.2.0.

### v2.2.0 (Desember 2025) — Ultimate Edition
- Migrasi ke `SdFat`, dukungan kartu legacy, optimasi buffer memori.

### v2.1.0 — Queue System Foundation
- Pengenalan sistem antrean multi-file dan sliding window duplicate check.

### v2.0.0 — Hybrid Architecture
- Implementasi mode offline dasar (single CSV).

### v1.0.0 — Initial Release
- Rilis awal (Online only).

---

## Peta Jalan Pengembangan (Roadmap)

1. **Versi 1.x** — Presensi Daring Dasar.
2. **Versi 2.x** — Sistem Hibrida, Queue Offline Terpartisi, Sinkronisasi Massal, Auto Reconnect, Legacy Storage, Background Operations, WiFi Optimization, OLED Auto Dim, Reliability & Security.
3. **Versi 3.x** — Integrasi Biometrik (Sidik Jari) sebagai metode autentikasi sekunder.
4. **Versi 4.x** — Ekspansi fungsi menjadi Buku Tamu Digital dan Integrasi PPDB.
5. **Versi 5.x** — Ekosistem Perpustakaan Pintar (Sirkulasi Mandiri).
6. **Versi 6.x** — IoT Gateway & LoRaWAN untuk area tanpa jangkauan seluler/WiFi.

---

## Best Practices

1. **Penempatan Perangkat:** Letakkan pada lokasi dengan sinyal WiFi yang baik (RSSI di atas -70 dBm). Gunakan RSSI display saat startup untuk monitoring.
2. **Power Supply:** Gunakan power supply stabil 5V 1A minimum untuk menghindari brownout saat transmisi WiFi daya maksimal.
3. **Single SSID Multi-AP:** Jika deployment menggunakan beberapa Access Point, pastikan semua menggunakan SSID dan password yang sama agar sistem dapat memilih AP terbaik otomatis.
4. **Background Process:** Biarkan background sync dan reconnect berjalan otomatis. Tidak perlu intervensi manual.
5. **Jadwal OLED:** Sesuaikan `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR` dengan pola aktivitas presensi di lokasi deployment.
6. **SD Card Health:** Gunakan SD card berkualitas baik dan format ulang secara berkala jika `failed_log.csv` menunjukkan banyak error sistem.

---

## Lisensi

Hak Cipta 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
