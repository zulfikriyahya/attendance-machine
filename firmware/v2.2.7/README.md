# Sistem Presensi Pintar Berbasis IoT (Hybrid Edition)

**Attendance Machine** adalah solusi presensi cerdas berbasis _Internet of Things_ (IoT) yang dirancang untuk mengatasi tantangan infrastruktur jaringan yang tidak stabil. Dibangun di atas mikrokontroler ESP32-C3, sistem ini menerapkan arsitektur _Hybrid_ yang menggabungkan kemampuan pemrosesan daring (_online_) dan luring (_offline_) secara mulus.

Sistem ini beroperasi dengan filosofi _Self-Healing_ dan _Store-and-Forward_, menjamin integritas data kehadiran tanpa kehilangan (_zero data loss_) melalui mekanisme antrean terpartisi (_Partitioned Queue System_), NVS buffer internal, sinkronisasi otomatis, dan **operasi latar belakang yang tidak mengganggu pengguna** (_Non-Intrusive Background Operations_).

---

## Spesifikasi

| Field | Value |
|---|---|
| Project | Madrasah Universe |
| Author | Yahya Zulfikri |
| Device | ESP32-C3 Super Mini |
| Versi | 2.2.7 |
| IDE | Arduino IDE v2.3.6 |
| Dibuat | Juli 2025 |
| Diperbarui | Maret 2026 |

---

## Arsitektur Sistem

Sistem dirancang sebagai gerbang fisik data kehadiran yang agnostik terhadap status konektivitas dengan prioritas pada responsivitas dan user experience.

### Mekanisme Operasional Utama

1. **Identifikasi:** Pengguna memindai kartu RFID pada perangkat.
2. **Validasi Lokal:** Perangkat melakukan verifikasi _debounce_ dan pengecekan duplikasi data dalam interval waktu tertentu (default: 30 menit) langsung pada penyimpanan lokal untuk mencegah input ganda. Pengecekan mencakup 3 file antrean terakhir untuk menutup celah di boundary file.
3. **Manajemen Penyimpanan (Queue System):** Saat ada SD card, data selalu masuk ke antrean CSV (`queue_X.csv`) tanpa validasi jaringan. Validasi RFID dilakukan server saat proses sync bulk. Metadata antrean disimpan di `queue_meta.txt` untuk efisiensi akses.
4. **NVS Buffer (Fallback Tanpa SD Card):** Saat SD card tidak tersedia, data disimpan sementara di flash internal ESP32 (NVS) dengan kapasitas 20 record. Data di NVS disync ke server saat koneksi tersedia dan dihapus setelah berhasil. Jika NVS penuh, tap ditolak.
5. **Sinkronisasi Latar Belakang (_Background Sync_):** Sistem secara berkala (setiap 5 menit) mengirim data secara _batch_ ke server tanpa feedback visual. NVS buffer disync terlebih dahulu sebelum queue SD card.
6. **Reconnect Otomatis (_Silent Auto-Reconnect_):** Jika WiFi terputus, sistem mencoba reconnect setiap 5 menit di latar belakang. Setelah kembali online, NVS buffer dan queue SD disync otomatis.
7. **Manajemen OLED Cerdas:** Layar OLED otomatis mati pada jam tertentu untuk hemat daya, namun tetap menyala sementara saat ada tapping kartu.
8. **Integrasi Hilir:** Server memproses data _batch_ untuk keperluan notifikasi WhatsApp, laporan digital, dan analisis kehadiran.

---

## Spesifikasi Teknis

### Perangkat Keras

| Komponen             | Spesifikasi              | Fungsi Utama                                                               |
| :------------------- | :----------------------- | :------------------------------------------------------------------------- |
| **Unit Pemroses**    | ESP32-C3 Super Mini      | Manajemen logika utama, konektivitas WiFi, dan sistem berkas.              |
| **Sensor Identitas** | RFID RC522 (13.56 MHz)   | Pembacaan UID kartu presensi (Protokol SPI).                               |
| **Penyimpanan**      | Modul MicroSD (SPI)      | Penyimpanan antrean data offline (CSV) dan log sistem. Opsional.           |
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

## Diagram Koneksi

![Diagram](./diagram.svg)

---

## Fitur Perangkat Lunak (Firmware)

### Core Features

- **Offline-First Capability:** Prioritas penyimpanan data lokal saat jaringan tidak tersedia atau tidak stabil.
- **Partitioned Queue System:** Manajemen memori tingkat lanjut yang memecah penyimpanan data menjadi berkas-berkas kecil untuk mencegah _buffer overflow_.
- **NVS Buffer:** Penyimpanan fallback di flash internal ESP32 untuk kondisi tanpa SD card. Kapasitas 20 record, persisten melewati restart dan deep sleep.
- **Queue-First Validation:** Saat ada SD card, data langsung masuk queue tanpa memanggil server. Validasi RFID dilakukan server saat sync bulk, sehingga tap kartu tidak pernah terhambat latensi jaringan.
- **Smart Duplicate Prevention:** Algoritma _sliding window_ yang memindai 3 indeks antrean lokal terakhir untuk menolak pemindaian kartu yang sama dalam periode waktu yang dikonfigurasi (default: 30 menit).
- **Bulk Upload Efficiency:** Mengirimkan himpunan data dalam satu permintaan HTTP POST.
- **Hybrid Timekeeping:** Sinkronisasi waktu menggunakan NTP saat daring, dan estimasi waktu berbasis RTC internal saat luring.
- **Deep Sleep Scheduling:** Manajemen daya otomatis di luar jam operasional (default: 18:00–05:00).
- **Single SSID:** Konfigurasi jaringan satu SSID dengan reconnect state machine 4 state.

### Advanced Features

- **Silent Background Sync:** Sinkronisasi data berjalan di latar belakang tanpa feedback visual atau audio. NVS buffer disync sebelum queue SD.
- **Non-Intrusive Reconnect:** Auto-reconnect WiFi tanpa menampilkan loading screen.
- **Zero-Interruption UX:** Tap kartu tidak pernah terblokir oleh proses background maupun feedback OLED.
- **Zero Network Latency on Tap:** Tidak ada panggilan jaringan saat tap — baik kondisi ada SD maupun kondisi tanpa SD dengan jaringan lambat/down.
- **Task Watchdog (WDT):** Pemulihan otomatis 60 detik, dinonaktifkan sebelum deep sleep.
- **HTTPS Enforcement:** Semua komunikasi API menggunakan `WiFiClientSecure`.
- **Queue Overwrite Protection:** Rotasi file antrean tidak menimpa file yang belum ter-sync.
- **Metadata-Driven Cache:** Pending count disimpan di `queue_meta.txt`, menghindari scan penuh 2000 file setiap boot.
- **Heap Fragmentation Prevention:** Seluruh operasi string menggunakan `char[]` di stack.
- **OLED Auto Dim:** Display mati otomatis pukul 08:00 dan nyala kembali pukul 14:00.
- **Smart Wake-up on Tap:** Display menyala sementara saat ada tap kartu meski dalam periode dim, menggunakan `RfidFeedback` struct non-blocking.

---

## Konfigurasi Sistem

```cpp
// Konfigurasi Jaringan
const char WIFI_SSID[]       PROGMEM = "SSID_WIFI";
const char WIFI_PASSWORD[]   PROGMEM = "PasswordWifi";
const char API_BASE_URL[]    PROGMEM = "https://zedlabs.id";
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
4. **Sliding Window Duplicate Check:** Memeriksa 3 berkas antrean terakhir untuk akurasi di boundary file.
5. **Sinkronisasi Background:** Data dikirim batch ke server tanpa feedback visual. Jika HTTP 200, berkas dihapus. Jika gagal, berkas dipertahankan.

## Mekanisme NVS Buffer

1. **Kapasitas:** 20 record, disimpan di flash internal ESP32 (namespace `presensi`).
2. **Persistensi:** Data bertahan melewati restart dan deep sleep.
3. **Prioritas Sync:** NVS buffer disync ke server sebelum queue SD card.
4. **Full Buffer:** Jika NVS penuh dan server masih tidak dapat dihubungi, tap ditolak dengan pesan `BUFFER PENUH!`.
5. **Pembersihan:** Semua record NVS dihapus setelah server merespons HTTP 200.

---

## Alur Operasi

### Boot

```
Startup Animation
    └─ Init SD Card
        ├─ Ada SD  → Load metadata → tampil pending records
        └─ Tidak ada SD → cek NVS buffer → tampil jika ada
    └─ Connect WiFi
        ├─ Berhasil → Ping API
        │   ├─ API OK → Sync NTP → Sync NVS buffer → Bulk sync SD queue
        │   └─ API Gagal → Offline mode
        └─ Gagal → Offline mode
    └─ Init RFID
    └─ Sistem Siap
```

### Saat Kartu Di-tap

```
RFID terbaca
    └─ Ada SD card?
        ├─ Ya → Langsung simpan ke queue SD (tanpa panggilan jaringan)
        │       Server menolak RFID invalid saat sync → dicatat failed_log.csv
        └─ Tidak ada SD
            ├─ Online → kirimLangsung() ke API
            │   ├─ Berhasil → selesai
            │   └─ Gagal (timeout/down/lambat) → simpan ke NVS buffer
            └─ Offline → simpan ke NVS buffer

    Jika NVS penuh → tolak tap (BUFFER PENUH!)
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

## Dependensi Library

| Library | Versi yang diuji |
|---|---|
| MFRC522 | ≥ 1.4.10 |
| Adafruit SSD1306 | ≥ 2.5.7 |
| Adafruit GFX Library | ≥ 1.11.9 |
| ArduinoJson | ≥ 7.x |
| SdFat | ≥ 2.2.x |

Library bawaan ESP32 core (tidak perlu install terpisah): `WiFi`, `WiFiClientSecure`, `HTTPClient`, `Wire`, `SPI`, `time`, `esp_task_wdt`, `Preferences`.

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
| RFID Feedback          | Non-blocking             |
| String Handling        | Stack-based `char[]`     |
| Transport Security     | HTTPS (WiFiClientSecure) |
| Queue Overwrite Guard  | Enabled                  |

### Performance Metrics

| Metrik                  | Nilai                              |
| :---------------------- | :--------------------------------- |
| Tap Latency (Ada SD)    | < 50ms (tulis SD saja)             |
| Tap Latency (Tanpa SD, online, server OK) | < 10 detik      |
| Tap Latency (Tanpa SD, server down/lambat) | < 50ms (NVS)  |
| Tap Latency (Tanpa SD, offline) | < 50ms (NVS)             |
| Background Sync         | Setiap 5 menit                     |
| Queue Capacity          | 50.000 records (SD) + 20 (NVS)     |
| Power (Active)          | ~150mA                             |
| Power (Deep Sleep)      | < 5mA                              |

---

## Troubleshooting

**Masalah: Device restart saat boot setelah SD card terdeteksi**
Kemungkinan WDT trigger selama scan file. Naikkan `WDT_TIMEOUT_SEC` sementara ke 120 untuk diagnosis.

**Masalah: `esp_task_wdt_init` compilation error**
Pastikan menggunakan ESP32 Arduino core v3.x.

**Masalah: OLED tidak mati/menyala sesuai jadwal**
Pastikan waktu sistem sudah tersinkronisasi dengan NTP. Periksa nilai `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR`.

**Masalah: NVS buffer tidak terhapus setelah online**
Cek koneksi server. NVS hanya dihapus jika server merespons HTTP 200. Jika server menolak semua record, record akan tetap di NVS hingga berhasil dikirim.

**Masalah: Tap ditolak dengan pesan BUFFER PENUH!**
NVS buffer (20 record) penuh dan server belum bisa dihubungi. Pastikan koneksi WiFi dan server dalam kondisi baik. Data akan otomatis disync dan slot NVS dikosongkan saat server kembali online.

**Masalah: Record tidak tersync meski online**
Cek `failed_log.csv` di SD card untuk melihat alasan penolakan dari server.

---

## Lisensi

Hak Cipta 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
