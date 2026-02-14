# Sistem Presensi RFID - Queue System v2.2.3 (Ultimate)

## Deskripsi Proyek

Sistem Presensi Pintar v2.2.3 adalah iterasi terbaru dari perangkat presensi berbasis IoT yang dibangun di atas mikrokontroler ESP32-C3 Super Mini. Versi ini fokus pada optimalisasi kekuatan sinyal WiFi untuk meningkatkan stabilitas koneksi dan jangkauan komunikasi dengan server.

Sistem ini dirancang untuk operasional jangka panjang tanpa pengawasan dengan mekanisme pemulihan kesalahan mandiri, queue system yang robust, dan background processing yang tidak mengganggu operasi utama.

## Fitur Teknis (Versi 2.2.3)

### 1. WiFi Signal Optimization (NEW! v2.2.3)

- **Maximum TX Power:** Pengaturan daya transmisi WiFi ke level maksimum (19.5 dBm) untuk jangkauan optimal.
- **Modem Sleep Mode:** Implementasi WIFI_PS_MAX_MODEM untuk efisiensi daya tanpa mengorbankan responsivitas.
- **Signal-Based AP Selection:** Koneksi otomatis ke Access Point dengan sinyal terkuat menggunakan WIFI_CONNECT_AP_BY_SIGNAL.
- **Persistent Connection:** Konfigurasi persistent mode dan auto-reconnect untuk stabilitas koneksi jangka panjang.
- **RSSI Display on Startup:** Tampilan kekuatan sinyal (RSSI) dalam dBm saat pertama kali terhubung WiFi.

### 2. Background Processing (Warisan v2.2.2)

- **Silent WiFi Reconnection:** Proses reconnect WiFi berjalan di background tanpa menampilkan loading atau progress bar yang dapat mengganggu pengguna saat tapping kartu.
- **Background Bulk Sync:** Sinkronisasi data offline ke server dilakukan secara diam-diam di latar belakang tanpa feedback visual atau audio yang mengganggu.
- **Non-Blocking Operations:** Semua operasi jaringan dirancang agar tidak memblokir proses pembacaan RFID, memastikan responsivitas sistem tetap optimal.
- **Dual Process Flags:** Implementasi flag `isWritingToQueue` dan `isReadingQueue` untuk mencegah konflik operasi paralel dan memastikan integritas data.

### 3. Manajemen Penyimpanan Tingkat Lanjut

- **Integrasi SdFat:** Menggunakan pustaka `SdFat` untuk akses sistem berkas yang lebih cepat dan stabil. Mendukung format FAT16 dan FAT32 secara optimal.
- **Buffered I/O:** Implementasi pembacaan dan penulisan berkas menggunakan buffer memori statis untuk meminimalkan fragmentasi heap pada ESP32-C3.
- **Sistem Antrean Skala Besar:** Mendukung hingga 2.000 berkas antrean dengan kapasitas 25 record per file, memungkinkan penyimpanan lokal hingga 50.000 data presensi saat offline.

### 4. Validasi Integritas Data

- **Sliding Window Duplicate Check:** Algoritma pencegahan duplikasi data tidak memindai seluruh penyimpanan, melainkan menggunakan metode sliding window (memeriksa berkas antrean aktif dan 1 berkas sebelumnya). Ini menjaga performa tetap cepat meskipun jumlah data tersimpan sangat besar.
- **Validasi Waktu Ketat:** Sinkronisasi waktu NTP dilakukan secara berkala (setiap 1 jam) untuk memastikan stempel waktu tetap akurat.

### 5. Stabilitas Sistem

- **Penanganan Kesalahan Fatal:** Mekanisme Fail-Safe yang me-restart perangkat secara otomatis jika terjadi kegagalan kritis pada modul RFID atau inisialisasi sistem berkas.
- **Auto-Reconnect Cerdas:** Sistem akan mencoba reconnect WiFi setiap 5 menit secara background jika koneksi terputus, tanpa mengganggu operasi normal.
- **Indikator Visual Presisi:** Umpan balik visual pada OLED yang lebih responsif, termasuk bilah progres dan status antrean real-time.

## Spesifikasi Perangkat Keras dan Pinout

Sistem menggunakan bus SPI bersama untuk komunikasi dengan modul RFID dan SD Card. Manajemen lalu lintas data diatur melalui manajemen sinyal Chip Select yang ketat.

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

## Prasyarat Instalasi Perangkat Lunak

Versi ini memiliki dependensi pustaka yang berbeda dari versi sebelumnya. Pastikan pustaka berikut terinstal:

1. **SdFat** oleh Bill Greiman (Versi 2.x): Wajib. Menggantikan pustaka `SD` bawaan Arduino. Memberikan kinerja I/O yang jauh lebih baik.
2. **MFRC522** (GithubCommunity): Driver RFID.
3. **Adafruit SSD1306** & **Adafruit GFX**: Driver tampilan OLED.
4. **ArduinoJson**: Serialisasi JSON.
5. **WiFi**, **HTTPClient**, **Wire**, **SPI**: Pustaka inti ESP32.

## Konfigurasi Sistem

Parameter operasional utama didefinisikan pada bagian awal kode sumber:

```cpp
// Konfigurasi Antrean
const int MAX_RECORDS_PER_FILE      = 25;     // Optimasi RAM untuk ESP32-C3
const int MAX_QUEUE_FILES           = 2000;   // Kapasitas total 50.000 record
const unsigned long SYNC_INTERVAL   = 300000; // Sinkronisasi setiap 5 menit (background)

// Konfigurasi Validasi
const unsigned long MIN_REPEAT_INTERVAL = 1800; // Debounce 30 menit untuk kartu sama

// Konfigurasi Reconnect
const unsigned long RECONNECT_INTERVAL = 300000; // Auto-reconnect setiap 5 menit
const unsigned long TIME_SYNC_INTERVAL = 3600000; // Time sync setiap 1 jam

// Konfigurasi Display
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Update display setiap 1 detik
```

## Mekanisme WiFi Signal Optimization (v2.2.3)

### 1. Pengaturan Daya Transmisi Maksimal

Sistem secara otomatis mengatur TX Power ke level maksimum yang didukung ESP32-C3:

```cpp
// Set TX Power ke maksimum untuk jangkauan optimal
WiFi.setTxPower(WIFI_POWER_19_5dBm);
```

Keuntungan:
- Jangkauan sinyal WiFi lebih jauh
- Penetrasi sinyal lebih baik melalui dinding atau penghalang
- Stabilitas koneksi meningkat pada jarak yang lebih jauh dari Access Point

### 2. Modem Sleep Mode

Implementasi power saving mode yang cerdas:

```cpp
// Modem sleep: WiFi mati saat idle, bangun otomatis saat ada traffic
WiFi.setSleep(WIFI_PS_MAX_MODEM);
```

Keuntungan:
- Hemat daya saat tidak ada transmisi data
- WiFi tetap responsif saat dibutuhkan
- Tidak mempengaruhi kecepatan tapping RFID

### 3. Prioritas Koneksi Berdasarkan Sinyal

Sistem memilih Access Point dengan sinyal terkuat secara otomatis:

```cpp
// Prioritas koneksi berdasarkan kekuatan sinyal
WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
```

Keuntungan:
- Koneksi lebih stabil karena memilih AP dengan sinyal terbaik
- Automatic failover ke AP alternatif jika sinyal melemah
- Optimal untuk deployment dengan multiple Access Points

### 4. Persistent Mode dan Auto-Reconnect

```cpp
// Set persistent mode untuk koneksi yang lebih stabil
WiFi.persistent(true);
WiFi.setAutoReconnect(true);
```

Keuntungan:
- Konfigurasi WiFi tersimpan di flash memory
- Reconnect otomatis tanpa perlu konfigurasi ulang
- Lebih cepat saat boot ulang

### 5. RSSI Monitoring

Sistem menampilkan kekuatan sinyal saat startup untuk diagnosis:

```cpp
long rssi = WiFi.RSSI();
snprintf(messageBuffer, sizeof(messageBuffer), "RSSI: %ld dBm", rssi);
showOLED(F("WIFI OK"), messageBuffer);
```

Interpretasi nilai RSSI:
- -50 dBm atau lebih baik: Sinyal sangat kuat
- -67 dBm: Sinyal kuat (4 bar)
- -70 dBm: Sinyal baik (3 bar)
- -80 dBm: Sinyal cukup (2 bar)
- -90 dBm: Sinyal lemah (1 bar)
- Di bawah -90 dBm: Sinyal sangat lemah

## Mekanisme Kerja Antrean (Queue Logic)

1. **Segmentasi:** Data disimpan dalam berkas kecil (`queue_N.csv`) berisi maksimal 25 baris untuk mencegah buffer overflow.
2. **Rotasi:** Saat berkas `queue_N` penuh, sistem secara otomatis membuat `queue_N+1`.
3. **Sinkronisasi Background:**
   - Sistem mencari berkas antrean yang ada secara sekuensial.
   - Data dibaca, diparsing ke JSON, dan dikirim ke server tanpa feedback visual.
   - Jika server merespons HTTP 200, berkas `queue_N` dihapus secara fisik dari SD Card.
   - Jika gagal, berkas dipertahankan untuk percobaan berikutnya.
   - Proses ini tidak mengganggu operasi tapping RFID.

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
    ]
  }
  ```

### Endpoint Ping/Health Check

- **URL:** `/api/presensi/ping`
- **Method:** `GET`
- **Header:** `X-API-KEY: [SECRET_KEY]`
- **Response:** HTTP 200 jika server aktif

## Kompatibilitas Kartu SD

Berkat implementasi `SdFat`, sistem ini mendukung berbagai jenis kartu memori yang sering gagal pada pustaka standar:

- SDSC (Standard Capacity) sampai 2GB (termasuk kartu lama 128MB/256MB).
- SDHC (High Capacity) 4GB sampai 32GB.
- Format sistem berkas: FAT16 dan FAT32.

## Flow Diagram Operasional

### Normal Operation Flow

```
[Standby Display] → [RFID Detected] → [Save to Queue] → [Success Feedback] → [Standby Display]
                                                        ↓
                                              [Background Sync (if online)]
```

### Background Reconnect Flow

```
[WiFi Disconnected] → [Wait 5 minutes] → [Silent Reconnect Attempt with Max TX Power]
                                                         ↓
                                                   [Success?]
                                                  ↙          ↘
                                             [Yes]            [No]
                                               ↓                ↓
                                       [Update isOnline]   [Retry later]
                                       [Auto Sync Queue]
```

### WiFi Signal Optimization Flow

```
[WiFi Init] → [Set TX Power to 19.5dBm] → [Enable Modem Sleep] → [Set Signal Priority]
                                    ↓
                              [Connect to AP]
                                    ↓
                         [Display RSSI in dBm]
```

## User Experience Improvements (v2.2.3)

### Perbandingan dengan Versi Sebelumnya:

#### Sebelum v2.2.3:
- Koneksi WiFi sering drop pada jarak yang jauh dari Access Point
- Tidak ada informasi tentang kekuatan sinyal saat startup
- Power management tidak optimal
- Koneksi ke AP acak tanpa mempertimbangkan kekuatan sinyal

#### Sesudah v2.2.3:
- Jangkauan WiFi lebih jauh dengan TX Power maksimal
- Display RSSI memberikan informasi diagnostik yang berguna
- Power management optimal dengan modem sleep
- Koneksi otomatis ke AP dengan sinyal terkuat
- Reconnect tetap menggunakan pengaturan power maksimal
- User experience yang mulus tanpa gangguan dari proses background

## Riwayat Perubahan (Changelog)

### v2.2.3 (Desember 2025) - WiFi Signal Optimization

- **Maximum TX Power:** Set WiFi TX Power ke 19.5 dBm untuk jangkauan maksimal
- **Modem Sleep Mode:** Implementasi WIFI_PS_MAX_MODEM untuk efisiensi daya
- **Signal-Based AP Selection:** Koneksi otomatis ke AP dengan sinyal terkuat
- **Persistent Mode:** WiFi persistent dan auto-reconnect untuk stabilitas
- **RSSI Display:** Tampilan kekuatan sinyal saat startup untuk monitoring
- **Optimized Queue Config:** Penyesuaian MAX_RECORDS_PER_FILE ke 25 dan MAX_QUEUE_FILES ke 2000
- **Display Update Interval:** Perubahan interval update display dari 500ms ke 1000ms untuk efisiensi

### v2.2.2 (Desember 2025) - Background Operations

- Background WiFi Reconnect: Reconnect otomatis setiap 5 menit tanpa tampilan
- Background Bulk Sync: Sinkronisasi data offline berjalan di latar belakang
- Dual Process Flags: Implementasi `isSyncing` dan `isReconnecting` untuk thread safety
- Silent Operations: Semua operasi network tanpa feedback visual/audio yang mengganggu
- Enhanced UX: Display hanya fokus pada informasi penting (status, waktu, queue counter)
- Improved Responsiveness: Tapping RFID tidak pernah terblokir oleh operasi background

### v2.2.1 (Desember 2025)

- Penambahan Fitur Autoconnect WiFi
- Perbaikan Bug System Versi 2.2.0

### v2.2.0 (Desember 2025)

- Migrasi ke `SdFat`
- Dukungan kartu memori legacy
- Optimasi buffer memori
- Perbaikan visual interface

### v2.1.0

- Pengenalan sistem antrean multi-file

### v2.0.0

- Implementasi mode offline dasar (single CSV)

### v1.0.0

- Rilis awal (Online only)

## Troubleshooting

### Masalah: WiFi sering disconnect

**Solusi:** Versi 2.2.3 sudah mengoptimalkan WiFi dengan TX Power maksimal dan modem sleep mode. Jika masih terjadi disconnect, periksa:
- Jarak perangkat dari Access Point (gunakan RSSI display untuk monitoring)
- Interferensi dari perangkat lain pada frekuensi 2.4 GHz
- Stabilitas power supply ESP32-C3 (minimal 500mA)

### Masalah: RSSI menunjukkan nilai lemah (-80 dBm atau lebih rendah)

**Solusi:**
- Pindahkan perangkat lebih dekat ke Access Point
- Pertimbangkan menggunakan WiFi extender atau Access Point tambahan
- Pastikan tidak ada penghalang logam antara perangkat dan Access Point

### Masalah: Sync lambat atau terblokir

**Solusi:** Pastikan tidak ada `showOLED()` atau `playTone()` di dalam `chunkedSync()`. Semua feedback visual harus dihapus untuk operasi background yang optimal.

### Masalah: Double sync/reconnect

**Solusi:** Periksa implementasi flag `syncInProgress` dan reconnect state machine. Pastikan flag di-set sebelum operasi dan di-clear setelah selesai.

### Masalah: Display tidak update status online/offline

**Solusi:** Pastikan `updateStandbySignal()` membaca variable `isOnline` yang selalu diupdate di loop utama.

## Best Practices

1. **Penempatan Perangkat:** Letakkan perangkat pada lokasi dengan sinyal WiFi yang baik (RSSI di atas -70 dBm) untuk performa optimal.

2. **Power Supply:** Gunakan power supply yang stabil dengan output minimal 5V 1A untuk menghindari brownout saat transmisi WiFi dengan daya maksimal.

3. **Monitoring RSSI:** Perhatikan nilai RSSI saat startup untuk memastikan kekuatan sinyal memadai. Jika RSSI di bawah -80 dBm, pertimbangkan reposisi perangkat.

4. **Multiple Access Points:** Jika deployment menggunakan multiple APs, pastikan semua AP memiliki SSID dan password yang sama agar sistem dapat memilih AP dengan sinyal terkuat secara otomatis.

5. **Tidak Perlu Modifikasi WiFi Code:** Semua optimasi WiFi sudah diimplementasikan di v2.2.3. Tidak perlu menambahkan kode WiFi tambahan.

6. **Background Process:** Biarkan background sync dan reconnect berjalan otomatis. Tidak perlu intervensi manual atau feedback visual tambahan.

## Deployment Recommendations

### Skenario 1: Single Access Point
- Pastikan perangkat berada dalam jangkauan 10-15 meter dari AP
- Monitor RSSI saat deployment awal
- Gunakan AP dengan power output yang memadai

### Skenario 2: Multiple Access Points
- Konfigurasi semua AP dengan SSID yang sama
- Sistem akan otomatis memilih AP dengan sinyal terkuat
- Overlap coverage area antar AP sebesar 20-30% untuk seamless roaming

### Skenario 3: Area dengan Interferensi Tinggi
- Gunakan WiFi channel yang tidak terlalu crowded (1, 6, atau 11)
- Pertimbangkan menggunakan AP dengan external antenna
- Monitor RSSI secara berkala untuk mendeteksi degradasi sinyal

## Technical Specifications

### WiFi Configuration
- TX Power: 19.5 dBm (maksimal untuk ESP32-C3)
- Sleep Mode: WIFI_PS_MAX_MODEM
- Sort Method: WIFI_CONNECT_AP_BY_SIGNAL
- Persistent: Enabled
- Auto-Reconnect: Enabled
- Reconnect Interval: 300 detik (5 menit)

### Queue System
- Max Records per File: 25
- Max Queue Files: 2000
- Total Capacity: 50.000 records
- Sync Interval: 300 detik (5 menit)
- Duplicate Check Window: 1800 detik (30 menit)

### Display Update
- Update Interval: 1000 ms (1 detik)
- RSSI Bars: 4 levels (berdasarkan -67, -70, -80, -90 dBm)
- Queue Counter: Real-time

### Power Management
- Modem Sleep: Enabled (WIFI_PS_MAX_MODEM)
- Deep Sleep: 18:00 - 05:00 (configurable)
- Wake-up: Timer-based

## Lisensi

Hak Cipta 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
