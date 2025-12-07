# Sistem Presensi Pintar Berbasis IoT (RFID & ESP32-C3)

## Deskripsi Proyek

Sistem ini adalah perangkat presensi kehadiran berbasis _Internet of Things_ (IoT) yang dirancang menggunakan mikrokontroler ESP32-C3 Super Mini. Perangkat ini memanfaatkan teknologi RFID (_Radio Frequency Identification_) untuk autentikasi pengguna dan mengirimkan data kehadiran secara _real-time_ ke server pusat melalui protokol HTTPS yang aman.

Firmware ini dikembangkan dengan fokus pada efisiensi daya, keandalan konektivitas, dan ketahanan data. Sistem dilengkapi dengan fitur manajemen daya otomatis (_Deep Sleep_), redundansi koneksi jaringan, serta mekanisme sinkronisasi waktu presisi menggunakan protokol NTP (_Network Time Protocol_).

## Spesifikasi Teknis

### Perangkat Keras

- **Unit Pemroses Utama:** ESP32-C3 Super Mini (RISC-V 32-bit Single-Core).
- **Pembaca Identitas:** Modul RFID RC522 (13.56 MHz).
- **Antarmuka Visual:** OLED Display 0.96 inci (SSD1306) resolusi 128x64 piksel, I2C.
- **Indikator Audio:** Buzzer Pasif/Aktif (Piezoelectric).

### Fitur Utama

1.  **Keamanan Transmisi Data:** Komunikasi API menggunakan HTTPS dengan autentikasi berbasis `X-API-KEY`.
2.  **Manajemen Daya Cerdas:** Mode _Deep Sleep_ terjadwal otomatis (aktif pukul 18:00 hingga 05:00 WIB) untuk penghematan energi signifikan.
3.  **Redundansi Jaringan:** Dukungan _Multi-SSID_ dengan mekanisme _failover_ otomatis jika jaringan utama tidak tersedia.
4.  **Sinkronisasi Waktu Presisi:** Menggunakan 5 server NTP berbeda dengan algoritma _fallback_ dan estimasi waktu berbasis RTC (_Real-Time Clock_) internal jika koneksi internet terputus.
5.  **Antarmuka Pengguna Interaktif:** Menampilkan status koneksi, kekuatan sinyal (RSSI), waktu, dan animasi status pemindaian pada layar OLED.

## Konfigurasi Pin (_Pinout_)

Perangkat lunak ini dikonfigurasi khusus untuk papan pengembangan **ESP32-C3 Super Mini**. Berikut adalah tabel pemetaan pin (GPIO):

| Komponen         | Pin Modul | Pin ESP32-C3 (GPIO) | Keterangan                |
| :--------------- | :-------- | :------------------ | :------------------------ |
| **RFID RC522**   | SDA (SS)  | GPIO 7              | Chip Select (SPI)         |
|                  | SCK       | GPIO 4              | Clock (SPI)               |
|                  | MOSI      | GPIO 6              | Master Out Slave In (SPI) |
|                  | MISO      | GPIO 5              | Master In Slave Out (SPI) |
|                  | RST       | GPIO 3              | Reset                     |
| **OLED SSD1306** | SDA       | GPIO 8              | Data (I2C)                |
|                  | SCL       | GPIO 9              | Clock (I2C)               |
| **Buzzer**       | VCC / (+) | GPIO 10             | Output PWM/Tone           |

> **Catatan:** Tegangan operasional seluruh periferal adalah 3.3V. Pastikan koneksi _Ground_ (GND) terhubung dengan baik.

## Diagram Koneksi

![Diagram](./diagram.svg)

## Prasyarat Instalasi

### Pustaka (_Libraries_)

Pastikan pustaka berikut telah terinstal pada Arduino IDE atau PlatformIO sebelum melakukan kompilasi:

1.  **MFRC522** oleh GithubCommunity (untuk komunikasi modul RFID).
2.  **Adafruit SSD1306** oleh Adafruit (driver layar OLED).
3.  **Adafruit GFX Library** oleh Adafruit (grafis dasar).
4.  **ArduinoJson** oleh Benoit Blanchon (parsing data JSON).
5.  **WiFi** & **HTTPClient** (bawaan paket board ESP32).

### Konfigurasi Firmware

Sebelum mengunggah kode, lakukan penyesuaian pada bagian `KONFIGURASI JARINGAN` di dalam kode sumber:

```cpp
// Kredensial WiFi (Mendukung 2 SSID untuk cadangan)
const char WIFI_SSID_1[] PROGMEM     = "NAMA_WIFI_UTAMA";
const char WIFI_SSID_2[] PROGMEM     = "NAMA_WIFI_CADANGAN";
const char WIFI_PASSWORD_1[] PROGMEM = "PASSWORD_WIFI_UTAMA";
const char WIFI_PASSWORD_2[] PROGMEM = "PASSWORD_WIFI_CADANGAN";

// Konfigurasi API Endpoint
const char API_BASE_URL[] PROGMEM    = "https://domain-server-anda.com";
const char API_SECRET_KEY[] PROGMEM  = "TOKEN_API_ANDA";
```

## Spesifikasi API Backend

Perangkat ini mengirimkan permintaan HTTP POST ke endpoint server. Pastikan backend Anda siap menerima format berikut:

- **Endpoint:** `/api/presensi/rfid`
- **Method:** `POST`
- **Headers:**
  - `Content-Type`: `application/json`
  - `X-API-KEY`: `[Nilai API_SECRET_KEY]`
- **Body Request:**
  ```json
  {
    "rfid": "1234567890"
  }
  ```
- **Ekspektasi Respons (JSON):**
  ```json
  {
    "message": "Presensi Berhasil",
    "data": {
      "nama": "Nama Pengguna",
      "waktu": "07:00:00",
      "status": "Masuk"
    }
  }
  ```

## Mekanisme Operasional

1.  **Booting:** Sistem melakukan inisialisasi I2C, SPI, dan menyambungkan ke WiFi. Jika gagal, sistem akan melakukan _restart_ otomatis.
2.  **Sinkronisasi Waktu:** Sistem menghubungi server NTP. Jika gagal, sistem menggunakan estimasi waktu dari memori RTC (jika tersedia dari sesi sebelumnya).
3.  **Siaga (Idle):** Menampilkan waktu dan status sinyal. Menunggu kartu RFID ditempelkan.
4.  **Pemindaian:** Saat kartu dideteksi, UID dibaca dan dikirim ke server.
5.  **Umpan Balik:**
    - _Sukses:_ Nada _beep_ pendek 3 kali dan pesan "BERHASIL" pada layar.
    - _Gagal:_ Nada panjang dan pesan kesalahan.
6.  **Deep Sleep:** Pada pukul 18:00, sistem secara otomatis masuk ke mode tidur dalam dan akan bangun kembali pada pukul 05:00 keesokan harinya.

## Riwayat Versi

- **v1.0.0 (Stabil)** - Rilis awal. Fitur dasar presensi, sinkronisasi NTP, dan mode hemat daya.

## Lisensi

Hak Cipta © 2025. Kode sumber ini bersifat terbuka untuk tujuan pendidikan dan pengembangan lebih lanjut.
