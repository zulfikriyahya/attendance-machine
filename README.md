# 🎓 Attendance Machine – RFID Presensi + ESP32 + Laravel + WhatsApp

Attendance Machine adalah sistem presensi otomatis berbasis **ESP32 + RFID** yang terhubung ke backend **Laravel** dan memberikan notifikasi real-time via **WhatsApp**. Dirancang untuk **pendidikan dan instansi publik**, sistem ini mendukung mode offline, hemat daya, dan sinkronisasi otomatis.

---

## 📸 Gambar Wiring (Hardware Schematic)

| Versi  | Wiring Diagram                        |
| ------ | ------------------------------------- |
| v0.1.0 | ![v0.1.0](firmware/v0.1.0/v0.1.0.svg) |
| v0.1.1 | ![v0.1.1](firmware/v0.1.1/v0.1.1.svg) |

---

## 🚀 Daftar Isi

- [Fitur Utama](#fitur-utama)
- [Perbedaan Versi Firmware](#perbedaan-versi-firmware)
- [Komponen Hardware](#komponen-hardware)
- [Cara Kerja](#cara-kerja)
- [Instalasi & Flash Firmware](#instalasi--flash-firmware)
- [Konfigurasi `config.h`](#konfigurasi-configh)
- [Integrasi Laravel API](#integrasi-laravel-api)
- [Lisensi & Kontribusi](#lisensi--kontribusi)

---

## ✨ Fitur Utama

- 🔐 **RFID Presensi** (RC522 / PN532)
- 📡 **WiFi Otomatis** – Multi SSID
- 💤 **Sleep Mode** – Hemat daya di luar jam aktif
- 💬 **Notifikasi WhatsApp** – Ke orang tua/pegawai
- 💾 **Offline Mode** – Sinkron otomatis saat online
- 📈 **Anti-Dobel Scan** (debounce + timestamp)
- 🔋 **Portable** – Bisa pakai baterai BL-5C / Li-ion / Baterai 18650
- 🖥️ **OLED Display** – Tampilkan nama, waktu, dan status
- 🔊 **Buzzer Aktif** – Feedback audio
- 🔧 **Tanpa Reset/Instalasi Ulang** – Plug & play
- 🔐 **Autentikasi API Token** untuk keamanan

---

## 🧪 Perbedaan Versi Firmware

| Versi  | Deskripsi Singkat                                     |Catatan                            |
| ------ | ----------------------------------------------------- |-----------------------------------|
| v0.1.0 | Firmware dasar: scan RFID, kirim API, tampilkan OLED  |Public Release                     |
| v0.1.1 | Tambahan: sleep mode otomatis di luar jam operasional |Public Release                     |
| v0.1.2 | Tambahan: auto-sync data offline saat online kembali  |Hanya Untuk MTs Negeri 1 Pandeglang|

---

## 🛠️ Komponen Hardware

| Komponen | Tipe / Spesifikasi                      |
| -------- | --------------------------------------- |
| MCU      | ESP32-C3 Super Mini / S3                |
| RFID     | RC522 (SPI) / PN532                     |
| Display  | OLED 0.96" (I2C SSD1306)                |
| Storage  | (Opsional) SD Card module (SPI)         |
| Buzzer   | Buzzer aktif (5V/3.3V)                  |
| Power    | USB / Baterai 3.7V BL-5C / Li-ion 18650 |

---

## ⚙️ Cara Kerja

1. Pengguna men-tap kartu RFID → UID dibaca
2. Data dikirim ke server Laravel API via WiFi
3. Jika tidak ada koneksi:
   - Data disimpan di memori sementara (JSON offline)
4. Sinkronisasi otomatis saat WiFi kembali
5. Jika berhasil:
   - OLED tampilkan nama + waktu
   - Buzzer berbunyi
   - WhatsApp terkirim via backend Laravel

---

## 🔧 Instalasi & Flash Firmware

1. Install Arduino IDE atau PlatformIO
2. Pilih board: **ESP32C3 Dev Module** / **ESP32S3**
3. Pastikan library berikut terinstall:
   - `WiFi.h`, `SPI.h`, `Wire.h`
   - `MFRC522`, `Adafruit_SSD1306`
   - `ArduinoJson`, `HTTPClient`
4. Edit `config.h` sesuai kebutuhan (SSID, API, jam kerja, dll)
5. Upload firmware ke ESP32 via USB

---

## 📝 Konfigurasi `config.h`

1. Salin `config-example.h` menjadi `config.h`
2. Isi data berikut:

```cpp
#define RST_PIN 3
#define SS_PIN 7
...
const char WS1[] PROGMEM = "ZEDLABS";                             // SSID Wifi 1
const char WS2[] PROGMEM = "ZULFIKRIYAHYA";                       // SSID Wifi 2 (Opsional)
const char WP1[] PROGMEM = "Password1";                           // Password Wifi 1
const char WP2[] PROGMEM = "Password2";                           // Password Wifi 2 (Opsional)
const char API[] PROGMEM = "https://presensi.example.sch.id/api"; // Sesuaikan dengan APP_URL pada.env
const char KEY[] PROGMEM = "SecretApi";                           // Sesuaikan dengan API_SECRET pada.env
const char NT1[] PROGMEM = "pool.ntp.org";
const char NT2[] PROGMEM = "time.google.com";
const char NT3[] PROGMEM = "id.pool.ntp.org";
const char NT4[] PROGMEM = "time.nist.gov";
const char NT5[] PROGMEM = "time.cloudflare.com";

const int WCT = 2;
const int NCT = 5;
const int NTO = 8000;
const int MNR = 2;
const int SST = 18; // Mulai Sleep Mode
const int EST = 5;  // Selesai Sleep Mode
const long GMT = 25200;
const int DST = 0;
```

---

## 🔗 Integrasi Laravel API

```
POST /api/presensi/rfid
Headers:
  X-API-KEY: [API_SECRET]
Body JSON:
  {
    "rfid": "1234567890"
  }
Response:
  {
    "message": "Presensi Berhasil",
    "data": {
      "nama": "Yahya Zulfikri",
      "waktu": "2025-07-17 07:30",
      "status": "Hadir"
    }
  }
```

Pastikan backend Laravel kamu:

- Memverifikasi API key
- Mencatat log offline → online
- Mengirim WhatsApp

---

## 🧠 Backend Laravel (Terpisah)

Sistem backend mendukung:

- CRUD data pegawai/siswa
- Laporan PDF/Excel berdasarkan filter
- Dashboard admin (FilamentPHP)
- Tanda tangan elektronik sah
- Monitoring status mesin (ping, log, baterai)
- WhatsApp notification real-time

---

## ✅ Lisensi & Kontribusi

Proyek ini bersifat **open-source** dengan lisensi [MIT](LICENSE).
Kontribusi sangat terbuka — baik dalam bentuk **kode**, **ide**, atau **laporan bug**.

---

> 📌 _Dokumen ini akan terus diperbarui sesuai perkembangan fitur dan penerapan._
