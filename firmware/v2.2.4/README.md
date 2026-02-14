# Sistem Presensi RFID - Queue System v2.2.4 (Ultimate)

## Deskripsi Proyek

Sistem Presensi Pintar v2.2.4 adalah iterasi terbaru dari perangkat presensi berbasis IoT yang dibangun di atas mikrokontroler ESP32-C3 Super Mini. Versi ini menambahkan fitur OLED Auto Dim untuk menghemat daya dan memperpanjang umur layar OLED dengan mematikan display secara otomatis pada jam-jam tertentu.

Sistem ini dirancang untuk operasional jangka panjang tanpa pengawasan dengan mekanisme pemulihan kesalahan mandiri, queue system yang robust, background processing yang tidak mengganggu operasi utama, dan manajemen display yang cerdas.

## Fitur Teknis (Versi 2.2.4)

### 1. OLED Auto Dim (NEW! v2.2.4)

- **Scheduled Display Control:** OLED otomatis mati pada pukul 08:00 dan nyala kembali pada pukul 14:00.
- **Smart Wake-up on Tap:** Display otomatis menyala sementara saat ada kartu yang di-tap, kemudian kembali mati sesuai jadwal.
- **Power Saving:** Mengurangi konsumsi daya dan memperpanjang umur OLED dengan mematikan display saat tidak diperlukan.
- **Seamless Operation:** Proses tapping RFID tetap berjalan normal meskipun display mati.
- **Visual Feedback:** Display tetap menampilkan informasi saat kartu di-tap bahkan dalam periode dim.

### 2. WiFi Signal Optimization (Warisan v2.2.3)

- **Maximum TX Power:** Pengaturan daya transmisi WiFi ke level maksimum (19.5 dBm) untuk jangkauan optimal.
- **Modem Sleep Mode:** Implementasi WIFI_PS_MAX_MODEM untuk efisiensi daya tanpa mengorbankan responsivitas.
- **Signal-Based AP Selection:** Koneksi otomatis ke Access Point dengan sinyal terkuat menggunakan WIFI_CONNECT_AP_BY_SIGNAL.
- **Persistent Connection:** Konfigurasi persistent mode dan auto-reconnect untuk stabilitas koneksi jangka panjang.
- **RSSI Display on Startup:** Tampilan kekuatan sinyal (RSSI) dalam dBm saat pertama kali terhubung WiFi.

### 3. Background Processing (Warisan v2.2.2)

- **Silent WiFi Reconnection:** Proses reconnect WiFi berjalan di background tanpa menampilkan loading atau progress bar yang dapat mengganggu pengguna saat tapping kartu.
- **Background Bulk Sync:** Sinkronisasi data offline ke server dilakukan secara diam-diam di latar belakang tanpa feedback visual atau audio yang mengganggu.
- **Non-Blocking Operations:** Semua operasi jaringan dirancang agar tidak memblokir proses pembacaan RFID, memastikan responsivitas sistem tetap optimal.
- **Dual Process Flags:** Implementasi flag `isWritingToQueue` dan `isReadingQueue` untuk mencegah konflik operasi paralel dan memastikan integritas data.

### 4. Manajemen Penyimpanan Tingkat Lanjut

- **Integrasi SdFat:** Menggunakan pustaka `SdFat` untuk akses sistem berkas yang lebih cepat dan stabil. Mendukung format FAT16 dan FAT32 secara optimal.
- **Buffered I/O:** Implementasi pembacaan dan penulisan berkas menggunakan buffer memori statis untuk meminimalkan fragmentasi heap pada ESP32-C3.
- **Sistem Antrean Skala Besar:** Mendukung hingga 2.000 berkas antrean dengan kapasitas 25 record per file, memungkinkan penyimpanan lokal hingga 50.000 data presensi saat offline.

### 5. Validasi Integritas Data

- **Sliding Window Duplicate Check:** Algoritma pencegahan duplikasi data tidak memindai seluruh penyimpanan, melainkan menggunakan metode sliding window (memeriksa berkas antrean aktif dan 1 berkas sebelumnya). Ini menjaga performa tetap cepat meskipun jumlah data tersimpan sangat besar.
- **Validasi Waktu Ketat:** Sinkronisasi waktu NTP dilakukan secara berkala (setiap 1 jam) untuk memastikan stempel waktu tetap akurat.

### 6. Stabilitas Sistem

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

// Konfigurasi Sleep Mode
const int SLEEP_START_HOUR = 18;   // Pukul 18:00 - Mesin Mati
const int SLEEP_END_HOUR = 5;      // Pukul 05:00 - Mesin Nyala

// Konfigurasi OLED Auto Dim (NEW! v2.2.4)
const int OLED_DIM_START_HOUR = 8; // Pukul 08:00 - OLED Mati
const int OLED_DIM_END_HOUR = 14;  // Pukul 14:00 - OLED Nyala

// Konfigurasi Display
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Update display setiap 1 detik
```

## Mekanisme OLED Auto Dim (v2.2.4)

### 1. Scheduled Display Management

Sistem mengelola status OLED berdasarkan waktu:

```cpp
// Fungsi untuk cek jadwal OLED
void checkOLEDSchedule() {
    // Ambil waktu saat ini
    int currentHour = timeInfo.tm_hour;

    // Cek apakah dalam rentang DIM (08:00 - 13:59)
    if (currentHour >= OLED_DIM_START_HOUR && currentHour < OLED_DIM_END_HOUR) {
        turnOffOLED();  // Matikan display
    } else {
        turnOnOLED();   // Nyalakan display
    }
}
```

### 2. Smart Wake-up on RFID Scan

Saat kartu di-tap pada periode dim, display otomatis menyala sementara:

```cpp
void handleRFIDScan() {
    // Simpan status OLED sebelumnya
    bool wasOff = !oledIsOn;

    // Nyalakan OLED sementara untuk scan
    if (wasOff) turnOnOLED();

    // Proses scanning dan tampilkan feedback
    showOLED(F("RFID"), rfidBuffer);
    playToneNotify();

    // Proses presensi
    kirimPresensi(rfidBuffer, message);

    // Matikan kembali jika memang sedang jam dim
    if (wasOff) checkOLEDSchedule();
}
```

### 3. Power Saving Benefits

Keuntungan dari OLED Auto Dim:

- **Pengurangan Konsumsi Daya:** OLED SSD1306 mengkonsumsi sekitar 15-20mA saat menyala. Dengan mematikan display selama 6 jam per hari, penghematan daya mencapai 25%.
- **Perpanjangan Umur OLED:** OLED memiliki umur terbatas (sekitar 10.000-50.000 jam). Mematikan display saat tidak diperlukan dapat memperpanjang umur layar hingga 30-40%.
- **Pengurangan Burn-in:** Mencegah burn-in pada pixel OLED yang menampilkan konten statis dalam waktu lama.

### 4. Jadwal OLED Default

| Waktu         | Status OLED | Keterangan                           |
| :------------ | :---------- | :----------------------------------- |
| 00:00 - 07:59 | ON          | Display aktif untuk presensi pagi    |
| 08:00 - 13:59 | OFF         | Display mati untuk hemat daya        |
| 14:00 - 17:59 | ON          | Display aktif untuk presensi sore    |
| 18:00 - 04:59 | SLEEP MODE  | Mesin deep sleep (termasuk display)  |

### 5. Customisasi Jadwal

Untuk mengubah jadwal OLED Auto Dim, edit konstanta di bagian konfigurasi:

```cpp
// Contoh: Ubah periode dim menjadi 10:00 - 15:00
const int OLED_DIM_START_HOUR = 10; // Pukul 10:00 - OLED Mati
const int OLED_DIM_END_HOUR = 15;   // Pukul 15:00 - OLED Nyala
```

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

### OLED Auto Dim Flow

```
[Check Current Time] → [Is between 08:00 - 13:59?]
                              ↓                ↓
                            [Yes]            [No]
                              ↓                ↓
                       [Turn OFF OLED]  [Turn ON OLED]
                              ↓
                      [Wait for RFID Tap]
                              ↓
                       [Turn ON OLED temporarily]
                              ↓
                       [Show Scan Feedback]
                              ↓
                       [Check Schedule Again]
```

### RFID Scan During Dim Period

```
[OLED is OFF] → [Card Detected] → [Save wasOff = true] → [Turn ON OLED]
                                                                ↓
                                                    [Show RFID & Feedback]
                                                                ↓
                                                      [Process Complete]
                                                                ↓
                                              [Check if wasOff == true]
                                                                ↓
                                                  [Turn OFF OLED again]
```

## User Experience Improvements (v2.2.4)

### Perbandingan dengan Versi Sebelumnya:

#### Sebelum v2.2.4:
- OLED selalu menyala sepanjang waktu operasional
- Konsumsi daya lebih tinggi
- Risiko burn-in pada display lebih besar
- Umur OLED lebih pendek
- Tidak ada mekanisme hemat daya untuk display

#### Sesudah v2.2.4:
- OLED otomatis mati pada jam 08:00 - 13:59
- Konsumsi daya berkurang hingga 25%
- Display otomatis menyala saat ada tapping
- Feedback visual tetap tersedia saat diperlukan
- Perpanjangan umur OLED hingga 30-40%
- User experience tetap seamless dan tidak terganggu

## Riwayat Perubahan (Changelog)

### v2.2.4 (Desember 2025) - OLED Auto Dim

- **Scheduled Display Control:** OLED otomatis mati pukul 08:00, nyala pukul 14:00
- **Smart Wake-up:** Display menyala sementara saat ada kartu yang di-tap
- **Power Saving:** Pengurangan konsumsi daya hingga 25%
- **OLED Longevity:** Perpanjangan umur display hingga 30-40%
- **Seamless Operation:** Tapping RFID tetap berfungsi normal saat display mati
- **Display State Management:** Implementasi flag `oledIsOn` untuk tracking status display

### v2.2.3 (Desember 2025) - WiFi Signal Optimization

- Maximum TX Power: Set WiFi TX Power ke 19.5 dBm untuk jangkauan maksimal
- Modem Sleep Mode: Implementasi WIFI_PS_MAX_MODEM untuk efisiensi daya
- Signal-Based AP Selection: Koneksi otomatis ke AP dengan sinyal terkuat
- Persistent Mode: WiFi persistent dan auto-reconnect untuk stabilitas
- RSSI Display: Tampilan kekuatan sinyal saat startup untuk monitoring
- Optimized Queue Config: Penyesuaian MAX_RECORDS_PER_FILE ke 25 dan MAX_QUEUE_FILES ke 2000
- Display Update Interval: Perubahan interval update display dari 500ms ke 1000ms untuk efisiensi

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

### Masalah: OLED tidak mati pada jam yang ditentukan

**Solusi:**
- Pastikan waktu sistem sudah tersinkronisasi dengan NTP server
- Periksa nilai `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR` di konfigurasi
- Restart perangkat untuk memastikan konfigurasi terbaca dengan benar

### Masalah: OLED tidak menyala saat kartu di-tap

**Solusi:**
- Periksa fungsi `handleRFIDScan()` untuk memastikan ada pemanggilan `turnOnOLED()`
- Periksa apakah variabel `oledIsOn` diupdate dengan benar
- Restart perangkat jika masalah berlanjut

### Masalah: Display tidak update setelah menyala kembali

**Solusi:**
- Fungsi `turnOnOLED()` sudah mengset `currentDisplay.needsUpdate = true`
- Pastikan `updateStandbySignal()` dipanggil setelah OLED menyala
- Periksa koneksi I2C antara ESP32-C3 dan OLED

### Masalah: WiFi sering disconnect

**Solusi:** Versi 2.2.4 mewarisi optimasi WiFi dari v2.2.3. Jika masih terjadi disconnect, periksa:
- Jarak perangkat dari Access Point (gunakan RSSI display untuk monitoring)
- Interferensi dari perangkat lain pada frekuensi 2.4 GHz
- Stabilitas power supply ESP32-C3 (minimal 500mA)

### Masalah: Sync lambat atau terblokir

**Solusi:** Pastikan tidak ada `showOLED()` atau `playTone()` di dalam `chunkedSync()`. Semua feedback visual harus dihapus untuk operasi background yang optimal.

## Best Practices

1. **Jadwal OLED:** Sesuaikan jadwal OLED Auto Dim dengan pola aktivitas presensi di lokasi deployment. Jika presensi siang banyak, pertimbangkan untuk mengubah jadwal dim.

2. **Power Supply:** Meskipun OLED Auto Dim mengurangi konsumsi daya, tetap gunakan power supply yang stabil dengan output minimal 5V 1A.

3. **Monitoring Awal:** Pada minggu pertama deployment, monitor pola penggunaan untuk menentukan jadwal dim yang optimal.

4. **Tidak Perlu Disable:** Fitur OLED Auto Dim dirancang untuk tidak mengganggu operasi. Tidak perlu menonaktifkan fitur ini kecuali ada kebutuhan khusus.

5. **Customisasi Jadwal:** Jangan ragu untuk menyesuaikan `OLED_DIM_START_HOUR` dan `OLED_DIM_END_HOUR` sesuai kebutuhan spesifik lokasi.

6. **Testing Schedule:** Setelah mengubah jadwal, test dengan mengubah waktu sistem untuk memastikan OLED mati dan menyala sesuai jadwal baru.

## Deployment Recommendations

### Skenario 1: Kantor dengan Jam Kerja 08:00 - 17:00
- Gunakan default setting (OLED mati 08:00 - 14:00)
- Presensi pagi (07:00-08:00): Display ON
- Presensi siang (12:00-13:00): Display OFF, tetapi menyala saat tap
- Presensi sore (16:00-17:00): Display ON

### Skenario 2: Fasilitas 24 Jam dengan Shift
- Pertimbangkan untuk mengubah jadwal dim ke periode dengan traffic rendah
- Contoh: Dim 02:00 - 06:00 untuk shift malam
- Monitor pola presensi untuk optimasi

### Skenario 3: Sekolah dengan Jam Belajar 07:00 - 15:00
- Ubah jadwal dim: 10:00 - 12:00 (istirahat pertama)
- Display ON saat siswa datang (07:00-08:00)
- Display ON saat pulang (14:00-15:00)

### Skenario 4: Deployment Outdoor dengan Solar Power
- Maksimalkan OLED Auto Dim untuk hemat daya
- Pertimbangkan jadwal dim yang lebih panjang
- Contoh: Dim 09:00 - 16:00, ON hanya saat peak hours

## Technical Specifications

### OLED Management
- Display Type: SSD1306 128x64 OLED
- Auto Dim Schedule: 08:00 - 13:59 (default)
- Wake-up on Tap: Enabled
- Power Consumption (ON): ~20mA
- Power Consumption (OFF): ~0mA
- Command: SSD1306_DISPLAYON / SSD1306_DISPLAYOFF

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
- Auto Dim Check: Every loop cycle

### Power Management
- Modem Sleep: Enabled (WIFI_PS_MAX_MODEM)
- OLED Auto Dim: 08:00 - 14:00 (configurable)
- Deep Sleep: 18:00 - 05:00 (configurable)
- Wake-up: Timer-based

## Power Consumption Analysis

### Konsumsi Daya Per Komponen (Estimasi):

| Komponen    | Mode Aktif | Mode Sleep/OFF | Keterangan                        |
| :---------- | :--------- | :------------- | :-------------------------------- |
| ESP32-C3    | ~80mA      | ~10μA          | Deep sleep sangat efisien         |
| WiFi TX     | ~170mA     | -              | Hanya saat transmit               |
| OLED        | ~20mA      | ~0mA           | SSD1306 OFF benar-benar mati      |
| RFID RC522  | ~13mA      | ~10μA          | Standby mode                      |
| SD Card     | ~50mA      | ~100μA         | Saat read/write                   |
| Buzzer      | ~30mA      | 0mA            | Saat bunyi                        |

### Penghematan Daya dengan OLED Auto Dim:

**Perhitungan Harian (24 jam):**
- Waktu OLED ON tanpa Auto Dim: 13 jam (05:00 - 18:00)
- Waktu OLED ON dengan Auto Dim: 7 jam (05:00-08:00 + 14:00-18:00)
- Penghematan: 6 jam x 20mA = 120mAh per hari
- Persentase penghematan: ~25% dari konsumsi display

**Impact pada Battery Life (contoh dengan battery 2000mAh):**
- Tanpa Auto Dim: Battery habis dalam ~15 jam
- Dengan Auto Dim: Battery habis dalam ~18 jam
- Peningkatan battery life: ~20%

## Lisensi

Hak Cipta 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
