# Sistem Presensi Pintar (RFID)

Sistem presensi berbasis _Internet of Things_ (IoT) yang dirancang untuk perangkat ESP32-C3 Super Mini. Sistem ini menggunakan teknologi RFID untuk identifikasi, layar OLED untuk antarmuka pengguna, dan komunikasi HTTPS aman untuk pengiriman data ke server.

**Repositori:** https://github.com/zulfikriyahya/attendance-machine.git

## Deskripsi Proyek

Proyek ini adalah solusi perangkat tegar (firmware) untuk mesin absensi mandiri. Perangkat akan membaca kartu RFID (Mifare), memvalidasi data, dan mengirimkannya ke server backend melalui REST API. Sistem dilengkapi dengan fitur manajemen daya otomatis (Deep Sleep) dan sinkronisasi waktu jaringan (NTP) untuk memastikan akurasi data.

**Identitas Proyek:**

- **Penulis:** Yahya Zulfikri (ZedLabs)
- **Versi:** 1.0.0 (Stabil)
- **Tanggal Rilis:** Desember 2025

## Fitur Utama

1.  **Konektivitas Aman:** Menggunakan protokol HTTPS dengan autentikasi `X-API-KEY` untuk komunikasi data.
2.  **Manajemen Waktu Otomatis:** Sinkronisasi waktu menggunakan NTP (Network Time Protocol) dengan dukungan multi-server (pool.ntp.org, google, nist, dll) dan mekanisme _fallback_ otomatis.
3.  **Mode Hemat Daya (Deep Sleep):** Perangkat otomatis masuk ke mode tidur dalam pada jam 18:00 hingga 05:00 untuk menghemat energi.
4.  **Redundansi Jaringan:** Mendukung konfigurasi dua SSID WiFi yang berbeda dan akan mencoba beralih secara otomatis jika koneksi utama gagal.
5.  **Antarmuka Interaktif:** Menampilkan status koneksi, kekuatan sinyal WiFi, animasi startup, dan umpan balik visual pada layar OLED.
6.  **Indikator Audio:** Umpan balik suara (Buzzer) untuk status sukses, gagal, atau notifikasi sistem.

## Spesifikasi Perangkat Keras

Sistem ini dirancang khusus untuk mikrokontroler **ESP32-C3 Super Mini**.

### Pemetaan Pin (Wiring Diagram)

| Komponen         | Pin Perangkat | Pin ESP32-C3 (GPIO) | Keterangan  |
| :--------------- | :------------ | :------------------ | :---------- |
| **RFID RC522**   | SDA (SS)      | GPIO 7              | Chip Select |
|                  | SCK           | GPIO 4              | SPI Clock   |
|                  | MOSI          | GPIO 6              | SPI MOSI    |
|                  | MISO          | GPIO 5              | SPI MISO    |
|                  | RST           | GPIO 3              | Reset       |
|                  | VCC           | 3.3V                | Daya        |
|                  | GND           | GND                 | Ground      |
| **OLED SSD1306** | SDA           | GPIO 8              | I2C Data    |
|                  | SCL           | GPIO 9              | I2C Clock   |
|                  | VCC           | 3.3V                | Daya        |
|                  | GND           | GND                 | Ground      |
| **Buzzer**       | POS (+)       | GPIO 10             | Output PWM  |
|                  | NEG (-)       | GND                 | Ground      |

## Struktur Direktori

```text
.
├── doc
│   └── README.md          # Dokumentasi tambahan
├── firmware
│   └── v1.0.0
│       ├── diagram.svg    # Diagram skematik/wiring
│       ├── LICENSE        # Lisensi firmware
│       ├── main.ino       # Kode sumber utama
│       └── README.md      # Catatan rilis versi
├── LICENSE                # Lisensi proyek utama
└── README.md              # File ini
```

## Instalasi dan Konfigurasi

### Persyaratan Perangkat Lunak

- Arduino IDE
- Board Manager: ESP32 by Espressif Systems

### Pustaka yang Dibutuhkan

Instal pustaka berikut melalui Library Manager di Arduino IDE:

1.  **MFRC522** (oleh GithubCommunity)
2.  **Adafruit SSD1306** (oleh Adafruit)
3.  **Adafruit GFX Library** (oleh Adafruit)
4.  **ArduinoJson** (oleh Benoit Blanchon)

### Konfigurasi Kode

Sebelum mengunggah kode, sesuaikan bagian konfigurasi pada file `firmware/v1.0.0/main.ino`:

**1. Pengaturan WiFi**

```cpp
const char WIFI_SSID_1[] PROGMEM     = "Nama_WiFi_Utama";
const char WIFI_PASSWORD_1[] PROGMEM = "Password_Utama";
const char WIFI_SSID_2[] PROGMEM     = "Nama_WiFi_Cadangan";
const char WIFI_PASSWORD_2[] PROGMEM = "Password_Cadangan";
```

**2. Pengaturan API Server**

```cpp
const char API_BASE_URL[] PROGMEM    = "https://domain-anda.com";
const char API_SECRET_KEY[] PROGMEM  = "Kunci_API_Rahasia_Anda";
```

**3. Pengaturan Zona Waktu**
Sesuaikan `GMT_OFFSET_SEC` jika Anda berada di luar zona waktu WIB (GMT+7).

```cpp
const long GMT_OFFSET_SEC = 25200; // 25200 detik = 7 Jam
```

## Spesifikasi API Backend

Perangkat mengharapkan endpoint API berikut tersedia di server:

### 1. Pengecekan Koneksi (Ping)

- **Metode:** `GET`
- **URL:** `/api/presensi/ping`
- **Header:** `X-API-KEY: [API_SECRET_KEY]`
- **Respon Sukses:** HTTP 200 OK

### 2. Pengiriman Data RFID

- **Metode:** `POST`
- **URL:** `/api/presensi/rfid`
- **Header:**
  - `Content-Type: application/json`
  - `X-API-KEY: [API_SECRET_KEY]`
- **Body:**
  ```json
  {
    "rfid": "1234567890"
  }
  ```
- **Respon Sukses (JSON):**
  ```json
  {
    "message": "BERHASIL",
    "data": {
      "nama": "Nama Pengguna",
      "waktu": "08:00:00",
      "status": "MASUK"
    }
  }
  ```

## Cara Penggunaan

1.  Hidupkan perangkat.
2.  Tunggu proses inisialisasi (logo startup, koneksi WiFi, dan sinkronisasi waktu).
3.  Pastikan layar menampilkan pesan "TEMPELKAN KARTU" dan indikator sinyal WiFi muncul.
4.  Tempelkan kartu RFID pada pembaca.
5.  Perangkat akan berbunyi dan menampilkan nama serta status kehadiran jika berhasil.

## Lisensi

Proyek ini didistribusikan di bawah lisensi yang tercantum dalam file `LICENSE`. Silakan merujuk ke file tersebut untuk informasi hak cipta dan penggunaan.

---

Copyright (c) 2025 Yahya Zulfikri - ZedLabs
