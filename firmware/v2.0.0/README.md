# Sistem Presensi Pintar Hibrida (Online/Offline) Berbasis ESP32-C3

## Deskripsi Proyek

Sistem ini adalah solusi presensi kehadiran berbasis _Internet of Things_ (IoT) yang ditingkatkan untuk lingkungan dengan konektivitas jaringan yang tidak stabil. Menggunakan mikrokontroler ESP32-C3 Super Mini, sistem ini mengintegrasikan pembacaan RFID, antarmuka visual OLED, dan penyimpanan lokal berbasis SD Card.

Versi 2.0.0 memperkenalkan arsitektur hibrida yang memprioritaskan integritas data. Sistem secara otomatis beralih ke penyimpanan lokal (CSV pada SD Card) saat koneksi server terputus dan melakukan sinkronisasi massal (_bulk synchronization_) saat koneksi pulih.

## Fitur Utama (Versi 2.0.0)

### 1. Toleransi Kegagalan Jaringan (_Network Fault Tolerance_)

- **Mode Operasi Ganda:** Sistem beroperasi dalam mode _Online_ (langsung ke API) atau _Offline_ (simpan ke SD Card) secara otomatis berdasarkan status konektivitas.
- **Penyimpanan Lokal:** Data kehadiran disimpan dalam format CSV pada SD Card saat API tidak dapat dijangkau.

### 2. Sinkronisasi Data Cerdas

- **Auto-Sync:** Mekanisme latar belakang yang berjalan setiap 60 detik untuk memeriksa dan mengunggah data tertunda (_pending_) ke server.
- **Validasi Kedaluwarsa:** Sistem hanya menyinkronkan data yang valid (usia data < 30 hari) untuk menjaga relevansi basis data.
- **Pembersihan Otomatis:** Data pada SD Card dihapus hanya setelah server memberikan konfirmasi sukses (HTTP 200), mencegah duplikasi atau kehilangan data.

### 3. Integritas Data

- **Pencegahan Duplikasi Lokal:** Algoritma internal mencegah pencatatan kartu yang sama dalam interval waktu 1 jam (dapat dikonfigurasi) saat mode offline.
- **Identitas Perangkat:** Setiap catatan presensi menyertakan `device_id` unik berbasis MAC Address untuk pelacakan aset.

### 4. Efisiensi Daya

- **Deep Sleep Terjadwal:** Mode hemat daya ekstrem aktif otomatis pada pukul 18:00 hingga 05:00 waktu setempat.

## Spesifikasi Perangkat Keras dan Pinout

Perangkat lunak ini dikonfigurasi untuk **ESP32-C3 Super Mini**. Perhatikan bahwa modul RFID dan SD Card berbagi bus SPI yang sama namun menggunakan pin _Chip Select_ (CS) yang berbeda.

| Komponen           | Pin Modul | Pin ESP32-C3 (GPIO) | Protokol             |
| :----------------- | :-------- | :------------------ | :------------------- |
| **RFID RC522**     | SDA (SS)  | GPIO 7              | SPI (Chip Select 1)  |
|                    | SCK       | GPIO 4              | SPI (Clock - Shared) |
|                    | MOSI      | GPIO 6              | SPI (MOSI - Shared)  |
|                    | MISO      | GPIO 5              | SPI (MISO - Shared)  |
|                    | RST       | GPIO 3              | Reset                |
| **SD Card Module** | CS        | GPIO 1              | SPI (Chip Select 2)  |
|                    | SCK       | GPIO 4              | SPI (Clock - Shared) |
|                    | MOSI      | GPIO 6              | SPI (MOSI - Shared)  |
|                    | MISO      | GPIO 5              | SPI (MISO - Shared)  |
| **OLED SSD1306**   | SDA       | GPIO 8              | I2C (Data)           |
|                    | SCL       | GPIO 9              | I2C (Clock)          |
| **Buzzer**         | VCC / (+) | GPIO 10             | Output Digital/PWM   |

> **Perhatian:** Pastikan integritas sinyal pada bus SPI (GPIO 4, 5, 6) terjaga saat menghubungkan dua perangkat budak (RFID dan SD Card).

## Diagram Koneksi

![Diagram](./diagram.svg)

## Prasyarat Perangkat Lunak

Pastikan pustaka berikut terinstal pada lingkungan pengembangan (Arduino IDE/PlatformIO):

1.  **MFRC522** (Komunikasi RFID)
2.  **Adafruit SSD1306** & **Adafruit GFX** (Tampilan OLED)
3.  **ArduinoJson** (Serialisasi data API)
4.  **SD** & **FS** (Manajemen sistem berkas - Bawaan Core ESP32)
5.  **WiFi**, **HTTPClient**, **Wire**, **SPI** (Bawaan Core ESP32)

## Konfigurasi Sistem

Lakukan konfigurasi pada variabel global di awal kode sumber sebelum kompilasi:

```cpp
// 1. Kredensial Jaringan
const char WIFI_SSID_1[] PROGMEM     = "SSID_UTAMA";
const char WIFI_PASSWORD_1[] PROGMEM = "PASS_UTAMA";

// 2. Endpoint API
const char API_BASE_URL[] PROGMEM    = "https://api.domain.com";
const char API_SECRET_KEY[] PROGMEM  = "KUNCI_AUTENTIKASI";

// 3. Parameter Offline
const unsigned long SYNC_INTERVAL    = 60000;  // Interval sync (ms)
const unsigned long MIN_REPEAT_INTERVAL = 3600; // Debounce offline (detik)
```

## Spesifikasi API Backend

Sistem membutuhkan dua titik akhir (_endpoint_) API untuk menangani presensi _real-time_ dan sinkronisasi massal.

### 1. Presensi Real-time

- **URL:** `/api/presensi/rfid`
- **Metode:** `POST`
- **Header:** `X-API-KEY`, `Content-Type: application/json`
- **Body Request:**
  ```json
  {
    "rfid": "1234567890",
    "timestamp": "2025-12-07 08:00:00",
    "device_id": "ESP32_A1B2"
  }
  ```

### 2. Sinkronisasi Massal (Bulk Sync)

- **URL:** `/api/presensi/sync-bulk`
- **Metode:** `POST`
- **Header:** `X-API-KEY`, `Content-Type: application/json`
- **Body Request:**
  ```json
  {
    "data": [
      {
        "rfid": "1234567890",
        "timestamp": "2025-12-07 07:55:00",
        "device_id": "ESP32_A1B2",
        "sync_mode": true
      },
      {
        "rfid": "0987654321",
        "timestamp": "2025-12-07 07:56:00",
        "device_id": "ESP32_A1B2",
        "sync_mode": true
      }
    ]
  }
  ```

## Struktur Data Lokal (CSV)

Jika sistem beroperasi dalam mode offline, data disimpan pada SD Card dengan nama berkas `/presensi.csv`. Format penyimpanan adalah sebagai berikut:

```csv
rfid,timestamp,device_id,unix_time
1234567890,2025-12-07 08:00:00,ESP32_A1B2,1765094400
0987654321,2025-12-07 08:05:00,ESP32_A1B2,1765094700
```

Kolom `unix_time` digunakan internal oleh sistem untuk validasi kedaluwarsa data dan perhitungan interval duplikasi.

## Indikator Status

Layar OLED menampilkan informasi status kritis di baris paling atas:

- **Kiri Atas:** Status Konektivitas (`ONLINE` atau `OFFLINE`).
- **Kanan Atas:** Indikator Data Tertunda (`P:X` dimana X adalah jumlah data di SD Card yang belum tersinkronisasi).
- **Ikon Sinyal:** Bar kekuatan sinyal WiFi (RSSI).

## Riwayat Versi

- **v2.0.0 (Desember 2025):** Implementasi penyimpanan offline (SD Card), sinkronisasi massal, dan manajemen integritas data.
- **v1.0.0 (Juli 2025):** Rilis awal dengan fungsionalitas dasar presensi online dan deep sleep.

## Lisensi

Hak Cipta © 2025. Kode sumber ini didistribusikan untuk tujuan pengembangan internal dan implementasi sistem presensi korporat.
