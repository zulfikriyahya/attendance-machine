# Sistem Presensi Pintar (RFID + Fingerprint)

> **ESP32-C3 Super Mini · MFRC522 · HLK-ZW101 · SSD1306 · SdFat Queue System**

[![Version](https://img.shields.io/badge/versi_stabil-v2.2.4-blue?style=flat-square)](.)
[![Version](https://img.shields.io/badge/versi_terbaru-v3.0.0-green?style=flat-square)](.)
[![Platform](https://img.shields.io/badge/platform-ESP32--C3-red?style=flat-square)](.)
[![License](https://img.shields.io/badge/lisensi-MIT-yellow?style=flat-square)](.)

---

## Daftar Isi

- [Deskripsi](#deskripsi)
- [Versi](#versi)
- [Fitur Teknis](#fitur-teknis)
- [Spesifikasi Hardware & Pinout](#spesifikasi-hardware--pinout)
- [Instalasi Library](#instalasi-library)
- [Konfigurasi](#konfigurasi)
- [Cara Penggunaan](#cara-penggunaan)
- [Alur Kerja Sistem](#alur-kerja-sistem)
- [API Specification](#api-specification)
- [Mekanisme Queue Offline](#mekanisme-queue-offline)
- [Power Consumption](#power-consumption)
- [Troubleshooting](#troubleshooting)
- [Changelog](#changelog)
- [Lisensi](#lisensi)

---

## Deskripsi

Sistem Presensi Pintar adalah perangkat absensi berbasis IoT yang dibangun di atas mikrokontroler **ESP32-C3 Super Mini**. Sistem ini mendukung dua metode input identifikasi — **kartu RFID** (via RC522) dan **sidik jari** (via HLK-ZW101) — dengan alur kerja yang sepenuhnya sama.

Dirancang untuk operasional jangka panjang tanpa pengawasan, sistem ini dilengkapi mekanisme antrian offline berkapasitas hingga **50.000 record**, sinkronisasi latar belakang ke API server, manajemen daya cerdas dengan jadwal deep sleep dan OLED auto dim, serta pemulihan kesalahan mandiri.

---

## Versi

| Versi | Nama | Status | File |
|:---|:---|:---|:---|
| **v3.0.0** | RFID + Fingerprint | Terbaru | `presensi_rfid_fingerprint_v3.0.0.ino` |
| **v2.2.4** | OLED Auto Dim | Stabil | `presensi_v2.2.4.ino` |
| v2.2.3 | WiFi Signal Optimization | Legacy | — |
| v2.2.2 | Background Operations | Legacy | — |
| v2.2.0 | Migrasi SdFat | Legacy | — |

---

## Fitur Teknis

### v3.0.0 — RFID + Fingerprint (Terbaru)

- **Dual Input:** Kartu RFID *dan* sidik jari HLK-ZW101 aktif bersamaan sebagai alternatif satu sama lain.
- **Alur Identik:** Tap kartu atau tempel jari memanggil fungsi `kirimPresensi()` yang sama persis — tidak ada perbedaan logika.
- **Format ID Kompatibel:** Fingerprint ID dikirim sebagai string `FP_XXXXX` sehingga langsung kompatibel dengan field `rfid` di database dan API yang sudah ada.
- **Enroll via Serial Monitor:** Template sidik jari bisa dienroll ke modul ZW101 langsung dari Serial Monitor Arduino IDE tanpa tools tambahan.
- **Tanpa Library Tambahan:** Protokol ZW101 diimplementasikan native — tidak perlu install library fingerprint apapun.
- **Deteksi Otomatis:** Jika ZW101 tidak terhubung, sistem tetap berjalan dengan RFID saja. OLED menampilkan `"TAP KARTU"` atau `"KARTU / JARI"` sesuai hardware yang terdeteksi.

### v2.2.4 — OLED Auto Dim

- **Scheduled Display Control:** OLED otomatis mati pukul **08:00** dan nyala kembali pukul **14:00**.
- **Smart Wake-up on Tap:** Display menyala sementara saat ada kartu yang di-tap, lalu kembali mati sesuai jadwal.
- **Power Saving:** Mengurangi konsumsi daya hingga **25%** dan memperpanjang umur OLED hingga **30–40%**.
- **Seamless Operation:** Proses absensi tetap berjalan normal meskipun display mati.

### Fitur Umum (Semua Versi)

- **WiFi Signal Optimization:** TX Power maksimum 19.5 dBm, koneksi otomatis ke AP dengan sinyal terkuat, modem sleep mode.
- **Background Processing:** Reconnect WiFi, sinkronisasi data, dan operasi jaringan berjalan di latar belakang tanpa mengganggu proses tapping.
- **Queue System Skala Besar:** Hingga 2.000 file antrian × 25 record = **50.000 data presensi** saat offline.
- **Duplicate Prevention:** Sliding window duplicate check — satu kartu/jari tidak bisa absen dua kali dalam 30 menit.
- **NTP Time Sync:** Sinkronisasi waktu otomatis setiap 1 jam ke server NTP.
- **Deep Sleep:** Mesin tidur otomatis pukul 18:00 dan bangun pukul 05:00 untuk hemat daya.
- **Auto-Restart on Fatal Error:** Restart otomatis jika modul RFID atau SD card gagal inisialisasi.

---

## Spesifikasi Hardware & Pinout

### Komponen yang Dibutuhkan

| Komponen | Spesifikasi | Keterangan |
|:---|:---|:---|
| Mikrokontroler | ESP32-C3 Super Mini | Wajib |
| RFID Reader | RC522 (MFRC522) | Wajib |
| Fingerprint | HLK-ZW101 | Opsional (v3.0.0+) |
| Display | OLED SSD1306 128×64 I2C | Wajib |
| Penyimpanan | MicroSD Card Module | Wajib |
| Buzzer | Passive Buzzer 3V | Wajib |
| MicroSD | SDHC 4–32GB, FAT32 | Wajib |

### Pinout — RFID, SD Card, OLED, Buzzer

| Komponen | Pin Modul | Pin ESP32-C3 | Protokol |
|:---|:---|:---|:---|
| **Bus SPI** | SCK | GPIO 4 | SPI (Shared) |
| | MOSI | GPIO 6 | SPI (Shared) |
| | MISO | GPIO 5 | SPI (Shared) |
| **RFID RC522** | SDA (SS) | GPIO 7 | SPI |
| | RST | GPIO 3 | Digital |
| **SD Card** | CS | GPIO 1 | SPI |
| **OLED SSD1306** | SDA | GPIO 8 | I2C |
| | SCL | GPIO 9 | I2C |
| **Buzzer** | (+) | GPIO 10 | PWM |

### Pinout Tambahan — HLK-ZW101 (v3.0.0)

| Pin ZW101 | Pin ESP32-C3 | Keterangan |
|:---|:---|:---|
| VCC | 3.3V | Catu daya |
| GND | GND | Ground |
| TX | GPIO 2 | ESP32 RX ← TX ZW101 |
| RX | GPIO 21 | ESP32 TX → RX ZW101 |
| TOUCH | GPIO 20 | Sinyal deteksi jari (opsional, set -1 jika tidak pakai) |

> **Catatan:** ZW101 menggunakan UART1 (`HardwareSerial(1)`) pada ESP32-C3 dengan baud rate 57600. Modul RC522 dan ZW101 menggunakan antarmuka berbeda (SPI vs UART) sehingga tidak ada konflik.

---

## Instalasi Library

Buka Arduino IDE → **Tools → Manage Libraries**, cari dan install library berikut:

| Library | Author | Keterangan |
|:---|:---|:---|
| `Adafruit SSD1306` | Adafruit | Driver OLED |
| `Adafruit GFX Library` | Adafruit | Dependensi SSD1306 |
| `MFRC522` | GithubCommunity | Driver RFID RC522 |
| `ArduinoJson` | Benoit Blanchon | Serialisasi JSON untuk API |
| `SdFat` | Bill Greiman (v2.x) | **Wajib pakai SdFat**, bukan library SD bawaan Arduino |

**Tidak ada library tambahan** untuk fingerprint HLK-ZW101 — protokolnya sudah diimplementasikan langsung di kode.

### Board Package ESP32

Tambahkan URL berikut di **File → Preferences → Additional Boards Manager URLs**:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Kemudian buka **Tools → Board → Boards Manager**, cari `esp32` oleh Espressif Systems, dan install.

**Board Setting yang Direkomendasikan:**

| Setting | Nilai |
|:---|:---|
| Board | ESP32C3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 4MB |
| Partition Scheme | Default 4MB with spiffs |
| Upload Speed | 921600 |

---

## Konfigurasi

Edit bagian konfigurasi di bagian atas file `.ino` sebelum upload:

```cpp
// ── NETWORK ──────────────────────────────────────────────
const char WIFI_SSID_1[]     PROGMEM = "SSID_WIFI_1";
const char WIFI_SSID_2[]     PROGMEM = "SSID_WIFI_2";       // Fallback SSID
const char WIFI_PASSWORD_1[] PROGMEM = "PasswordWifi1";
const char WIFI_PASSWORD_2[] PROGMEM = "PasswordWifi2";
const char API_BASE_URL[]    PROGMEM = "https://domain-anda.id";
const char API_SECRET_KEY[]  PROGMEM = "your-secret-key";

// ── QUEUE SYSTEM ─────────────────────────────────────────
const int MAX_RECORDS_PER_FILE   = 25;       // Record per file CSV
const int MAX_QUEUE_FILES        = 2000;     // Total kapasitas: 50.000 record
const unsigned long SYNC_INTERVAL = 300000; // Sync tiap 5 menit

// ── JADWAL ───────────────────────────────────────────────
const int SLEEP_START_HOUR   = 18;  // Deep sleep mulai pukul 18:00
const int SLEEP_END_HOUR     = 5;   // Deep sleep selesai pukul 05:00
const int OLED_DIM_START_HOUR = 8;  // OLED mati pukul 08:00
const int OLED_DIM_END_HOUR   = 14; // OLED nyala pukul 14:00

// ── VALIDASI ─────────────────────────────────────────────
const unsigned long MIN_REPEAT_INTERVAL = 1800; // Debounce 30 menit

// ── FINGERPRINT (v3.0.0) ─────────────────────────────────
#define PIN_FP_RX    2     // ESP32 RX ← TX ZW101
#define PIN_FP_TX    21    // ESP32 TX → RX ZW101
#define PIN_FP_TOUCH 20    // Set -1 jika tidak pakai TOUCH pin
#define FP_MAX_TEMPLATES 50
```

### Jadwal OLED Default

| Waktu | Status OLED | Keterangan |
|:---|:---|:---|
| 00:00 – 07:59 | ON | Display aktif untuk presensi pagi |
| 08:00 – 13:59 | OFF | Display mati, hemat daya |
| 14:00 – 17:59 | ON | Display aktif untuk presensi sore |
| 18:00 – 04:59 | SLEEP MODE | Deep sleep, display juga mati |

---

## Cara Penggunaan

### Absensi Normal

Tidak diperlukan konfigurasi khusus. Cukup tap kartu RFID atau tempel jari — sistem akan langsung menyimpan data.

### Enroll Template Sidik Jari (v3.0.0)

Enroll dilakukan **satu kali** sebelum modul dipakai untuk absensi. Buka **Serial Monitor** (baud rate 115200) dan ketik perintah berikut:

| Perintah | Fungsi | Contoh |
|:---|:---|:---|
| `ENROLL <id>` | Enroll jari ke slot ID tertentu | `ENROLL 1` |
| `FPCOUNT` | Lihat jumlah template tersimpan | `FPCOUNT` |
| `FPDEL <id>` | Hapus template ID tertentu | `FPDEL 3` |
| `FPEMPTY` | Hapus semua template | `FPEMPTY` |

**Langkah Enroll:**
1. Ketik `ENROLL 1` di Serial Monitor, tekan Enter.
2. Tempel jari pada sensor saat OLED menampilkan `TEMPEL JARI 1`.
3. Angkat jari saat OLED menampilkan `ANGKAT JARI...`.
4. Tempel jari lagi saat OLED menampilkan `TEMPEL JARI 2`.
5. Jika berhasil, OLED menampilkan `ENROLL OK! · ID 1 TERSIMPAN`.

> **Penting:** ID di ZW101 harus cocok dengan mapping di database server Anda. Jika pegawai A memiliki `fp_id = 1` di database, enroll ke slot 1 — alat akan mengirim `FP_00001` ke API, dan server yang mencocokkan ke data pegawai.

---

## Alur Kerja Sistem

### Alur Utama (Main Loop)

```
[POWER ON] → [Setup: SD · ZW101 · WiFi · RFID · NTP]
                          ↓
                      [loop()]
                          ↓
              ┌─── Enroll Mode? ───┐
              │ YA                 │ TIDAK
              ↓                   ↓
     [handleFPEnroll()]   ┌─── Kartu Ditap? ───┐
              │           │ YA                  │ TIDAK
              │           ↓                     ↓
              │  [handleRFIDScan()]   ┌── Jari Terdeteksi? ──┐
              │           │          │ YA                    │ TIDAK
              │           │          ↓                       ↓
              │           │ [handleFPScan()]    [Background Tasks]
              │           │          │          - processReconnect()
              │           └──────────┘          - updateDisplay()
              │                 ↓               - chunkedSync()
              │        [kirimPresensi(uid)]      - periodicTimeSync()
              │                 ↓                       ↓
              └─────────────────┴───────────────────────┘
                                ↓
                     ┌── Jam Sleep (≥18:00)? ──┐
                     │ YA                       │ TIDAK
                     ↓                          ↓
               [DEEP SLEEP]               [yield() → ulang]
              bangun 05:00
```

### Alur kirimPresensi() — Fungsi Bersama RFID & Fingerprint

```
kirimPresensi(uid)
       ↓
   SD Card ada? ── TIDAK ──→ return "NO WIFI & SD"
       ↓ YA
  Duplikat < 30 menit? ── YA ──→ return "CUKUP SEKALI!"
       ↓ TIDAK
   saveToQueue(uid, timestamp, unix_time)
       ↓
   return "DATA TERSIMPAN"
```

### OLED Auto Dim Saat RFID/Fingerprint Scan

```
[OLED OFF - jam dim] → [Kartu/Jari Terdeteksi]
                                ↓
                      [Simpan wasOff = true]
                                ↓
                         [Nyalakan OLED]
                                ↓
                     [Tampilkan feedback scan]
                                ↓
                       [Proses selesai]
                                ↓
                   [wasOff == true? → Cek jadwal]
                                ↓
                         [Matikan OLED]
```

---

## API Specification

### Endpoint Absensi Bulk (Sync Offline)

```
POST /api/presensi/sync-bulk
Content-Type: application/json
X-API-KEY: <SECRET_KEY>
```

**Payload:**
```json
{
  "data": [
    {
      "rfid": "0001234567",
      "timestamp": "2025-12-01 08:23:11",
      "device_id": "ESP32_A1B2",
      "sync_mode": true
    },
    {
      "rfid": "FP_00003",
      "timestamp": "2025-12-01 08:24:05",
      "device_id": "ESP32_A1B2",
      "sync_mode": true
    }
  ]
}
```

> Field `rfid` berisi UID kartu (`0001234567`) **atau** ID fingerprint (`FP_00003`). Server yang bertanggung jawab mencocokkan ke data pegawai.

**Response:** `HTTP 200` jika berhasil → file queue dihapus dari SD card. Selain 200, file dipertahankan untuk percobaan berikutnya.

### Endpoint Health Check

```
GET /api/presensi/ping
X-API-KEY: <SECRET_KEY>
```

**Response:** `HTTP 200` jika server aktif.

---

## Mekanisme Queue Offline

Data tersimpan di SD card dalam format CSV saat WiFi tidak tersedia atau saat server tidak merespons.

### Struktur File

```
/queue_0.csv     ← file aktif (maks 25 baris)
/queue_1.csv     ← file berikutnya jika queue_0 penuh
...
/queue_1999.csv  ← file terakhir
```

**Isi file CSV:**
```
rfid,timestamp,device_id,unix_time
0001234567,2025-12-01 08:23:11,ESP32_A1B2,1733030591
FP_00003,2025-12-01 08:24:05,ESP32_A1B2,1733030645
```

### Mekanisme Sync

1. `chunkedSync()` berjalan di latar belakang setiap **5 menit** selama WiFi terhubung.
2. Maksimal **5 file** diproses per siklus untuk menghindari blocking.
3. Setiap siklus dibatasi **15 detik** agar tidak mengganggu scan.
4. File yang berhasil terkirim (HTTP 200) **langsung dihapus** dari SD card.
5. File yang gagal **dipertahankan** dan dicoba lagi di siklus berikutnya.
6. Data lebih dari **30 hari** (`MAX_OFFLINE_AGE`) dilewati saat sync.
7. Saat WiFi reconnect, sync langsung dijalankan tanpa menunggu interval.

**Kapasitas:**

| Parameter | Nilai |
|:---|:---|
| Record per file | 25 |
| Maksimal file | 2.000 |
| Total kapasitas | **50.000 record** |
| Retensi data | 30 hari |

---

## Power Consumption

### Konsumsi Per Komponen (Estimasi)

| Komponen | Mode Aktif | Mode Sleep/OFF |
|:---|:---|:---|
| ESP32-C3 | ~80 mA | ~10 μA (deep sleep) |
| WiFi TX | ~170 mA | — |
| OLED SSD1306 | ~20 mA | ~0 mA |
| RFID RC522 | ~13 mA | ~10 μA |
| SD Card | ~50 mA | ~100 μA |
| Buzzer | ~30 mA | 0 mA |

### Penghematan dengan OLED Auto Dim

| | Tanpa Auto Dim | Dengan Auto Dim |
|:---|:---|:---|
| Waktu OLED ON per hari | 13 jam (05:00–18:00) | 7 jam (05:00–08:00 + 14:00–18:00) |
| Penghematan per hari | — | 6 jam × 20 mA = **120 mAh** |
| Persentase penghematan | — | **~25%** dari konsumsi display |
| Perpanjangan umur OLED | — | **~30–40%** |

---

## Troubleshooting

### OLED tidak mati pada jam yang ditentukan
- Pastikan waktu sudah tersinkronisasi NTP (cek tampilan jam di OLED).
- Periksa nilai `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR`.
- Restart perangkat.

### OLED tidak menyala saat kartu di-tap
- Pastikan `handleRFIDScan()` / `handleFingerprintScan()` memanggil `turnOnOLED()` sebelum `showOLED()`.
- Periksa variabel `oledIsOn`.

### Fingerprint tidak terdeteksi (v3.0.0)
- Cek wiring: TX ZW101 → GPIO 2, RX ZW101 → GPIO 21.
- Pastikan tegangan ZW101 menggunakan **3.3V**, bukan 5V.
- Buka Serial Monitor dan lihat apakah ada log `[FP] ZW101 OK`.
- Coba `FPCOUNT` untuk memverifikasi komunikasi.

### Sidik jari tidak dikenali saat absensi
- Pastikan template sudah dienroll dengan `ENROLL <id>`.
- Enroll ulang jika kualitas gambar buruk (gunakan `FPDEL <id>` lalu `ENROLL <id>` lagi).
- Pastikan permukaan sensor bersih.

### SD Card tidak terdeteksi
- Pastikan menggunakan library **SdFat** (bukan library `SD` bawaan Arduino).
- Format SD card ke FAT32.
- Cek kecepatan SPI: coba turunkan dari `SD_SCK_MHZ(10)` ke `SD_SCK_MHZ(4)`.
- Periksa koneksi CS ke GPIO 1.

### WiFi sering disconnect
- Periksa RSSI yang ditampilkan saat startup — idealnya > -70 dBm.
- Pastikan power supply stabil minimal **5V 1A**.
- Periksa interferensi di frekuensi 2.4 GHz.

### Sync lambat atau terblokir
- Pastikan tidak ada `showOLED()` atau `playTone()` di dalam `chunkedSync()`.
- Periksa koneksi internet dan respons API server.

### Data tidak muncul di server
- Verifikasi `API_BASE_URL` dan `API_SECRET_KEY` sudah benar.
- Pastikan endpoint `/api/presensi/sync-bulk` merespons HTTP 200.
- Cek format payload JSON yang dikirim sesuai spesifikasi API.

---

## Changelog

### v3.0.0 — RFID + Fingerprint *(Februari 2026)*
- **TAMBAH:** Dukungan fingerprint HLK-ZW101 sebagai alternatif RFID
- **TAMBAH:** `handleFingerprintScan()` — alur identik dengan `handleRFIDScan()`
- **TAMBAH:** `handleFingerprintEnroll()` via Serial Monitor
- **TAMBAH:** Format ID fingerprint `FP_XXXXX` kompatibel dengan field `rfid` di API
- **TAMBAH:** Enroll, hapus, dan manajemen template via Serial Monitor
- **TAMBAH:** Deteksi otomatis ZW101 — fallback ke RFID saja jika tidak terhubung
- **TAMBAH:** Protokol ZW101 native (tanpa library eksternal)
- **UBAH:** `lastUID` diperbesar dari 11 ke 16 karakter untuk menampung format `FP_XXXXX`
- **UBAH:** OLED standby menampilkan `"KARTU / JARI"` jika ZW101 terdeteksi

### v2.2.4 — OLED Auto Dim *(Desember 2025)*
- **TAMBAH:** Scheduled Display Control — OLED mati 08:00, nyala 14:00
- **TAMBAH:** Smart Wake-up — display menyala sementara saat ada tap, lalu kembali mati
- **TAMBAH:** Flag `oledIsOn` untuk tracking status display
- **TAMBAH:** `turnOnOLED()`, `turnOffOLED()`, `checkOLEDSchedule()`
- **HASIL:** Pengurangan konsumsi daya ~25%, perpanjangan umur OLED ~30–40%

### v2.2.3 — WiFi Signal Optimization *(Desember 2025)*
- **TAMBAH:** TX Power maksimum 19.5 dBm
- **TAMBAH:** Modem Sleep Mode `WIFI_PS_MAX_MODEM`
- **TAMBAH:** Koneksi ke AP dengan sinyal terkuat `WIFI_CONNECT_AP_BY_SIGNAL`
- **TAMBAH:** Persistent mode dan auto-reconnect
- **TAMBAH:** Tampilan RSSI (dBm) saat startup
- **UBAH:** `MAX_RECORDS_PER_FILE` → 25, `MAX_QUEUE_FILES` → 2000
- **UBAH:** `DISPLAY_UPDATE_INTERVAL` → 1000ms

### v2.2.2 — Background Operations *(Desember 2025)*
- **TAMBAH:** Background WiFi Reconnect setiap 5 menit
- **TAMBAH:** Background Bulk Sync tanpa feedback visual/audio
- **TAMBAH:** Dual process flags `isWritingToQueue` dan `isReadingQueue`
- **HASIL:** Tapping RFID tidak pernah terblokir oleh operasi network

### v2.2.1 *(Desember 2025)*
- Penambahan Fitur Autoconnect WiFi
- Perbaikan Bug v2.2.0

### v2.2.0 *(Desember 2025)*
- Migrasi dari library `SD` ke `SdFat`
- Dukungan kartu memori legacy (SD, SDHC, FAT16/FAT32)
- Optimasi buffer memori
- Perbaikan visual interface

### v2.1.0
- Sistem antrian multi-file

### v2.0.0
- Mode offline dasar (single CSV)

### v1.0.0
- Rilis awal (Online only, tanpa offline support)

---

## Deployment Recommendations

### Kantor Jam Kerja 08:00–17:00
Gunakan setting default. OLED mati 08:00–14:00 saat jam kerja berlangsung, dan menyala sementara setiap ada yang absen.

### Sekolah Jam 07:00–15:00
Ubah jadwal dim ke 10:00–12:00 agar display aktif saat siswa datang (07:00–08:00) dan pulang (14:00–15:00).

```cpp
const int OLED_DIM_START_HOUR = 10;
const int OLED_DIM_END_HOUR   = 12;
```

### Fasilitas 24 Jam / Shift Malam
Ubah jadwal dim ke jam traffic rendah, contoh:
```cpp
const int OLED_DIM_START_HOUR = 2;
const int OLED_DIM_END_HOUR   = 5;
```

### Outdoor / Solar Power
Maksimalkan auto dim, contoh dim 09:00–16:00:
```cpp
const int OLED_DIM_START_HOUR = 9;
const int OLED_DIM_END_HOUR   = 16;
```

---

## Lisensi

Hak Cipta 2025 - 2026 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
