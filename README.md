# Sistem Presensi Pintar Berbasis IoT (Hybrid Edition)

**Attendance Machine** adalah solusi presensi cerdas berbasis _Internet of Things_ (IoT) yang dirancang untuk mengatasi tantangan infrastruktur jaringan yang tidak stabil. Dibangun di atas mikrokontroler ESP32-C3, sistem ini menerapkan arsitektur _Hybrid_ yang menggabungkan kemampuan pemrosesan daring (_online_) dan luring (_offline_) secara mulus.

Sistem ini beroperasi dengan filosofi _Self-Healing_ dan _Store-and-Forward_, menjamin integritas data kehadiran tanpa kehilangan (_zero data loss_) melalui mekanisme antrean terpartisi (_Partitioned Queue System_), sinkronisasi otomatis, dan **operasi latar belakang yang tidak mengganggu pengguna** (_Non-Intrusive Background Operations_).

---

## Visi dan Misi Proyek

Proyek ini bertujuan mendigitalisasi, mengautomatisasi, dan mengintegrasikan data kehadiran lembaga pendidikan dan instansi pemerintahan dengan sistem yang tangguh (_resilient_) dan **user-friendly**. Fokus utamanya adalah menghapus hambatan teknis akibat gangguan jaringan, menciptakan transparansi data, mempermudah pelaporan administratif melalui ekosistem digital terintegrasi, serta memberikan **pengalaman pengguna yang mulus tanpa gangguan proses teknis**.

---

## Arsitektur Sistem

Sistem dirancang sebagai gerbang fisik data kehadiran yang agnostik terhadap status konektivitas dengan prioritas pada responsivitas dan user experience.

### Mekanisme Operasional Utama

1. **Identifikasi:** Pengguna memindai kartu RFID pada perangkat.
2. **Validasi Lokal:** Perangkat melakukan verifikasi _debounce_ dan pengecekan duplikasi data dalam interval waktu tertentu (default: 30 menit) langsung pada penyimpanan lokal untuk mencegah input ganda.
3. **Manajemen Penyimpanan (Queue System):** Data tidak dikirim langsung satu per satu, melainkan masuk ke dalam sistem antrean berkas CSV (`queue_X.csv`). Data dipecah menjadi berkas-berkas kecil (maksimal 25 rekaman per berkas) untuk menjaga stabilitas memori RAM mikrokontroler.
4. **Sinkronisasi Latar Belakang (_Background Sync_):** Sistem secara berkala (setiap 5 menit) memeriksa keberadaan berkas antrean. Jika koneksi internet tersedia, data dikirim secara _batch_ ke server **tanpa menampilkan proses atau mengganggu pengguna**. Berkas lokal dihapus secara otomatis hanya jika server memberikan respons sukses (HTTP 200).
5. **Reconnect Otomatis (_Silent Auto-Reconnect_):** Jika WiFi terputus, sistem mencoba reconnect setiap 5 menit secara otomatis dan diam-diam di latar belakang. Setelah kembali online, sistem otomatis melakukan sync data offline yang tertunda.
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
- **Smart Duplicate Prevention:** Algoritma _sliding window_ yang memindai indeks antrean lokal untuk menolak pemindaian kartu yang sama dalam periode waktu yang dikonfigurasi (default: 30 menit).
- **Bulk Upload Efficiency:** Mengoptimalkan penggunaan _bandwidth_ dengan mengirimkan himpunan data dalam satu permintaan HTTP POST.
- **Hybrid Timekeeping:** Sinkronisasi waktu presisi menggunakan NTP saat daring, dan estimasi waktu berbasis RTC internal saat luring.
- **Deep Sleep Scheduling:** Manajemen daya otomatis untuk menonaktifkan sistem di luar jam operasional (default: 18:00–05:00).

### Advanced Features (v2.2.2+)

- **Silent Background Sync:** Sinkronisasi data berjalan di latar belakang tanpa feedback visual atau audio yang mengganggu.
- **Non-Intrusive Reconnect:** Auto-reconnect WiFi tanpa menampilkan loading screen atau progress bar.
- **Zero-Interruption UX:** Pengguna dapat melakukan tapping RFID kapan saja tanpa terblokir oleh proses background.
- **Thread-Safe Operations:** Implementasi dual-flag system (`isSyncing`, `isReconnecting`) untuk mencegah race condition.
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

---

## Konfigurasi Sistem

Parameter operasional utama didefinisikan pada bagian awal kode sumber:

```cpp
// Konfigurasi Jaringan
const char WIFI_SSID_1[]     PROGMEM = "SSID_WIFI_1";
const char WIFI_SSID_2[]     PROGMEM = "SSID_WIFI_2";
const char WIFI_PASSWORD_1[] PROGMEM = "PasswordWifi1";
const char WIFI_PASSWORD_2[] PROGMEM = "PasswordWifi2";
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

// Konfigurasi OLED Auto Dim (v2.2.4)
const int OLED_DIM_START_HOUR = 8;  // Pukul 08:00 - OLED Mati
const int OLED_DIM_END_HOUR   = 14; // Pukul 14:00 - OLED Nyala

// Konfigurasi Display
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Update setiap 1 detik
```

---

## Mekanisme Antrean (Queue Logic)

1. **Segmentasi:** Data disimpan dalam berkas kecil (`queue_N.csv`) berisi maksimal 25 baris untuk mencegah _buffer overflow_.
2. **Rotasi:** Saat berkas `queue_N` penuh, sistem otomatis membuat `queue_N+1`.
3. **Sliding Window Duplicate Check:** Hanya memeriksa berkas antrean aktif dan 1 berkas sebelumnya — menjaga performa cepat meskipun data tersimpan sangat besar.
4. **Sinkronisasi Background:**
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

### Implementasi Flag Thread-Safety

```cpp
// Global flags untuk mencegah operasi paralel
bool isSyncing      = false; // Mencegah sync bersamaan
bool isReconnecting = false; // Mencegah reconnect bersamaan
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

### Endpoint Sinkronisasi (Bulk)

- **URL:** `/api/presensi/sync-bulk`
- **Method:** `POST`
- **Header:** `Content-Type: application/json`, `X-API-KEY: [SECRET_KEY]`
- **Payload:**
  ```json
  {
    "data": [
      {
        "rfid": "UID_KARTU",
        "timestamp": "YYYY-MM-DD HH:MM:SS",
        "device_id": "ESP32_MAC_ADDR",
        "sync_mode": true
      }
    ]
  }
  ```
- **Respons:** Server harus mengembalikan **HTTP 200 OK** untuk mengonfirmasi penerimaan data. Kode lain akan menyebabkan perangkat menyimpan kembali data dan mencoba pengiriman ulang di siklus berikutnya.

### Endpoint Health Check

- **URL:** `/api/presensi/ping`
- **Method:** `GET`
- **Header:** `X-API-KEY: [SECRET_KEY]`
- **Respons:** HTTP 200 jika server aktif dan siap menerima data.

---

## Kompatibilitas Kartu SD

Berkat implementasi `SdFat`, sistem mendukung berbagai jenis kartu memori:

- **SDSC** (Standard Capacity) ≤ 2GB, termasuk kartu lama 128MB/256MB.
- **SDHC** (High Capacity) 4GB – 32GB.
- Format sistem berkas: **FAT16** dan **FAT32**.

---

## Prasyarat Instalasi Perangkat Lunak

Install pustaka berikut melalui Arduino Library Manager:

1. **SdFat** oleh Bill Greiman (Versi 2.x) — **Wajib**, menggantikan pustaka `SD` bawaan Arduino.
2. **MFRC522** (GithubCommunity) — Driver RFID.
3. **Adafruit SSD1306** & **Adafruit GFX** — Driver tampilan OLED.
4. **ArduinoJson** — Serialisasi JSON.
5. **WiFi**, **HTTPClient**, **Wire**, **SPI** — Pustaka inti ESP32.

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
│   └── v2.2.4/     # OLED Auto Dim
└── LICENSE
```

---

## Technical Specifications

### Queue System

| Parameter             | Nilai          |
| :-------------------- | :------------- |
| Max Records per File  | 25             |
| Max Queue Files       | 2.000          |
| Total Capacity        | 50.000 records |
| Sync Interval         | 300 detik      |
| Duplicate Check       | 1.800 detik    |

### WiFi Configuration

| Parameter          | Nilai                      |
| :----------------- | :------------------------- |
| TX Power           | 19.5 dBm (maksimal)        |
| Sleep Mode         | WIFI_PS_MAX_MODEM          |
| Sort Method        | WIFI_CONNECT_AP_BY_SIGNAL  |
| Persistent         | Enabled                    |
| Auto-Reconnect     | Enabled                    |
| Reconnect Interval | 300 detik (5 menit)        |

### Display & Power

| Parameter              | Nilai                     |
| :--------------------- | :------------------------ |
| Display Update         | 1.000 ms (1 detik)        |
| RSSI Bars              | 4 levels                  |
| OLED Auto Dim          | 08:00 – 14:00 (default)   |
| Deep Sleep             | 18:00 – 05:00 (default)   |
| Modem Sleep            | WIFI_PS_MAX_MODEM         |

### RSSI Interpretation

| RSSI          | Kualitas         |
| :------------ | :--------------- |
| ≥ -50 dBm     | Sangat Kuat      |
| -67 dBm       | Kuat (4 bar)     |
| -70 dBm       | Baik (3 bar)     |
| -80 dBm       | Cukup (2 bar)    |
| -90 dBm       | Lemah (1 bar)    |
| < -90 dBm     | Sangat Lemah     |

---

## Troubleshooting

**Masalah: WiFi sering disconnect**
Pastikan perangkat dalam jangkauan yang cukup (RSSI di atas -70 dBm). Periksa interferensi pada frekuensi 2.4 GHz dan stabilitas power supply (minimal 5V 1A).

**Masalah: Sync lambat atau terblokir**
Pastikan tidak ada `showOLED()` atau `playTone()` di dalam fungsi `chunkedSync()` atau `syncAllQueues()`. Semua feedback visual harus dihapus untuk operasi background yang optimal.

**Masalah: Double sync/reconnect**
Periksa implementasi flag `isSyncing` dan `isReconnecting`. Flag harus di-set sebelum operasi dimulai dan di-clear setelah selesai.

**Masalah: OLED tidak mati/menyala sesuai jadwal**
Pastikan waktu sistem sudah tersinkronisasi dengan NTP server. Periksa nilai `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR` di konfigurasi.

**Masalah: Display tidak update status online/offline**
Pastikan `updateStandbySignal()` membaca variabel `isOnline` yang selalu diperbarui di loop utama.

---

## Performance Metrics

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

## Riwayat Perubahan (Changelog)

### v2.2.4 (Desember 2025) — OLED Auto Dim
- Scheduled Display Control: OLED otomatis mati pukul 08:00, nyala pukul 14:00
- Smart Wake-up: Display menyala sementara saat ada kartu yang di-tap
- Power Saving: Pengurangan konsumsi daya hingga 25%
- OLED Longevity: Perpanjangan umur display hingga 30–40%
- Implementasi flag `oledIsOn` untuk tracking status display

### v2.2.3 (Desember 2025) — WiFi Signal Optimization
- Maximum TX Power: Set WiFi TX Power ke 19.5 dBm untuk jangkauan maksimal
- Modem Sleep Mode: Implementasi WIFI_PS_MAX_MODEM untuk efisiensi daya
- Signal-Based AP Selection: Koneksi otomatis ke AP dengan sinyal terkuat
- Persistent Mode: WiFi persistent dan auto-reconnect untuk stabilitas
- RSSI Display: Tampilan kekuatan sinyal saat startup untuk monitoring
- Optimized Queue Config: MAX_RECORDS_PER_FILE ke 25, MAX_QUEUE_FILES ke 2000

### v2.2.2 (Desember 2025) — Background Operations
- Background WiFi Reconnect: Reconnect otomatis setiap 5 menit tanpa tampilan
- Background Bulk Sync: Sinkronisasi data offline di latar belakang
- Dual Process Flags: Implementasi `isSyncing` dan `isReconnecting`
- Silent Operations: Semua operasi network tanpa feedback visual/audio
- Enhanced UX: Display fokus pada informasi penting (status, waktu, queue counter)

### v2.2.1 (Desember 2025) — Stability & Auto-Reconnect
- Penambahan Fitur Autoconnect WiFi
- Perbaikan Bug System Versi 2.2.0

### v2.2.0 (Desember 2025) — Ultimate Edition
- Migrasi ke `SdFat`
- Dukungan kartu memori legacy (128MB+)
- Optimasi buffer memori
- Perbaikan visual interface

### v2.1.0 — Queue System Foundation
- Pengenalan sistem antrean multi-file
- Sliding window duplicate check

### v2.0.0 — Hybrid Architecture
- Implementasi mode offline dasar (single CSV)

### v1.0.0 — Initial Release
- Rilis awal (Online only)

---

## Peta Jalan Pengembangan (Roadmap)

1. **Versi 1.x** — Presensi Daring Dasar.
2. **Versi 2.x** — Sistem Hibrida, Queue Offline Terpartisi, Sinkronisasi Massal, Auto Reconnect, Legacy Storage, Background Operations, WiFi Optimization, OLED Auto Dim.
3. **Versi 3.x** — Integrasi Biometrik (Sidik Jari) sebagai metode autentikasi sekunder.
4. **Versi 4.x** — Ekspansi fungsi menjadi Buku Tamu Digital dan Integrasi PPDB.
5. **Versi 5.x** — Ekosistem Perpustakaan Pintar (Sirkulasi Mandiri).
6. **Versi 6.x** — IoT Gateway & LoRaWAN untuk area tanpa jangkauan seluler/WiFi.

---

## Best Practices

1. **Penempatan Perangkat:** Letakkan pada lokasi dengan sinyal WiFi yang baik (RSSI di atas -70 dBm). Gunakan RSSI display saat startup untuk monitoring.
2. **Power Supply:** Gunakan power supply stabil 5V 1A minimum untuk menghindari brownout saat transmisi WiFi daya maksimal.
3. **Multiple Access Points:** Jika deployment menggunakan beberapa AP, pastikan semua memiliki SSID dan password yang sama agar sistem dapat memilih AP terbaik otomatis.
4. **Background Process:** Biarkan background sync dan reconnect berjalan otomatis. Tidak perlu intervensi manual.
5. **Jangan Tambah Feedback Visual di Background:** Semua operasi dalam `periodicSync()` dan `connectToWiFiBackground()` harus silent.
6. **Jadwal OLED:** Sesuaikan `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR` dengan pola aktivitas presensi di lokasi deployment.

---

## Lisensi

Hak Cipta 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
