# Catatan Rilis Firmware v1.0.0

**Versi:** 1.0.0 (Stabil)
**Tanggal Rilis:** Desember 2025
**Perangkat Target:** ESP32-C3 Super Mini
**Penulis:** Yahya Zulfikri

## Ringkasan

Versi 1.0.0 adalah rilis produksi pertama untuk Sistem Presensi Pintar. Versi ini berfokus pada stabilitas koneksi inti, keamanan pengiriman data, dan manajemen daya dasar. Firmware ini telah diuji untuk penggunaan operasional harian dengan fitur sinkronisasi waktu otomatis dan mode hemat daya terjadwal.

## Fitur Baru dan Perubahan

### Inti Sistem

- Rilis awal sistem presensi berbasis RFID.
- Implementasi mekanisme _Watchdog_ manual melalui pengecekan status WiFi dan API berkala.
- Penanganan memori RTC untuk menyimpan waktu saat perangkat melakukan _soft restart_.

### Jaringan dan Konektivitas

- **Dukungan Dual SSID:** Sistem menyimpan dua konfigurasi WiFi dan akan mencoba beralih secara otomatis jika jaringan utama tidak tersedia.
- **Keamanan API:** Komunikasi backend menggunakan protokol HTTPS.
- **Autentikasi:** Implementasi header `X-API-KEY` pada setiap permintaan HTTP untuk keamanan endpoint.

### Manajemen Waktu

- **NTP Multi-Server:** Sinkronisasi waktu menggunakan 5 server berbeda (pool.ntp.org, google, nist, cloudflare) untuk menjamin keberhasilan sinkronisasi.
- **Estimasi Waktu Cadangan:** Jika NTP gagal total, sistem menggunakan penghitung waktu internal berbasis `millis()` yang dikalkulasi dari sinkronisasi sukses terakhir (disimpan di memori RTC).

### Manajemen Daya

- **Deep Sleep Terjadwal:** Perangkat otomatis memasuki mode tidur dalam pada pukul 18:00 dan bangun kembali pada pukul 05:00 WIB untuk menghemat daya.

### Antarmuka Pengguna (UI/UX)

- Indikator visual kekuatan sinyal WiFi (RSSI) pada layar OLED.
- Animasi _booting_ "ZEDLABS".
- Mekanisme _debounce_ (penundaan) 300ms untuk mencegah pembacaan kartu ganda yang tidak disengaja.
- Umpan balik audio (Buzzer) yang berbeda untuk kondisi: Startup, Kartu Terdeteksi, Sukses, dan Gagal.

## Detail Teknis

### Diagram Skematik v1.0.0

![Diagram Skematik v1.0.0](diagram.svg)

### Versi Pustaka (Dependensi)

Disarankan menggunakan versi pustaka berikut atau yang lebih baru saat melakukan kompilasi:

- **ArduinoJson:** v6.x
- **Adafruit SSD1306:** v2.5.x
- **MFRC522:** v1.4.x
- **ESP32 Board Definition:** v2.0.x atau v3.0.x

### Konfigurasi Pin (GPIO)

Firmware ini dikompilasi dengan konfigurasi pin keras (hardcoded) untuk ESP32-C3 Super Mini:

- **SPI (RFID):** SS(7), SCK(4), MOSI(6), MISO(5), RST(3)
- **I2C (OLED):** SDA(8), SCL(9)
- **Output:** Buzzer(10)

## Batasan yang Diketahui (Known Issues)

1.  **Tidak Ada Mode Offline:** Firmware ini belum mendukung penyimpanan data lokal (SD Card/SPIFFS). Jika WiFi terputus saat pemindaian kartu, data kehadiran tidak akan tersimpan dan pengguna diminta untuk mencoba lagi.
2.  **Akurasi Waktu Cadangan:** Jika koneksi internet terputus lebih dari 24 jam dan perangkat melakukan restart, estimasi waktu internal mungkin mengalami penyimpangan (drift) beberapa detik hingga menit karena keterbatasan kristal osilator internal ESP32.

## Instruksi Pembaruan

Karena ini adalah rilis versi pertama, instalasi dilakukan melalui flashing langsung menggunakan kabel USB-C:

1.  Buka `main.ino` di Arduino IDE.
2.  Pilih Board: "ESP32C3 Dev Module".
3.  Aktifkan opsi "USB CDC On Boot: Enabled" agar Serial Monitor dapat terbaca (jika diperlukan untuk debugging).
4.  Pastikan parameter `API_BASE_URL` dan `WIFI_SSID` sudah disesuaikan.
5.  Unggah (Upload) ke perangkat.
