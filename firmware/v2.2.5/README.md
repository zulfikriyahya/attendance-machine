# ATTENDANCE MACHINE

**MADRASAH UNIVERSE — Sistem Presensi Pintar berbasis RFID**

![Version](https://img.shields.io/badge/version-2.2.5-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--C3-orange)
![IDE](https://img.shields.io/badge/IDE-Arduino%202.3.6-teal)
![Author](https://img.shields.io/badge/author-Yahya%20Zulfikri-green)

---

## Deskripsi

ATTENDANCE MACHINE adalah firmware untuk perangkat presensi berbasis RFID yang berjalan di atas ESP32-C3 Super Mini. Sistem mendukung mode **online** (langsung ke server API) dan **offline** (antrian ke SD Card), dengan sinkronisasi otomatis saat koneksi pulih. Dirancang untuk kebutuhan madrasah dengan pertimbangan jam operasional, deep sleep, dan toleransi jaringan yang tidak stabil.

---

## Hardware

| Komponen | Spesifikasi |
|---|---|
| Mikrokontroler | ESP32-C3 Super Mini |
| RFID Reader | MFRC522 (RC522) |
| Display | OLED SSD1306 128×64 I2C |
| Storage | MicroSD via SPI |
| Buzzer | Pasif 3000 Hz |

### Wiring / Pin Mapping

| Pin GPIO | Fungsi |
|---|---|
| 1 | SD Card CS |
| 3 | RFID RST |
| 4 | SPI SCK |
| 5 | SPI MISO |
| 6 | SPI MOSI |
| 7 | RFID SS |
| 8 | OLED SDA |
| 9 | OLED SCL |
| 10 | Buzzer |

> RFID dan SD Card berbagi bus SPI yang sama, dikelola dengan seleksi CS manual.

---

## Fitur Utama

- **Dual Mode Presensi** — Online langsung ke API, offline tersimpan di queue SD Card
- **Queue System** — Hingga 2.000 file queue × 25 record per file (kapasitas ±50.000 record)
- **Auto Sync** — Sinkronisasi otomatis saat koneksi WiFi pulih, dengan chunked sync non-blocking
- **Duplicate Prevention** — Cek duplikat berbasis UID + unix timestamp dengan window 30 menit
- **Time Fallback** — Estimasi waktu dari RTC memory jika NTP tidak tersedia (max 12 jam)
- **Deep Sleep** — Otomatis tidur pukul 18.00–05.00 WIB untuk hemat daya
- **OLED Schedule** — Layar mati otomatis pukul 08.00–14.00 WIB, nyala saat ada scan
- **SD Hot-Detect** — Deteksi ulang SD Card yang terlepas setiap 30 detik
- **Reconnect State Machine** — Percobaan ulang WiFi ke dua SSID secara non-blocking
- **Failed Log** — Record yang gagal disync dicatat ke `/failed_log.csv`

---

## Arsitektur Sistem

```
RFID Scan
    │
    ▼
kirimPresensi()
    ├─ [Waktu invalid]      → Tolak, tampilkan "WAKTU INVALID"
    ├─ [SD tersedia]
    │      ├─ [Online]      → validateRfidOnline() → saveToQueue()
    │      └─ [Offline]     → saveToQueue() langsung
    └─ [SD tidak ada]
           ├─ [Online]      → kirimLangsung() ke API
           └─ [Offline]     → Tolak, tampilkan "NO SD & WIFI"
```

```
Queue Sync Flow
    │
    ▼
chunkedSync()
    ├─ Baca hingga 5 file per siklus
    ├─ Tiap file: readQueueFile() → HTTP POST bulk ke /api/presensi/sync-bulk
    ├─ Response 200: hapus file, catat error item ke failed_log
    └─ Berjalan non-blocking, dilanjutkan di loop berikutnya
```

---

## Konfigurasi

Edit konstanta berikut di bagian atas file sebelum upload:

### Network

```cpp
const char WIFI_SSID_1[]     PROGMEM = "SSID_ANDA";
const char WIFI_PASSWORD_1[] PROGMEM = "PASSWORD_ANDA";
const char WIFI_SSID_2[]     PROGMEM = "SSID_BACKUP";
const char WIFI_PASSWORD_2[] PROGMEM = "PASSWORD_BACKUP";
const char API_BASE_URL[]    PROGMEM = "https://domain-anda.sch.id";
const char API_SECRET_KEY[]  PROGMEM = "SECRET_KEY_ANDA";
```

### Jadwal Operasional

```cpp
#define SLEEP_START_HOUR    18   // Mulai deep sleep (jam)
#define SLEEP_END_HOUR       5   // Bangun dari deep sleep (jam)
#define OLED_DIM_START_HOUR  8   // Layar mati otomatis (jam)
#define OLED_DIM_END_HOUR   14   // Layar hidup kembali (jam)
#define GMT_OFFSET_SEC   25200L  // WIB = UTC+7
```

### Queue

```cpp
#define MAX_RECORDS_PER_FILE     25    // Record per file CSV
#define MAX_QUEUE_FILES        2000    // Jumlah maksimal file antrian
#define MAX_SYNC_FILES_PER_CYCLE  5    // File yang diproses tiap siklus sync
#define QUEUE_WARN_THRESHOLD   1600    // Threshold warning queue hampir penuh
```

---

## API Endpoints

| Method | Endpoint | Fungsi |
|---|---|---|
| GET | `/api/presensi/ping` | Health check server |
| POST | `/api/presensi/validate` | Validasi RFID terdaftar |
| POST | `/api/presensi` | Kirim presensi langsung |
| POST | `/api/presensi/sync-bulk` | Sync batch dari queue |

Semua request menyertakan header `X-API-KEY`.

### Payload — Presensi Langsung

```json
{
  "rfid": "0012345678",
  "timestamp": "2026-03-10 07:30:00",
  "device_id": "ESP32_AABB",
  "sync_mode": false
}
```

### Payload — Sync Bulk

```json
{
  "data": [
    {
      "rfid": "0012345678",
      "timestamp": "2026-03-10 07:30:00",
      "device_id": "ESP32_AABB",
      "sync_mode": true
    }
  ]
}
```

### Response Kode

| HTTP Code | Makna |
|---|---|
| 200 | Berhasil |
| 400 | Duplikat / sudah presensi hari ini |
| 404 | RFID tidak terdaftar |

---

## Format Data SD Card

### Queue File (`/queue_N.csv`)

```
rfid,timestamp,device_id,unix_time
0012345678,2026-03-10 07:30:00,ESP32_AABB,1741577400
```

### Failed Log (`/failed_log.csv`)

```
rfid,timestamp,reason
0012345678,2026-03-10 07:30:00,DUPLICATE
```

---

## Dependensi Library

| Library | Fungsi |
|---|---|
| `WiFi.h` | Koneksi WiFi ESP32 |
| `HTTPClient.h` | HTTP request ke API |
| `Wire.h` | I2C untuk OLED |
| `SPI.h` | Bus SPI untuk RFID dan SD |
| `MFRC522.h` | Driver RFID RC522 |
| `Adafruit_SSD1306.h` | Driver OLED 128×64 |
| `ArduinoJson.h` | Serialisasi JSON payload |
| `SdFat.h` | Operasi file SD Card |
| `time.h` | NTP dan manajemen waktu |

Install semua library via **Arduino Library Manager** atau **PlatformIO**.

---

## Instalasi

1. Buka project di Arduino IDE 2.3.6
2. Pilih board: `ESP32C3 Dev Module`
3. Sesuaikan konfigurasi network dan jadwal di bagian atas file
4. Upload ke perangkat
5. Monitor serial output untuk melihat status boot (opsional)

---

## Alur Boot

```
Power On
    │
    ├─ Startup animation + melody
    ├─ Init SD Card → cek pending record
    ├─ Connect WiFi (2 SSID, masing-masing hingga 6 detik)
    ├─ Ping API (retry 3x)
    ├─ Sync NTP time
    ├─ Sync queue jika ada pending record
    ├─ Init RFID (restart jika gagal)
    └─ Loop utama
```

---

## Perilaku Saat Berbagai Kondisi

| Kondisi | Perilaku |
|---|---|
| WiFi OK, SD OK | Validasi online → simpan ke queue → sync berkala |
| WiFi OK, SD tidak ada | Kirim langsung ke API, tidak ada fallback storage |
| WiFi putus, SD OK | Simpan ke queue, reconnect tiap 5 menit |
| WiFi putus, SD tidak ada | Tolak semua scan, tampilkan "NO SD & WIFI" |
| Waktu tidak valid | Tolak semua scan, tampilkan "WAKTU INVALID" |
| Pukul 18.00–05.00 | Deep sleep, bangun otomatis sesuai hitungan waktu |

---

## Device ID

Device ID di-generate otomatis dari 2 byte terakhir MAC address saat pertama boot:

```
ESP32_XXYY  →  contoh: ESP32_A3F1
```

---

## Catatan Teknis

- `RTC_DATA_ATTR` digunakan untuk mempertahankan `lastValidTime`, `bootTime`, dan `currentQueueFile` saat deep sleep
- Estimasi waktu fallback maksimal 12 jam sejak sinkronisasi NTP terakhir
- SPI CS dikelola manual: RFID dan SD tidak pernah aktif bersamaan
- `sdBusy` flag mencegah akses SD yang tumpang tindih antara scan handler dan sync loop

---

## Author

**Yahya Zulfikri** — ZEDLABS
Project: MADRASAH UNIVERSE
Device: MTs N 1 Pandeglang
Version: 2.2.5 | Maret 2026
