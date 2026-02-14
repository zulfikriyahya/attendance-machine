# Sistem Presensi RFID - Queue System v2.2.3 (Ultimate - Connectivity Edition)

## Deskripsi Proyek
**Sistem Presensi Pintar v2.2.3** adalah penyempurnaan dari versi Ultimate yang berfokus secara spesifik pada **Stabilitas Konektivitas Jarak Jauh (Long-Range Connectivity)** dan keamanan data. Dibangun di atas mikrokontroler ESP32-C3 Super Mini, versi ini mengimplementasikan manajemen daya radio (RF) tingkat lanjut untuk memaksimalkan jangkauan WiFi di lingkungan dengan sinyal lemah.

Selain fitur *Background Processing* yang diperkenalkan pada v2.2.2, versi ini merombak struktur penyimpanan antrean menjadi lebih granular (pecahan lebih kecil) untuk meminimalkan risiko korupsi data saat penulisan dan mempercepat proses *chunked sync*.

## Fitur Teknis Baru (Versi 2.2.3)

### 1. Advanced WiFi Signal Optimization (NEW!)
Versi 2.2.3 tidak hanya sekadar "terhubung", tetapi secara aktif mengelola kualitas sinyal radio:
- **Max Transmission Power (19.5dBm):** Sistem memaksa output radio ke level maksimum (`WIFI_POWER_19_5dBm`) untuk meningkatkan jangkauan tangkapan sinyal dan stabilitas pengiriman data.
- **Smart AP Sorting:** Menggunakan algoritma `WIFI_CONNECT_AP_BY_SIGNAL` untuk memprioritaskan Access Point dengan sinyal terkuat (RSSI terbaik) saat roaming atau inisialisasi.
- **Modem Sleep Management:** Menggunakan `WIFI_PS_MAX_MODEM` untuk manajemen daya yang efisien namun tetap responsif terhadap lalu lintas data.

### 2. Granular Queue System (Optimized)
Struktur penyimpanan data offline telah dikalibrasi ulang untuk keamanan dan kecepatan:
- **Ukuran Berkas Lebih Kecil:** Kapasitas per berkas diturunkan dari 50 menjadi **25 record** (`MAX_RECORDS_PER_FILE`). Ini mengurangi waktu buka/tulis SD Card dan memperkecil risiko kehilangan data jika terjadi korupsi pada satu berkas.
- **Peningkatan Jumlah Berkas:** Kapasitas total berkas ditingkatkan menjadi **2.000 berkas** (`MAX_QUEUE_FILES`).
- **Total Kapasitas Tetap:** Meskipun ukuran berkas mengecil, total kapasitas penyimpanan tetap **50.000 data presensi** (25 record x 2.000 file).

### 3. Background Processing (Warisan v2.2.2)
Tetap mempertahankan fitur unggulan versi sebelumnya:
- **Silent WiFi Reconnection:** Reconnect tanpa mengganggu UI.
- **Background Bulk Sync:** Upload data berjalan di latar belakang.
- **Non-Blocking Tapping:** User tetap bisa tap kartu saat sinkronisasi berjalan.

## Spesifikasi Perangkat Keras dan Pinout
Sistem menggunakan bus SPI bersama (_Shared SPI Bus_). Konfigurasi pinout tetap sama dengan versi sebelumnya.

| Komponen       | Pin Modul | Pin ESP32-C3 | Protokol | Catatan                        |
| :------------- | :-------- | :----------- | :------- | :----------------------------- |
| **Bus SPI**    | SCK       | GPIO 4       | SPI      | Jalur Clock (Shared)           |
|                | MOSI      | GPIO 6       | SPI      | Jalur Data Master Out (Shared) |
|                | MISO      | GPIO 5       | SPI      | Jalur Data Master In (Shared)  |
| **RFID RC522** | SDA (SS)  | GPIO 7       | SPI      | Chip Select RFID               |
|                | RST       | GPIO 3       | Digital  | Reset Hardware                 |
| **SD Card**    | CS        | GPIO 1       | SPI      | Chip Select SD Card            |
| **OLED**       | SDA       | GPIO 8       | I2C      | Data Display                   |
|                | SCL       | GPIO 9       | I2C      | Clock Display                  |
| **Buzzer**     | (+)       | GPIO 10      | PWM      | Indikator Audio                |

> **Penting:** Pastikan power supply stabil (disarankan 5V 2A) karena fitur `WIFI_POWER_19_5dBm` mungkin menarik arus puncak yang lebih tinggi saat transmisi data.

## Konfigurasi Sistem (v2.2.3 Updated)
Parameter operasional telah diperbarui dalam kode sumber:

```cpp
// Konfigurasi Antrean (Updated v2.2.3)
const int MAX_RECORDS_PER_FILE      = 25;     // Lebih kecil = Lebih aman & cepat
const int MAX_QUEUE_FILES           = 2000;   // Lebih banyak file = Kapasitas tetap 50k
const unsigned long SYNC_INTERVAL   = 300000; // Sinkronisasi setiap 5 menit

// Optimasi Display
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Refresh rate 1 detik (hemat CPU)

// Konfigurasi Validasi
const unsigned long MIN_REPEAT_INTERVAL = 1800; // Debounce 30 menit

// Konfigurasi Reconnect
const unsigned long RECONNECT_INTERVAL = 300000; // Auto-reconnect 5 menit
```

## Mekanisme Koneksi Tingkat Lanjut (v2.2.3)

Fungsi `connectToWiFi()` pada versi ini telah dirombak total untuk keandalan:

1.  **Pembersihan State:** Memutus koneksi lama dan membersihkan konfigurasi WiFi sebelum memulai.
2.  **Boost Power:** Mengatur TX Power ke 19.5dBm (maksimum hardware ESP32).
3.  **Prioritas Sinyal:** Jika ada beberapa SSID yang sama (Mesh Network), sistem akan otomatis memilih yang sinyalnya paling kuat.
4.  **Persistent Connection:** Mengaktifkan flag `persistent(true)` dan `autoReconnect(true)` pada level driver WiFi.

```cpp
// Snippet Logika Koneksi v2.2.3
WiFi.disconnect(true);
WiFi.setTxPower(WIFI_POWER_19_5dBm); // Max Power
WiFi.setSleep(WIFI_PS_MAX_MODEM);    // Optimized Sleep
WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL); // Pilih sinyal terkuat
```

## Mekanisme Kerja Antrean (Granular Queue)

1.  **Write:** Data disimpan dalam `queue_N.csv`. Setelah mencapai **25 baris**, sistem otomatis membuat `queue_N+1`.
2.  **Sync:** Proses sinkronisasi mengambil berkas antrean satu per satu. Karena ukuran berkas lebih kecil (25 baris), proses upload HTTP lebih ringan dan jarang mengalami *timeout*.
3.  **Delete:** Berkas hanya dihapus jika server merespons HTTP 200 OK.

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
          // ... max 25 items per request ...
      ]
  }
  ```

## Prasyarat Instalasi
Library yang dibutuhkan sama dengan v2.2.2:
1.  **SdFat** (Bill Greiman) v2.x
2.  **MFRC522**
3.  **Adafruit SSD1306** & **Adafruit GFX**
4.  **ArduinoJson**
5.  ESP32 Board Definitions (Versi 2.0.11 atau terbaru disarankan untuk dukungan driver WiFi terbaru).

## Riwayat Perubahan (Changelog)

### v2.2.3 (Desember 2025) - Connectivity Edition
- **WiFi Signal Boost:** Implementasi `WIFI_POWER_19_5dBm` untuk jangkauan maksimal.
- **Smart Roaming:** Implementasi `WIFI_CONNECT_AP_BY_SIGNAL` untuk seleksi AP terbaik.
- **Granular Queue:** Perubahan struktur antrean (25 record/file, 2000 files) untuk I/O yang lebih cepat dan aman.
- **CPU Optimization:** Penyesuaian interval refresh display ke 1000ms.
- **Sleep Management:** Implementasi `WIFI_PS_MAX_MODEM`.

### v2.2.2 (Desember 2025) - Ultimate
- Background WiFi Reconnect & Bulk Sync (Silent Operation).
- Migrasi penuh ke `SdFat`.
- Dual Process Flags (`isSyncing`, `isReconnecting`).

### v2.2.0 - v2.2.1
- Dukungan kartu memori legacy & perbaikan autoconnect.

## Troubleshooting v2.2.3

### Masalah: Perangkat sering panas
**Penyebab:** Penggunaan TX Power 19.5dBm terus menerus.
**Solusi:** Pastikan sirkulasi udara pada box casing cukup. Jika menggunakan baterai, umur baterai mungkin sedikit lebih pendek dibanding v2.2.2.

### Masalah: RSSI (Sinyal) tetap rendah
**Solusi:** Cek antena ESP32-C3 Super Mini. Pastikan tidak tertutup logam atau komponen lain (seperti modul pembaca SD Card) secara langsung di atas antena keramik.

### Masalah: Gagal Mount SD Card
**Solusi:** Karena interval display diperlambat, pastikan menunggu inisialisasi selesai (sekitar 2-3 detik) saat boot sebelum mencabut/memasang kartu.

## Lisensi
Hak Cipta © 2025 Yahya Zulfikri. MIT License.
