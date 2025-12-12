# Sistem Presensi RFID - Queue System v2.2.1 (Ultimate)

## Deskripsi Proyek

Sistem Presensi Pintar v2.2.1 adalah iterasi mutakhir dari perangkat presensi berbasis IoT yang dibangun di atas mikrokontroler ESP32-C3 Super Mini. Versi ini, yang diberi label "Ultimate", difokuskan pada optimalisasi manajemen memori (RAM), stabilitas _Input/Output_ (I/O), dan kompatibilitas perangkat keras penyimpanan.

Perubahan paling signifikan pada versi ini adalah migrasi dari pustaka standar `SD` ke `SdFat`, memungkinkan dukungan untuk kartu SD kapasitas rendah (Legacy 128MB) hingga kapasitas tinggi dengan efisiensi _buffer_ yang superior. Sistem ini dirancang untuk operasional jangka panjang tanpa pengawasan (_unattended operation_) dengan mekanisme pemulihan kesalahan mandiri.

## Fitur Teknis (Versi 2.2.1)

### 1. Manajemen Penyimpanan Tingkat Lanjut

- **Integrasi SdFat:** Menggunakan pustaka `SdFat` untuk akses sistem berkas yang lebih cepat dan stabil. Mendukung format FAT16 dan FAT32 secara optimal.
- **Buffered I/O:** Implementasi pembacaan dan penulisan berkas menggunakan buffer memori statis untuk meminimalkan fragmentasi _heap_ pada ESP32-C3.
- **Sistem Antrean Skala Besar:** Mendukung hingga 1.000 berkas antrean (`queue_0.csv` s.d `queue_999.csv`), memungkinkan penyimpanan lokal hingga 50.000 data presensi saat offline.

### 2. Validasi Integritas Data

- **Sliding Window Duplicate Check:** Algoritma pencegahan duplikasi data tidak memindai seluruh penyimpanan, melainkan menggunakan metode _sliding window_ (memeriksa berkas antrean aktif dan 2 berkas sebelumnya). Ini menjaga performa tetap cepat (O(1)) meskipun jumlah data tersimpan sangat besar.
- **Validasi Waktu Ketat:** Sinkronisasi waktu NTP dilakukan secara berkala (setiap 1 jam) untuk memastikan stempel waktu (_timestamp_) tetap akurat.

### 3. Stabilitas Sistem

- **Penanganan Kesalahan Fatal:** Mekanisme _Fail-Safe_ yang me-restart perangkat secara otomatis jika terjadi kegagalan kritis pada modul RFID atau inisialisasi sistem berkas.
- **Indikator Visual Presisi:** Umpan balik visual pada OLED yang lebih responsif, termasuk bilah progres (_progress bar_) dan status antrean _real-time_.

## Spesifikasi Perangkat Keras dan Pinout

Sistem menggunakan bus SPI bersama (_Shared SPI Bus_) untuk komunikasi dengan modul RFID dan SD Card. Manajemen lalu lintas data diatur melalui manajemen sinyal _Chip Select_ (CS) yang ketat.

| Komponen       | Pin Modul | Pin ESP32-C3 | Protokol | Catatan                        |
| :------------- | :-------- | :----------- | :------- | :----------------------------- |
| **Bus SPI**    | SCK       | GPIO 4       | SPI      | Jalur Clock (Shared)           |
|                | MOSI      | GPIO 6       | SPI      | Jalur Data Master Out (Shared) |
|                | MISO      | GPIO 5       | SPI      | Jalur Data Master In (Shared)  |
| **RFID RC522** | SDA (SS)  | GPIO 7       | SPI      | Chip Select RFID               |
|                | RST       | GPIO 3       | Digital  | Reset Hardwrae                 |
| **SD Card**    | CS        | GPIO 1       | SPI      | Chip Select SD Card            |
| **OLED**       | SDA       | GPIO 8       | I2C      | Data Display                   |
|                | SCL       | GPIO 9       | I2C      | Clock Display                  |
| **Buzzer**     | (+)       | GPIO 10      | PWM      | Indikator Audio                |

> **Penting:** Pastikan implementasi perangkat keras mendukung penggunaan GPIO 4, 5, dan 6 secara paralel untuk dua perangkat SPI berbeda.

## Prasyarat Instalasi Perangkat Lunak

Versi ini memiliki dependensi pustaka yang berbeda dari versi sebelumnya. Pastikan pustaka berikut terinstal:

1.  **SdFat** oleh Bill Greiman (Versi 2.x): **Wajib**. Menggantikan pustaka `SD` bawaan Arduino. Memberikan kinerja I/O yang jauh lebih baik.
2.  **MFRC522** (GithubCommunity): Driver RFID.
3.  **Adafruit SSD1306** & **Adafruit GFX**: Driver tampilan OLED.
4.  **ArduinoJson**: Serialisasi JSON.
5.  **WiFi**, **HTTPClient**, **Wire**, **SPI**: Pustaka inti ESP32.

## Konfigurasi Sistem

Parameter operasional utama didefinisikan pada bagian awal kode sumber:

```cpp
// Konfigurasi Antrean
const int MAX_RECORDS_PER_FILE      = 50;     // Batas aman untuk RAM ESP32-C3
const int MAX_QUEUE_FILES           = 1000;   // Kapasitas total 50.000 record
const unsigned long SYNC_INTERVAL   = 60000;  // Sinkronisasi setiap 1 menit

// Konfigurasi Validasi
const unsigned long MIN_REPEAT_INTERVAL = 1800; // Debounce 30 menit untuk kartu sama
```

## Mekanisme Kerja Antrean (Queue Logic)

1.  **Segmentasi:** Data disimpan dalam berkas kecil (`queue_N.csv`) berisi maksimal 50 baris untuk mencegah _buffer overflow_.
2.  **Rotasi:** Saat berkas `queue_N` penuh, sistem secara otomatis membuat `queue_N+1`.
3.  **Sinkronisasi:**
    - Sistem mencari berkas antrean yang ada secara sekuensial.
    - Data dibaca, diparsing ke JSON, dan dikirim ke server.
    - Jika server merespons HTTP 200, berkas `queue_N` dihapus secara fisik dari SD Card.
    - Jika gagal, berkas dipertahankan untuk percobaan berikutnya.

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

## Kompatibilitas Kartu SD

Berkat implementasi `SdFat`, sistem ini mendukung berbagai jenis kartu memori yang sering gagal pada pustaka standar:

- SDSC (Standard Capacity) <= 2GB (termasuk kartu lama 128MB/256MB).
- SDHC (High Capacity) 4GB - 32GB.
- Format sistem berkas: FAT16 dan FAT32.

## Riwayat Perubahan (Changelog)

- **v2.2.1 (Desember 2025):** Penambahan Fitur Autoconnect Wifi dan Perbaikan Bug System.
- **v2.2.0 (Desember 2025):** Migrasi ke `SdFat`, dukungan kartu memori legacy, optimasi buffer memori, dan perbaikan visual interface.
- **v2.1.0:** Pengenalan sistem antrean multi-file.
- **v2.0.0:** Implementasi mode offline dasar (single CSV).
- **v1.0.0:** Rilis awal (Online only).

## Lisensi

Hak Cipta © 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
