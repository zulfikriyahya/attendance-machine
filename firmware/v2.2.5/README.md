# Sistem Presensi Pintar Berbasis IoT (Hybrid Edition)

**Attendance Machine** adalah solusi presensi cerdas berbasis _Internet of Things_ (IoT) yang dirancang untuk mengatasi tantangan infrastruktur jaringan yang tidak stabil. Dibangun di atas mikrokontroler ESP32-C3, sistem ini menerapkan arsitektur _Hybrid_ yang menggabungkan kemampuan pemrosesan daring (_online_) dan luring (_offline_) secara mulus.

Sistem ini beroperasi dengan filosofi _Self-Healing_ dan _Store-and-Forward_, menjamin integritas data kehadiran tanpa kehilangan (_zero data loss_) melalui mekanisme antrean terpartisi (_Partitioned Queue System_), sinkronisasi otomatis, dan **operasi latar belakang yang tidak mengganggu pengguna** (_Non-Intrusive Background Operations_).

---

## Spesifikasi

| Field | Value |
|---|---|
| Project | Madrasah Universe |
| Author | Yahya Zulfikri |
| Device | ESP32-C3 Super Mini |
| Versi | 2.2.5 |
| IDE | Arduino IDE v2.3.6 |
| Dibuat | Juli 2025 |
| Diperbarui | Maret 2026 |

---

## Arsitektur Sistem

Sistem dirancang sebagai gerbang fisik data kehadiran yang agnostik terhadap status konektivitas dengan prioritas pada responsivitas dan user experience.

### Mekanisme Operasional Utama

1. **Identifikasi:** Pengguna memindai kartu RFID pada perangkat.
2. **Validasi Lokal:** Perangkat melakukan verifikasi _debounce_ dan pengecekan duplikasi data dalam interval waktu tertentu (default: 30 menit) langsung pada penyimpanan lokal untuk mencegah input ganda.
3. **Manajemen Penyimpanan (Queue System):** Data masuk ke sistem antrean berkas CSV (`queue_X.csv`), dipecah menjadi berkas-berkas kecil (maksimal 25 rekaman per berkas) untuk menjaga stabilitas memori RAM mikrokontroler.
4. **Sinkronisasi Latar Belakang (_Background Sync_):** Sistem secara berkala (setiap 5 menit) memeriksa keberadaan berkas antrean. Jika koneksi internet tersedia, data dikirim secara _batch_ ke server tanpa feedback visual. Berkas lokal dihapus secara otomatis hanya jika server memberikan respons sukses (HTTP 200).
5. **Reconnect Otomatis (_Silent Auto-Reconnect_):** Jika WiFi terputus, sistem mencoba reconnect ke dua SSID secara bergantian setiap 5 menit. Setelah kembali online, sistem otomatis melakukan sync data offline yang tertunda.
6. **Manajemen OLED Cerdas:** Layar OLED otomatis mati pada jam tertentu untuk hemat daya dan memperpanjang umur display, namun tetap menyala sementara saat ada tapping kartu.
7. **Integrasi Hilir:** Server memproses data _batch_ untuk keperluan notifikasi WhatsApp, laporan digital, dan analisis kehadiran.

---

## Spesifikasi Teknis

### Perangkat Keras

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

---

## Fitur Perangkat Lunak (Firmware)

### Core Features

- **Offline-First Capability:** Prioritas penyimpanan data lokal saat jaringan tidak tersedia atau tidak stabil.
- **Partitioned Queue System:** Manajemen memori tingkat lanjut yang memecah penyimpanan data menjadi berkas-berkas kecil untuk mencegah _buffer overflow_.
- **Smart Duplicate Prevention:** Algoritma _sliding window_ yang memindai 2 indeks antrean lokal terakhir untuk menolak pemindaian kartu yang sama dalam periode waktu yang dikonfigurasi (default: 30 menit).
- **Bulk Upload Efficiency:** Mengirimkan himpunan data dalam satu permintaan HTTP POST.
- **Hybrid Timekeeping:** Sinkronisasi waktu menggunakan NTP saat daring, dan estimasi waktu berbasis RTC internal saat luring.
- **Deep Sleep Scheduling:** Manajemen daya otomatis di luar jam operasional (default: 18:00–05:00).
- **Dual SSID Failover:** Mendukung dua konfigurasi WiFi sebagai primary dan fallback dengan state machine reconnect 7 state.

### Advanced Features

- **Silent Background Sync:** Sinkronisasi data berjalan di latar belakang tanpa feedback visual atau audio.
- **Non-Intrusive Reconnect:** Auto-reconnect WiFi tanpa menampilkan loading screen.
- **Smart Display Management:** OLED hanya menampilkan informasi penting (status, waktu, queue counter).
- **Legacy Storage Support:** Dukungan SD Card lama (128MB–256MB) menggunakan pustaka SdFat.
- **OLED Auto Dim:** Display mati otomatis pukul 08:00 dan nyala kembali pukul 14:00.
- **Maximum TX Power (19.5 dBm):** Jangkauan sinyal WiFi lebih jauh dengan penetrasi lebih baik.
- **Modem Sleep Mode:** Efisiensi daya tanpa mengorbankan responsivitas jaringan.

---

## Konfigurasi Sistem

```cpp
// Konfigurasi Jaringan
const char WIFI_SSID_1[]     PROGMEM = "SSID_WIFI_1";
const char WIFI_SSID_2[]     PROGMEM = "SSID_WIFI_2";
const char WIFI_PASSWORD_1[] PROGMEM = "PasswordWifi1";
const char WIFI_PASSWORD_2[] PROGMEM = "PasswordWifi2";
const char API_BASE_URL[]    PROGMEM = "https://zedlabs.id";
const char API_SECRET_KEY[]  PROGMEM = "SecretAPIToken";
const long GMT_OFFSET_SEC    = 25200; // WIB (UTC+7)

// Konfigurasi Antrean
const int MAX_RECORDS_PER_FILE      = 25;
const int MAX_QUEUE_FILES           = 2000;
const unsigned long SYNC_INTERVAL   = 300000;   // 5 menit

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
```

---

## Mekanisme Antrean (Queue Logic)

1. **Segmentasi:** Data disimpan dalam berkas kecil (`queue_N.csv`) berisi maksimal 25 baris.
2. **Rotasi:** Saat berkas `queue_N` penuh, sistem membuat `queue_N+1`. File lama pada slot berikutnya dihapus dan ditimpa.
3. **Sliding Window Duplicate Check:** Memeriksa 2 berkas antrean terakhir untuk menolak kartu yang sama dalam interval 30 menit.
4. **Sinkronisasi Background:** Data dikirim secara batch ke server. Jika HTTP 200, berkas dihapus. Jika gagal, berkas dipertahankan untuk percobaan berikutnya.
5. **Pending Count:** Dihitung dengan scan penuh seluruh SD card setiap kali cache dirty.

---

## Alur Operasi

### Boot

```
Startup Animation
    └─ Init SD Card
        ├─ Ada SD  → tampil pending records
        └─ Tidak ada SD → lanjut tanpa queue
    └─ Connect WiFi (SSID1 → SSID2)
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
    └─ Feedback OLED + buzzer
    └─ delay 1800ms (blocking)
    └─ Ada SD card?
        ├─ Ya
        │   ├─ Online → Validasi RFID → Simpan ke queue SD
        │   └─ Offline → Simpan ke queue SD
        └─ Tidak ada SD
            ├─ Online → Kirim langsung ke API
            └─ Offline → Tolak (NO SD & WIFI)
```

### State Machine Reconnect

```
RECONNECT_IDLE
    │
    ▼
RECONNECT_INIT_SSID1 ──► WiFi.begin(SSID1)
    │
    ▼
RECONNECT_TRYING_SSID1
    ├── Connected ──► RECONNECT_SUCCESS
    └── Timeout   ──► RECONNECT_INIT_SSID2 ──► WiFi.begin(SSID2)
                            │
                            ▼
                      RECONNECT_TRYING_SSID2
                            ├── Connected ──► RECONNECT_SUCCESS
                            └── Timeout   ──► RECONNECT_FAILED ──► IDLE
```

---

## OLED Auto Dim Schedule

| Waktu         | Status OLED | Keterangan                        |
| :------------ | :---------- | :-------------------------------- |
| 00:00 – 07:59 | ON          | Display aktif untuk presensi pagi |
| 08:00 – 13:59 | OFF         | Display mati untuk hemat daya     |
| 14:00 – 17:59 | ON          | Display aktif untuk presensi sore |
| 18:00 – 04:59 | SLEEP MODE  | Mesin deep sleep                  |

---

## API Specification

### Endpoint Health Check
- **URL:** `/api/presensi/ping` — **Method:** `GET`

### Endpoint Validasi RFID
- **URL:** `/api/presensi/validate` — **Method:** `POST`
- **Payload:** `{ "rfid": "0012345678" }`
- **Respons:** HTTP 200 (valid), HTTP 404 (tidak dikenali).

### Endpoint Kirim Langsung
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

### Endpoint Sinkronisasi Bulk
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
- **Respons:** HTTP 200. Record dengan `status: error` dicatat ke `failed_log.csv`.

Semua request menggunakan header `X-API-KEY`.

---

## Struktur File SD Card

```
/
├── queue_0.csv        ← File antrean aktif
├── queue_1.csv
├── ...
├── queue_1999.csv
└── failed_log.csv     ← Log record yang gagal disync
```

Format file antrean:
```
rfid,timestamp,device_id,unix_time
0012345678,2026-03-10 07:30:00,ESP32_A1B2,1741571400
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

Library bawaan ESP32 core: `WiFi`, `HTTPClient`, `Wire`, `SPI`, `time`.

---

## Technical Specifications

### Queue System

| Parameter             | Nilai           |
| :-------------------- | :-------------- |
| Max Records per File  | 25              |
| Max Queue Files       | 2.000           |
| Total Capacity        | 50.000 records  |
| Duplicate Check Range | 2 file terakhir |
| Sync Interval         | 300 detik       |
| Duplicate Interval    | 1.800 detik     |

### WiFi Configuration

| Parameter          | Nilai                                        |
| :----------------- | :------------------------------------------- |
| SSID Support       | 2 (primary + fallback)                       |
| TX Power           | 19.5 dBm                                     |
| Sleep Mode         | WIFI_PS_MAX_MODEM                            |
| Sort Method        | WIFI_CONNECT_AP_BY_SIGNAL                    |
| Reconnect States   | 7 (IDLE, INIT_SSID1, TRYING_SSID1, INIT_SSID2, TRYING_SSID2, SUCCESS, FAILED) |
| Reconnect Interval | 300 detik                                    |

### Performance Metrics

| Metrik                  | Nilai         |
| :---------------------- | :------------ |
| Tap-to-Feedback Latency | ~1800ms (blocking feedback) |
| Background Sync         | Setiap 5 menit |
| Queue Capacity          | 50.000 records |
| Power (Active)          | ~150mA        |
| Power (Deep Sleep)      | < 5mA         |

---

## Known Limitations

- **Blocking RFID feedback:** Setelah tap kartu, sistem menunggu 1800ms sebelum dapat membaca kartu berikutnya.
- **Heap fragmentation:** Penggunaan Arduino `String` pada fungsi yang dipanggil berulang berpotensi menyebabkan fragmentasi heap pada operasi jangka panjang.
- **Full SD scan:** Pending count dihitung dengan scan penuh hingga 2000 file setiap kali cache dirty, berpotensi lambat saat SD card memiliki banyak file.
- **No watchdog:** Tidak ada mekanisme pemulihan otomatis jika sistem hang.
- **HTTP only:** Komunikasi API tidak menggunakan enkripsi transport layer.
- **Queue overwrite risk:** Rotasi file antrean dapat menimpa file yang belum ter-sync jika sync tertinggal jauh.

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
│   └── v2.2.5/     # Stability & Multi-SSID
└── LICENSE
```

**Masalah: WiFi sering disconnect**
Pastikan RSSI di atas -70 dBm. Periksa interferensi pada frekuensi 2.4 GHz dan stabilitas power supply (minimal 5V 1A).

**Masalah: OLED tidak mati/menyala sesuai jadwal**
Pastikan waktu sistem sudah tersinkronisasi dengan NTP. Periksa nilai `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR`.

**Masalah: Record tidak tersync meski online**
Cek `failed_log.csv` di SD card untuk melihat alasan penolakan dari server.

**Masalah: Device hang tanpa restart otomatis**
v2.2.5 tidak memiliki watchdog. Cabut dan pasang kembali power supply, atau upgrade ke v2.2.6.

---

## Lisensi

Hak Cipta 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
