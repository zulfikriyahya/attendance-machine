# Attendance Machine 📡🎓

Sistem **presensi otomatis berbasis RFID dengan ESP32**, terhubung ke **API Laravel** via WiFi dan kini mendukung **mode offline**. Cocok untuk lingkungan sekolah, kantor, dan instansi yang membutuhkan sistem presensi real-time, cepat, efisien, hemat daya, dan **tetap berjalan tanpa internet**.

---

## 🔧 Fitur Utama (v0.1.2)

- 📶 **Auto WiFi Connect** (Multi SSID)
- 📡 **Pembacaan RFID** (modul RC522)
- 🧠 **Koneksi ke API Laravel** (JSON POST + API Key)
- 💾 **Offline Mode**: simpan presensi ke SD Card saat offline
- 🔄 **Sinkronisasi Otomatis** saat WiFi tersedia kembali
- 🖥️ **Layar OLED 0.96"** untuk status real-time
- 🔊 **Buzzer feedback** (berhasil / gagal / error)
- 🔐 **Autentikasi API Token**
- 🚫 **Anti dobel scan (UID debounce)**
- 🌙 **Sleep Mode Otomatis** saat idle untuk hemat daya

---

## 🧰 Komponen Hardware

| Komponen    | Spesifikasi                  |
| ----------- | ---------------------------- |
| ESP32-C3    | Super Mini atau setara       |
| RFID Reader | RC522 (SDA, SCK, MOSI, MISO) |
| Layar OLED  | 0.96" I2C SSD1306            |
| Buzzer      | Aktif (Digital ON/OFF)       |
| Kartu SD    | MicroSD + Modul pembaca      |
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
| SD Card CS | GPIO1        |

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
  - `SD` (untuk penyimpanan lokal)

### 2. Konfigurasi

1. Salin `config-example.h` menjadi `config.h`
2. Isi data berikut:

```cpp
#define RST_PIN 3
#define SS_PIN 7
#define SD_CS   1
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

## 🔁 Alur Kerja Firmware

1. Perangkat menyala → OLED tampil logo dan animasi
2. Auto connect ke WiFi (multi SSID)
3. Jika **online**:

   - Menunggu tap kartu
   - Kirim data ke API
   - Tampilkan status dan suara buzzer

4. Jika **offline**:

   - Simpan UID + timestamp ke file JSON lokal (`presensi_offline.json`)
   - Tampilkan status offline di OLED

5. Jika **online kembali**:

   - Otomatis membaca file lokal
   - Kirim seluruh data tertunda ke API
   - Tampilkan hasil sinkronisasi

6. Setelah idle → masuk Sleep Mode hemat daya

---

## 📡 API Endpoint

```http
POST /api/presensi/rfid
Headers:
  X-API-KEY: [API_SECRET]
Body:
  {
    "rfid": "1234567890"
  }
```

### Sinkronisasi Offline:

```http
POST /api/presensi/rfid/batch
Headers:
  X-API-KEY: [API_SECRET]
Body:
  [
    {
      "rfid": "1234567890",
      "waktu": "2025-07-20T07:32:00"
    },
    ...
  ]
```

---

## 🖼️ Diagram Koneksi

![Schema](v0.1.20.svg)

---

## ❗ Troubleshooting

- **Data tidak tersimpan offline?** Pastikan SD Card terdeteksi, cek pin `SD_CS`
- **Sinkronisasi gagal?** Cek format JSON, status API, dan koneksi WiFi
- **OLED blank?** Periksa alamat I2C (`0x3C`), kabel SDA/SCL
- **Sleep terlalu cepat?** Ubah timer idle di sketch
- **File JSON corrupt?** Gunakan `ArduinoJson` versi terbaru untuk parsing aman

---

## 📝 Catatan Tambahan

- Format penyimpanan lokal: file JSON array `presensi_offline.json`
- Saat sinkronisasi berhasil, file otomatis dihapus/reset
- Ideal digunakan untuk lokasi dengan WiFi tidak stabil

---

## 📄 Lisensi

Proyek ini dilisensikan di bawah MIT License. Lihat file `LICENSE`.

---

## 👤 Author

**Zulfikri Yahya**
📍 Indonesia

---

## 🤝 Kontribusi

Pull Request, ide, dan laporan bug sangat disambut!
Silakan fork, kembangkan, dan ajukan PR 👍
