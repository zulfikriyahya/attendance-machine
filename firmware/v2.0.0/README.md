# Catatan Rilis Firmware v2.0.0 (Offline Capable)

- **Versi:** 2.0.0 (Offline Storage)
- **Tanggal Rilis:** Desember 2025
- **Perangkat Target:** ESP32-C3 Super Mini
- **Penulis:** Yahya Zulfikri

## Ringkasan

Versi 2.0.0 adalah pembaruan besar (major update) yang mentransformasi Sistem Presensi Pintar dari sistem yang sepenuhnya bergantung pada internet (_online-only_) menjadi sistem _Hybrid_. Pembaruan ini memperkenalkan dukungan modul SD Card untuk menyimpan data kehadiran ketika koneksi internet terputus atau server tidak dapat dijangkau. Sistem kini memiliki kemampuan _Self-Healing_ dengan melakukan sinkronisasi data otomatis saat koneksi kembali stabil.

## Fitur Baru dan Perubahan

### Penyimpanan & Mode Offline (Fitur Utama)

- **Penyimpanan Lokal (SD Card):** Jika WiFi terputus atau API _down_, data presensi (RFID, Timestamp, Device ID) otomatis disimpan ke file CSV di SD Card.
- **Pencegahan Duplikasi Data:** Sistem membaca data historis di SD Card untuk mencegah pencatatan ganda (_double tapping_) untuk kartu yang sama dalam kurun waktu 1 jam terakhir.
- **Identitas Perangkat Unik:** Setiap perangkat kini menghasilkan `device_id` berbasis MAC Address untuk pelacakan sumber data saat sinkronisasi.

### Sinkronisasi Otomatis (Auto-Sync)

- **Background Sync:** Sistem memeriksa ketersediaan internet dan keberadaan data offline setiap 60 detik.
- **Pengiriman Massal (Bulk Upload):** Data offline dikirim dalam _batch_ (maksimal 50 data per paket) ke endpoint `/api/presensi/sync-bulk` untuk efisiensi bandwidth.
- **Pembersihan Otomatis:** Data pada SD Card hanya akan dihapus setelah server mengonfirmasi penerimaan data (HTTP 200 OK), menjamin integritas data (tidak ada data hilang).

### Perangkat Keras & Pinout

- **SPI Bus Bersama:** Implementasi komunikasi SPI yang membagi jalur data (SCK, MOSI, MISO) antara modul RFID RC522 dan modul SD Card dengan jalur _Chip Select_ (CS) yang berbeda.

### Antarmuka (UI/UX)

- **Indikator Status:** Tampilan OLED kini menampilkan status `ONLINE` atau `OFFLINE` di pojok kiri atas.
- **Counter Pending:** Menampilkan jumlah data yang belum tersinkronisasi (misal: `P:15`) di pojok kanan atas.
- **Pesan Interaktif:** Notifikasi spesifik seperti "DATA TERSIMPAN OFFLINE" atau "DUPLIKAT! TAP < 1 JAM".

## Detail Teknis

### Diagram Koneksi Perangkat Keras

Sistem menggunakan **ESP32-C3 Super Mini**. Perhatikan bahwa jalur SPI digunakan bersama (Shared Bus).

| Komponen       | Pin ESP32-C3 | Fungsi   | Catatan                 |
| :------------- | :----------- | :------- | :---------------------- |
| **RFID RC522** | GPIO 7       | SDA / SS | Chip Select RFID        |
|                | GPIO 4       | SCK      | Shared SPI Clock        |
|                | GPIO 6       | MOSI     | Shared SPI MOSI         |
|                | GPIO 5       | MISO     | Shared SPI MISO         |
|                | GPIO 3       | RST      | Reset                   |
| **SD Card**    | **GPIO 1**   | **CS**   | **Chip Select SD Card** |
|                | GPIO 4       | SCK      | Shared SPI Clock        |
|                | GPIO 6       | MOSI     | Shared SPI MOSI         |
|                | GPIO 5       | MISO     | Shared SPI MISO         |
| **OLED**       | GPIO 8       | SDA      | I2C Data                |
|                | GPIO 9       | SCL      | I2C Clock               |
| **Buzzer**     | GPIO 10      | (+)      | Output Suara            |

> **PERINGATAN:** GPIO 1 (CS SD Card) pada beberapa board ESP32-C3 mungkin terhubung ke LED internal atau jalur Boot. Pastikan SD Card module memiliki _pull-up resistor_ yang tepat agar tidak mengganggu proses booting.

### Struktur Data CSV (SD Card)

File disimpan dengan nama `/presensi.csv` dengan format berikut:

```csv
rfid,timestamp,device_id,unix_time
1234567890,2025-12-07 08:00:00,ESP32_A1B2,1733562000
```

- **Retensi Data:** Data offline yang usianya lebih dari 30 hari (2.592.000 detik) tidak akan diikutsertakan dalam proses sinkronisasi untuk menjaga relevansi data.

### Endpoint API Baru

Versi 2.0.0 memerlukan backend yang mendukung endpoint baru:

1.  **POST** `/api/presensi/sync-bulk`
    - Menerima array JSON berisi multiple data presensi.
    - Header: `X-API-KEY`.

## Instalasi dan Kompilasi

### Dependensi Pustaka

Pastikan pustaka berikut terinstal di Arduino IDE:

1.  **ArduinoJson** (v6.x)
2.  **Adafruit SSD1306** (v2.5.x)
3.  **Adafruit GFX Library**
4.  **MFRC522** (v1.4.x)
5.  **SD** (Built-in ESP32)
6.  **FS** (Built-in ESP32)

### Langkah Pembaruan

1.  Pastikan perkabelan perangkat keras sudah ditambahkan modul SD Card sesuai tabel pinout di atas.
2.  Format SD Card ke **FAT32** sebelum dimasukkan.
3.  Buka `main.ino`, sesuaikan `WIFI_SSID`, `WIFI_PASSWORD`, `API_BASE_URL`, dan `API_SECRET_KEY`.
4.  Upload sketch menggunakan pengaturan "USB CDC On Boot: Enabled" (opsional untuk debugging).

## Batasan yang Diketahui (Known Issues)

1.  **Timestamp saat Offline:** Jika perangkat mati total (_hard reset_) dan dinyalakan kembali dalam keadaan tanpa internet, waktu sistem mungkin tidak akurat karena ESP32 tidak memiliki baterai RTC dedicated. Sistem akan mencoba menggunakan estimasi waktu terakhir yang tersimpan di memori RTC internal, namun akurasinya bergantung pada durasi perangkat mati.
2.  **Konflik SPI:** Pastikan kualitas kabel jumper baik. Kabel yang terlalu panjang pada jalur SPI (SCK, MISO, MOSI) yang dibagi dua perangkat dapat menyebabkan kegagalan inisialisasi SD Card atau RFID.
3.  **Booting:** Jika SD Card tidak terdeteksi saat _startup_, sistem akan otomatis masuk ke mode "ONLINE SAJA" dan fitur offline dimatikan hingga _restart_ berikutnya.
