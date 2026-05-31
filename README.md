# Sistem Presensi Pintar Berbasis IoT
### Hybrid Edition — v2.3.0

**Attendance Machine** adalah solusi presensi cerdas berbasis _Internet of Things_ (IoT) yang dirancang untuk mengatasi tantangan infrastruktur jaringan yang tidak stabil. Dibangun di atas mikrokontroler ESP32-C3, sistem ini menerapkan arsitektur _Hybrid_ yang menggabungkan kemampuan pemrosesan daring (_online_) dan luring (_offline_) secara mulus.

---
![provisioning](v2.3.0/provisioning.png)
---

## Spesifikasi Proyek

| Field | Value |
|---|---|
| Project | Madrasah Universe |
| Author | Yahya Zulfikri |
| Device | ESP32-C3 Super Mini |
| Versi | **2.3.0** |
| IDE | Arduino IDE v2.3.6 |
| Dibuat | Juli 2025 |
| Diperbarui | Mei 2026 |

---

## Fitur Utama

Sistem beroperasi dengan filosofi _Self-Healing_ dan _Store-and-Forward_, menjamin integritas data kehadiran tanpa kehilangan (_zero data loss_) melalui:

- **Offline-First Capability** — Data tersimpan lokal saat jaringan tidak tersedia.
- **Partitioned Queue System** — Antrean CSV terpecah mencegah _buffer overflow_ (kapasitas 50.000 record).
- **NVS Buffer** — Fallback flash internal ESP32 untuk kondisi tanpa SD card (40 record, persisten melewati restart & deep sleep).
- **Local RFID Database** — Validasi RFID di RAM tanpa HTTP call, latency tap < 50ms.
- **Smart Duplicate Prevention** — Algoritma _sliding window_ mencegah duplikasi dalam 30 menit.
- **Silent Background Sync** — Sinkronisasi data batch setiap 5 menit tanpa feedback visual.
- **OTA Update Otomatis** — Pembaruan firmware jarak jauh via HTTPS setiap 3 jam.
- **Non-Intrusive Reconnect** — Auto-reconnect WiFi tanpa loading screen, mendukung 3 SSID.
- **Deep Sleep Scheduling** — Manajemen daya otomatis di luar jam operasional (default: 18:00–05:00).
- **OLED Auto Dim** — Layar mati otomatis pada jam 08:00–12:00 untuk hemat daya.
- **Provisioning Mode** — Konfigurasi perangkat via captive portal WiFi tanpa perlu upload ulang firmware.
- **Admin RFID** — Kartu RFID khusus untuk memicu sync manual dan melihat status perangkat.
- **Telemetry Heartbeat** — Laporan status perangkat ke server setiap 5 menit.
- **Remote Config** — Konfigurasi jadwal dan interval diambil dari server setiap 10 menit.
- **Factory Reset** — Reset via tombol BOOT (tahan 5 detik).
- **CRC8 Integrity Check** — Setiap record antrian diberi checksum untuk validasi integritas data.
- **Encrypted Credentials** — API key dan kredensial WiFi dienkripsi AES-128-CBC di NVS menggunakan kunci turunan dari eFuse MAC.

---

## Perangkat Keras

| Komponen | Spesifikasi | Fungsi |
|:---|:---|:---|
| **Unit Pemroses** | ESP32-C3 Super Mini | Logika utama, WiFi, sistem berkas |
| **Sensor Identitas** | RFID RC522 (13.56 MHz) | Baca UID kartu (SPI) |
| **Penyimpanan** | Modul MicroSD (SPI) | Queue CSV, RFID DB, log (Opsional) |
| **Antarmuka Visual** | OLED 0.96" SSD1306 | Status koneksi, jam, queue counter |
| **Indikator Audio** | Buzzer Aktif 5V | Feedback sukses, gagal, notifikasi |
| **Catu Daya** | 5V USB / 3.7V Li-ion | Sumber daya operasional |

### Pinout ESP32-C3

| Komponen | Pin Modul | GPIO | Protokol |
|:---|:---|:---|:---|
| Bus SPI | SCK | 4 | SPI (Shared) |
| | MOSI | 6 | SPI (Shared) |
| | MISO | 5 | SPI (Shared) |
| RFID RC522 | SDA (SS) | 7 | SPI |
| | RST | 3 | Digital |
| SD Card | CS | 1 | SPI |
| OLED | SDA | 8 | I2C |
| | SCL | 9 | I2C |
| Buzzer | (+) | 10 | PWM |
| Factory Reset | BOOT | 9 | Digital Input |

---

## Arsitektur & Alur Operasi

### Boot

```
Startup Animation
    └─ Init SD Card
        ├─ Ada SD  → Load metadata → Load RFID cache ke RAM → Load Admin RFID
        └─ Tidak ada SD → cek NVS buffer → tampil jika ada
    └─ Provisioning check
        └─ Belum dikonfigurasi → Captive Portal WiFi
    └─ Load credentials (terenkripsi dari NVS)
    └─ Connect WiFi (3 SSID, fallback)
        ├─ Berhasil → Sync NTP → Ping API
        │   ├─ API OK → Sync NVS buffer → Bulk sync SD queue
        │   │           → Cek & update RFID DB
        │   └─ API Gagal → Offline mode
        └─ Gagal → Offline mode
    └─ Init RFID RC522
    └─ Sistem Siap → Jalankan FreeRTOS Tasks
```

### Saat Kartu Di-tap

```
RFID terbaca
    └─ Admin RFID? → handleAdminScan() → tampil status + trigger sync
    └─ Ada SD card?
        ├─ Ya → isRfidInCache() [lookup RAM < 1ms]
        │       ├─ Cache kosong → izinkan (fallback)
        │       ├─ Ditemukan → cek NVS recent scan → saveToQueue() ✓
        │       └─ Tidak ditemukan → tolak (RFID NONAKTIF) ✗
        └─ Tidak ada SD
            ├─ Online → kirimLangsung() ke API
            │   ├─ Berhasil → selesai ✓
            │   └─ Gagal → simpan ke NVS buffer
            └─ Offline → simpan ke NVS buffer
    └─ NVS penuh → tolak tap (BUFFER PENUH!)
```

### State Machine Reconnect (3 SSID)

```
RECONNECT_IDLE
    │ WiFi disconnect & interval tercapai (60 detik)
    ▼
RECONNECT_INIT ──► WiFi.begin(SSID berikutnya)
    │
    ▼
RECONNECT_TRYING (timeout 20 detik)
    ├── Connected ──► RECONNECT_SUCCESS ──► Sync NVS + SD ──► IDLE
    └── Timeout   ──► RECONNECT_FAILED  ──► IDLE
```

---

## Konfigurasi Sistem

Konfigurasi dilakukan via **Provisioning Mode** (captive portal) saat pertama kali boot atau setelah factory reset. Tidak perlu mengubah kode sumber.

| Parameter | Default | Keterangan |
|:---|:---|:---|
| Sleep Start | 18:00 | Jam mulai deep sleep |
| Sleep End | 05:00 | Jam selesai deep sleep |
| OLED Dim Start | 08:00 | Jam layar mati |
| OLED Dim End | 12:00 | Jam layar menyala kembali |
| Sync Interval | 5 menit | Interval sinkronisasi background |
| OTA Check | 3 jam | Interval cek firmware baru |
| RFID DB Check | 3 jam | Interval cek database RFID |
| Reconnect | 60 detik | Interval percobaan reconnect WiFi |
| Duplicate Window | 30 menit | Interval pengecekan duplikasi tap |
| WDT Timeout | 90 detik (normal) / 180 detik (sync) | Watchdog timeout |

### Provisioning Mode

Aktif otomatis jika perangkat belum dikonfigurasi atau setelah factory reset.

| Parameter | Value |
|---|---|
| AP SSID | `ATTENDANCE MACHINE` |
| AP Password | `P@ssw0rd` |
| Timeout | 5 menit |

Hubungkan ke AP tersebut, buka browser, dan isi form konfigurasi (WiFi, API URL, API Key, nama perangkat, jadwal sleep & dim).

### Factory Reset

Tahan tombol **BOOT** selama **5 detik** saat perangkat menyala. Semua konfigurasi NVS, RFID DB, metadata antrian, dan failed log akan dihapus. Perangkat akan masuk Provisioning Mode setelah restart.

---

## OLED Auto Dim Schedule

| Waktu | Status OLED | Keterangan |
|:---|:---|:---|
| 00:00 – 07:59 | ON | Display aktif |
| 08:00 – 11:59 | OFF | Display mati (hemat daya) |
| 12:00 – 17:59 | ON | Display aktif |
| 18:00 – 04:59 | SLEEP MODE | Deep sleep |

> Display tetap menyala sementara saat ada tap RFID meski dalam periode dim.

---

## API Specification

Semua request menggunakan header `X-API-KEY`.

| Endpoint | Method | Fungsi |
|:---|:---|:---|
| `/api/presensi/ping` | GET | Health check |
| `/api/presensi` | POST | Kirim langsung (tanpa SD) |
| `/api/presensi/sync-bulk` | POST | Sinkronisasi batch (SD queue + NVS) |
| `/api/presensi/firmware/check` | POST | Cek ketersediaan OTA |
| `/api/presensi/firmware/download/{file}` | GET | Download firmware binary |
| `/api/presensi/rfid-list/version` | GET | Cek versi database RFID |
| `/api/presensi/rfid-list` | GET | Download database RFID |
| `/api/presensi/heartbeat` | POST | Telemetry perangkat |
| `/api/presensi/config` | GET | Remote config |

---

## Struktur File SD Card

```
/
├── queue_0.csv        ← File antrian aktif (maks. 25 baris/file)
├── queue_1.csv
├── ...
├── queue_59999.csv    ← Total kapasitas: 60.000 file × 25 = 1.500.000 record
├── queue_meta.txt     ← Cache: pending count + indeks file aktif
├── rfid_db.txt        ← Database RFID valid (diunduh dari server)
├── admin_rfid.txt     ← Daftar RFID admin (maks. 5 kartu)
└── failed_log.csv     ← Log record yang ditolak server (maks. 500 baris)
```

Format record antrian:
```
rfid,timestamp,device_id,unix_time,crc8
0012345678,2026-05-31 07:30:00,GERBANG UTAMA,1748652600,A3
```

---

## Spesifikasi Teknis

### Queue System

| Parameter | Nilai |
|:---|:---|
| Max Records per File | 25 |
| Max Queue Files | 60.000 |
| Total Kapasitas | 1.500.000 record |
| Duplicate Check Range | 3 file terakhir |
| Overwrite Protection | Aktif |
| Integritas Data | CRC8 per record |

### NVS Buffer

| Parameter | Nilai |
|:---|:---|
| Kapasitas | 40 record |
| Storage | Flash internal (namespace `presensi`) |
| Persistensi | Melewati restart & deep sleep |
| Perilaku penuh | Tolak tap |
| Sync priority | Sebelum SD queue |

### RFID Local Database

| Parameter | Nilai |
|:---|:---|
| File | `/rfid_db.txt` |
| Kapasitas cache RAM | 5.000 RFID |
| Lookup method | Scan linear di RAM, O(n) |
| Lookup latency | < 1ms |
| Fallback jika tidak ada | Izinkan semua tap |
| Pesan tolak | `RFID NONAKTIF` |
| Atomicity download | Tulis ke `.tmp` → rename |

### Performance

| Metrik | Nilai |
|:---|:---|
| Tap latency (SD + RFID valid) | < 50ms |
| Tap latency (SD + RFID tidak ada di cache) | < 50ms |
| Tap latency (tanpa SD, server OK) | < 10 detik |
| Tap latency (tanpa SD, server down/offline) | < 50ms (NVS) |
| Power (Active) | ~150mA |
| Power (Deep Sleep) | < 5mA |

---

## Dependensi Library

| Library | Versi |
|---|---|
| MFRC522 | ≥ 1.4.10 |
| Adafruit SSD1306 | ≥ 2.5.7 |
| Adafruit GFX Library | ≥ 1.11.9 |
| ArduinoJson | ≥ 7.x |
| SdFat | ≥ 2.2.x |

Library bawaan ESP32 core (tidak perlu install terpisah): `WiFi`, `WiFiClientSecure`, `HTTPClient`, `HTTPUpdate`, `Wire`, `SPI`, `time`, `WebServer`, `DNSServer`, `Update`, `esp_task_wdt`, `Preferences`, `mbedtls/aes`, `mbedtls/md`, `freertos/*`.

> **Catatan:** Gunakan ESP32 Arduino core **v3.x**. API `esp_task_wdt_init` pada core v3.x menggunakan struct `esp_task_wdt_config_t`.

---

## Troubleshooting

| Masalah | Kemungkinan Penyebab & Solusi |
|:---|:---|
| Device restart saat boot setelah SD terdeteksi | WDT trigger saat scan file. Pastikan firmware v2.3.0. |
| `esp_task_wdt_init` compilation error | Pastikan ESP32 Arduino core v3.x. |
| OLED tidak mati/menyala sesuai jadwal | Pastikan NTP sudah tersync. Cek nilai dim schedule di provisioning. |
| NVS buffer tidak terhapus setelah online | NVS hanya dihapus jika server merespons HTTP 200. Cek koneksi server. |
| Tap ditolak `BUFFER PENUH!` | NVS buffer (40 record) penuh. Pastikan WiFi dan server online. |
| Tap ditolak `RFID NONAKTIF` | RFID tidak ada di cache RAM. Daftarkan di server; DB diperbarui otomatis setiap 3 jam atau restart untuk force download. |
| `rfid_db.txt` tidak terunduh | Cek endpoint `/api/presensi/rfid-list` dan header `X-API-KEY`. Pastikan SD tersedia. |
| Record tidak tersync meski online | Cek `failed_log.csv` untuk alasan penolakan server. |
| OTA tidak berjalan | Pastikan firmware aktif di panel server. Cek koneksi WiFi dan API key. |
| OTA gagal dengan error code | Error ditampilkan di OLED (`ERR -xxx`). Cek file `.bin` dan URL download. |
| Device restart loop setelah OTA | File `.bin` korup atau tidak kompatibel. Upload ulang firmware yang benar. |
| Waktu tidak akurat setelah power putus | Firmware menyimpan waktu ke NVS. Pastikan NVS namespace `presensi` tidak penuh. |
| Perangkat tidak mau provisioning | Tahan tombol BOOT 5 detik untuk factory reset, lalu hubungkan ke AP `ATTENDANCE MACHINE`. |

---

## Changelog

### v2.3.0 (Mei 2026)
- Tambah **Provisioning Mode** via captive portal WiFi (AP `ATTENDANCE MACHINE`) — konfigurasi tanpa upload ulang firmware
- Tambah dukungan **3 SSID** WiFi dengan failover otomatis saat reconnect
- Tambah **Admin RFID** — kartu khusus untuk memicu sync manual dan melihat status via `/admin_rfid.txt`
- Tambah **Telemetry Heartbeat** ke endpoint `/api/presensi/heartbeat` setiap 5 menit
- Tambah **Remote Config** dari endpoint `/api/presensi/config` setiap 10 menit (jadwal sleep/dim, interval sync & OTA)
- Tambah **Factory Reset** via tombol BOOT (tahan 5 detik)
- Tambah **CRC8 integrity check** pada setiap record antrian SD
- Tambah **AES-128-CBC encryption** untuk kredensial WiFi dan API key di NVS (kunci turunan dari eFuse MAC)
- Tambah **FreeRTOS multi-task**: `taskRfid`, `taskSync`, `taskDisplay` berjalan paralel di core terpisah
- Tambah **Semaphore SD Mutex** (`xSdMutex`) dan **Display Mutex** (`xDisplayMutex`) untuk akses aman antar task
- Tambah **RFID Queue** (`xRfidQueue`, panjang 8) antara loop RFID dan task pemrosesan
- Perluas kapasitas **NVS Buffer** dari 20 menjadi **40 record**
- Perluas kapasitas **Queue Files** dari 2.000 menjadi **60.000 file** (total 1.500.000 record)
- Perluas kapasitas **RFID Cache RAM** dari 2.000 menjadi **5.000 RFID**
- Tambah **RFID Cache flat array** (`rfidCacheFlat[5000][11]`) menggantikan array pointer heap
- Tambah pengecekan **kualitas sinyal WiFi** (`SIGNAL_THRESHOLD_WEAK`, `SIGNAL_THRESHOLD_CRITICAL`) sebagai guard semua operasi jaringan
- Tambah **NVS last scan persistence** (`nvsSaveLastScan` / `nvsIsRecentScan`) sebagai lapisan duplicate check tambahan di luar SD queue
- Tambah **scan count harian** (`nvsBumpScanCount`, `nvsGetScanCount`) untuk telemetry
- Pindahkan `OTA_CHECK_INTERVAL` dan `RFID_DB_CHECK_INTERVAL` ke `#define` terpisah; runtime dapat di-override via remote config
- Tambah **WDT dua mode**: `WDT_NORMAL_TIMEOUT_MS` (90 detik) dan `WDT_SYNC_TIMEOUT_MS` (180 detik) dengan fungsi `extendWdtForSync()` / `restoreWdtNormal()`
- Tambah **`sleepRequested` flag** untuk koordinasi graceful sleep antar FreeRTOS task
- Tambah **`QUEUE_WARN_THRESHOLD`** (48.000 file): pesan `QUEUE HAMPIR PENUH!` ditampilkan saat mendekati kapasitas
- Perbaikan: sync gap boundary file menggunakan pengecekan NVS last scan selain sliding window SD
- Perbaikan: `MAX_SYNC_FILES_PER_CYCLE` tetap 5 untuk menjaga responsivitas tap selama sync
- Update versi string ke `2.3.0`

### v2.2.11 (Maret 2026)
- Kembalikan jadwal sleep ke `SLEEP_START_HOUR 18`
- Kembalikan `OTA_CHECK_INTERVAL` ke 3 jam

### v2.2.10 (Maret 2026)
- Tambah persistensi waktu ke NVS untuk ketahanan terhadap reset paksa
- Tambah kompensasi `lastValidTime` saat bangun dari deep sleep
- Ubah OTA check ke 6 jam, sleep start ke 23:00

### v2.2.9 (Maret 2026)
- Tambah fitur RFID Local Database

### v2.2.8 (Maret 2026)
- Tambah OTA Update otomatis

### v2.2.7 (Maret 2026)
- Rilis awal sistem hybrid (Queue System + NVS Buffer + Deep Sleep)

---

## Lisensi

Hak Cipta 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah **MIT License** untuk penggunaan pendidikan dan pengembangan profesional.
