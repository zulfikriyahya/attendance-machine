# Attendance Machine 📡🎓

Sistem **presensi otomatis berbasis RFID dengan ESP32**, terhubung ke **API Laravel** via WiFi. Cocok digunakan di lingkungan sekolah, kantor, dan instansi yang membutuhkan sistem presensi real-time, cepat, efisien, dan hemat daya.

---

## 🔧 Fitur Utama (v0.1.1)

- 📶 **Auto WiFi Connect** (Multi SSID)
- 📡 **Pembacaan RFID** (modul RC522)
- 🧠 **Koneksi ke API Laravel** (JSON POST + API Key)
- 🖥️ **Layar OLED 0.96"** untuk status real-time
- 🔊 **Buzzer feedback** (berhasil / gagal / error)
- 🔄 **Respon cepat & anti dobel scan (debounce)**
- 🔐 **Autentikasi API Token**
- 🌙 **Sleep Mode Otomatis** saat idle untuk hemat daya

---

## 🧰 Komponen Hardware

| Komponen    | Spesifikasi                  |
| ----------- | ---------------------------- |
| ESP32-C3    | Super Mini atau setara       |
| RFID Reader | RC522 (SDA, SCK, MOSI, MISO) |
| Layar OLED  | 0.96" I2C SSD1306            |
| Buzzer      | Aktif (Digital ON/OFF)       |
| Koneksi     | WiFi 2.4GHz                  |

**Pin Default:**

| Fungsi     | Pin ESP32-C3 |
| ---------- | ------------ |
| RC522 SS   | GPIO7        |
| RC522 RST  | GPIO3        |
| RC522 SCK  | GPIO4        |
| RC522 MOSI | GPIO6        |
| RC522 MISO | GPIO5        |
| OLED SDA   | GPIO8        |
| OLED SCL   | GPIO9        |
| Buzzer     | GPIO10       |

---

## ⚙️ Instalasi & Setup

### 1. Persiapan Software

- Arduino IDE terbaru
- Tambahkan board **ESP32-C3** dari Board Manager
- Install library berikut:
  - `MFRC522`
  - `Adafruit SSD1306`
  - `Adafruit GFX`
  - `ArduinoJson`
  - `WiFi`
  - `HTTPClient`

### 2. Konfigurasi

1. Salin `config-example.h` menjadi `config.h`
2. Isi data berikut:

```cpp
#define RST_PIN 3
#define SS_PIN 7
...
const char *WIFI_SSIDS[] = {"ZEDLABS", "LINE"};
const char *WIFI_PASSWORDS[] = {"pass1", "pass2"};
const String API_BASE_URL = "https://example.com/api";
const String API_SECRET = "YourSecretKeyHere";
```

### 3. Upload ke Board

- Pastikan port USB terdeteksi
- Compile dan upload via Arduino IDE

---

## 🔍 Alur Kerja

1. Perangkat menyala → OLED tampil logo dan animasi startup
2. Terhubung ke WiFi otomatis
3. Melakukan `ping` ke API untuk memastikan koneksi
4. Menunggu kartu RFID
5. Bila kartu valid → data dikirim ke API Laravel
6. OLED menampilkan status (nama, waktu, hasil)
7. Buzzer memberikan feedback suara
8. Setelah idle beberapa detik → perangkat masuk sleep mode otomatis (hemat baterai)
9. Tap kartu → perangkat otomatis aktif dan kembali ke mode normal

---

## 📡 API Endpoint

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

---

## 🖼️ Diagram Koneksi

![Schema](v0.1.1.svg)

---

## ❗ Troubleshooting

- **OLED tidak tampil?** Cek alamat I2C (`0x3C`), pastikan koneksi SDA/SCL benar
- **RC522 tidak terbaca?** Periksa `SCK`, `MISO`, `MOSI`, `SS`, `RST` sesuai pinout
- **Gagal WiFi?** Tambahkan lebih dari satu SSID di array `WIFI_SSIDS`
- **Sleep terlalu cepat?** Ubah durasi idle di firmware sebelum sleep
- **Error JSON?** Pastikan endpoint API aktif dan merespon format yang valid

---

## 📄 Lisensi

Proyek ini dilisensikan di bawah MIT License. Lihat file `LICENSE`.

---

## 👤 Author

**Zulfikri Yahya**
📍 Indonesia

---

## 🤝 Kontribusi

Pull Request dan laporan isu sangat disambut! Silakan fork proyek ini, modifikasi, dan kirim PR 👍
