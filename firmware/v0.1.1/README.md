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

## **Tabel Konversi Pin Lengkap**

| Fungsi     | Komponen | ESP32-C3 Super Mini | ESP32 DevKit V1 30 Pin | Keterangan               |
| ---------- | -------- | ------------------- | ---------------------- | ------------------------ |
| **RST**    | MFRC522  | GPIO 3              | **GPIO 16**            | Reset RFID Reader        |
| **SS/SDA** | MFRC522  | GPIO 7              | **GPIO 5**             | Slave Select/Chip Select |
| **SCK**    | MFRC522  | GPIO 4              | **GPIO 18**            | SPI Clock                |
| **MOSI**   | MFRC522  | GPIO 6              | **GPIO 23**            | Master Out Slave In      |
| **MISO**   | MFRC522  | GPIO 5              | **GPIO 19**            | Master In Slave Out      |
| **CS**     | SD Card  | GPIO 1              | **GPIO 15**            | Chip Select SD Card      |
| **SDA**    | OLED     | GPIO 8              | **GPIO 21**            | I2C Data                 |
| **SCL**    | OLED     | GPIO 9              | **GPIO 22**            | I2C Clock                |
| **Signal** | Buzzer   | GPIO 10             | **GPIO 4**             | Output Buzzer            |

## **Kode Definisi Pin untuk ESP32 DevKit V1:**

```cpp
// Pin MFRC522 (RFID Reader) - SPI
#define RST_PIN 16
#define SS_PIN 5
#define SCK 18
#define MISO 19
#define MOSI 23

// Pin SD Card - SPI
#define SD_CS 15

// Pin OLED Display - I2C
#define SDA_PIN 21
#define SCL_PIN 22

// Pin Buzzer
#define BUZZER_PIN 4
```

## **Diagram Koneksi:**

### **MFRC522 → ESP32 DevKit V1**

```
MFRC522          ESP32
----------------------------
SDA (SS)    →    GPIO 5
SCK         →    GPIO 18
MOSI        →    GPIO 23
MISO        →    GPIO 19
IRQ         →    (tidak dipakai)
GND         →    GND
RST         →    GPIO 16
3.3V        →    3.3V
```

### **OLED → ESP32 DevKit V1**

```
OLED            ESP32
----------------------------
VCC         →    3.3V atau 5V
GND         →    GND
SCL         →    GPIO 22
SDA         →    GPIO 21
```

### **Buzzer → ESP32 DevKit V1**

```
Buzzer          ESP32
----------------------------
Positive    →    GPIO 4
Negative    →    GND
```

### **SD Card Module → ESP32 DevKit V1** (Opsional)

```
SD Card         ESP32
----------------------------
CS          →    GPIO 15
SCK         →    GPIO 18
MOSI        →    GPIO 23
MISO        →    GPIO 19
VCC         →    5V
GND         →    GND
```

## **Catatan Penting:**

✅ **Pin yang Aman Digunakan untuk Output:** GPIO 4, 5, 12-15, 16-19, 21-23, 25-27, 32-33

⚠️ **Pin yang Harus Dihindari:**

- GPIO 0: Boot mode selection (pull-up saat boot)
- GPIO 2: Boot mode selection, LED onboard
- GPIO 6-11: Terhubung ke flash internal (JANGAN DIGUNAKAN)
- GPIO 34-39: Input only (ADC), tidak bisa output

📌 **SPI menggunakan 1 bus yang sama**, jadi MFRC522 dan SD Card share pin SCK, MISO, MOSI, tapi punya CS sendiri-sendiri.

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
