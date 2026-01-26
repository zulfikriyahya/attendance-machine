# Sistem Presensi RFID - Queue System v2.2.2 (Ultimate)

## Deskripsi Proyek

Sistem Presensi Pintar v2.2.2 adalah iterasi mutakhir dari perangkat presensi berbasis IoT yang dibangun di atas mikrokontroler ESP32-C3 Super Mini. Versi ini, yang diberi label "Ultimate", difokuskan pada optimalisasi manajemen memori (RAM), stabilitas _Input/Output_ (I/O), kompatibilitas perangkat keras penyimpanan, dan **proses background yang tidak mengganggu operasi tapping**.

Perubahan paling signifikan pada versi ini adalah migrasi dari pustaka standar `SD` ke `SdFat`, memungkinkan dukungan untuk kartu SD kapasitas rendah (Legacy 128MB) hingga kapasitas tinggi dengan efisiensi _buffer_ yang superior. Ditambah dengan **sistem background reconnect dan sync** yang memastikan proses tapping RFID tidak pernah terganggu oleh proses jaringan atau sinkronisasi data. Sistem ini dirancang untuk operasional jangka panjang tanpa pengawasan (_unattended operation_) dengan mekanisme pemulihan kesalahan mandiri.

## Fitur Teknis (Versi 2.2.2)

### 1. Background Processing (NEW! v2.2.2)

- **Silent WiFi Reconnection:** Proses reconnect WiFi berjalan di background tanpa menampilkan loading atau progress bar yang dapat mengganggu pengguna saat tapping kartu.
- **Background Bulk Sync:** Sinkronisasi data offline ke server dilakukan secara diam-diam di latar belakang tanpa feedback visual atau audio yang mengganggu.
- **Non-Blocking Operations:** Semua operasi jaringan dirancang agar tidak memblokir proses pembacaan RFID, memastikan responsivitas sistem tetap optimal.
- **Dual Process Flags:** Implementasi flag `isSyncing` dan `isReconnecting` untuk mencegah konflik operasi paralel dan memastikan integritas data.

### 2. Manajemen Penyimpanan Tingkat Lanjut

- **Integrasi SdFat:** Menggunakan pustaka `SdFat` untuk akses sistem berkas yang lebih cepat dan stabil. Mendukung format FAT16 dan FAT32 secara optimal.
- **Buffered I/O:** Implementasi pembacaan dan penulisan berkas menggunakan buffer memori statis untuk meminimalkan fragmentasi _heap_ pada ESP32-C3.
- **Sistem Antrean Skala Besar:** Mendukung hingga 1.000 berkas antrean (`queue_0.csv` s.d `queue_999.csv`), memungkinkan penyimpanan lokal hingga 50.000 data presensi saat offline.

### 3. Validasi Integritas Data

- **Sliding Window Duplicate Check:** Algoritma pencegahan duplikasi data tidak memindai seluruh penyimpanan, melainkan menggunakan metode _sliding window_ (memeriksa berkas antrean aktif dan 2 berkas sebelumnya). Ini menjaga performa tetap cepat (O(1)) meskipun jumlah data tersimpan sangat besar.
- **Validasi Waktu Ketat:** Sinkronisasi waktu NTP dilakukan secara berkala (setiap 1 jam) untuk memastikan stempel waktu (_timestamp_) tetap akurat.

### 4. Stabilitas Sistem

- **Penanganan Kesalahan Fatal:** Mekanisme _Fail-Safe_ yang me-restart perangkat secara otomatis jika terjadi kegagalan kritis pada modul RFID atau inisialisasi sistem berkas.
- **Auto-Reconnect Cerdas:** Sistem akan mencoba reconnect WiFi setiap 5 menit secara background jika koneksi terputus, tanpa mengganggu operasi normal.
- **Indikator Visual Presisi:** Umpan balik visual pada OLED yang lebih responsif, termasuk bilah progres (_progress bar_) dan status antrean _real-time_.

## Spesifikasi Perangkat Keras dan Pinout

Sistem menggunakan bus SPI bersama (_Shared SPI Bus_) untuk komunikasi dengan modul RFID dan SD Card. Manajemen lalu lintas data diatur melalui manajemen sinyal _Chip Select_ (CS) yang ketat.

| Komponen       | Pin Modul | Pin ESP32-C3 | Protokol | Catatan                        |
| :------------- | :-------- | :----------- | :------- | :----------------------------- |
| **Bus SPI**    | SCK       | GPIO 4       | SPI      | Jalur Clock (Shared)           |
|                | MOSI      | GPIO 6       | SPI      | Jalur Data Master Out (Shared) |
|                | MISO      | GPIO 5       | SPI      | Jalur Data Master In (Shared)  |
| **RFID RC522** | SDA (SS)  | GPIO 7       | SPI      | Chip Select RFID               |
|                | RST       | GPIO 3       | Digital  | Reset Hardwrae                 |
| **SD Card**    | CS        | GPIO 1       | SPI      | Chip Select SD Card            |
| **OLED**       | SDA       | GPIO 8       | I2C      | Data Display                   |
|                | SCL       | GPIO 9       | I2C      | Clock Display                  |
| **Buzzer**     | (+)       | GPIO 10      | PWM      | Indikator Audio                |

> **Penting:** Pastikan implementasi perangkat keras mendukung penggunaan GPIO 4, 5, dan 6 secara paralel untuk dua perangkat SPI berbeda.

## Prasyarat Instalasi Perangkat Lunak

Versi ini memiliki dependensi pustaka yang berbeda dari versi sebelumnya. Pastikan pustaka berikut terinstal:

1.  **SdFat** oleh Bill Greiman (Versi 2.x): **Wajib**. Menggantikan pustaka `SD` bawaan Arduino. Memberikan kinerja I/O yang jauh lebih baik.
2.  **MFRC522** (GithubCommunity): Driver RFID.
3.  **Adafruit SSD1306** & **Adafruit GFX**: Driver tampilan OLED.
4.  **ArduinoJson**: Serialisasi JSON.
5.  **WiFi**, **HTTPClient**, **Wire**, **SPI**: Pustaka inti ESP32.

## Konfigurasi Sistem

Parameter operasional utama didefinisikan pada bagian awal kode sumber:

```cpp
// Konfigurasi Antrean
const int MAX_RECORDS_PER_FILE      = 50;     // Batas aman untuk RAM ESP32-C3
const int MAX_QUEUE_FILES           = 1000;   // Kapasitas total 50.000 record
const unsigned long SYNC_INTERVAL   = 300000; // Sinkronisasi setiap 5 menit (background)

// Konfigurasi Validasi
const unsigned long MIN_REPEAT_INTERVAL = 1800; // Debounce 30 menit untuk kartu sama

// Konfigurasi Reconnect (NEW! v2.2.2)
const unsigned long RECONNECT_INTERVAL = 300000; // Auto-reconnect setiap 5 menit
const unsigned long TIME_SYNC_INTERVAL = 3600000; // Time sync setiap 1 jam

// Background Process Flags (NEW! v2.2.2)
bool isSyncing = false;       // Flag untuk mencegah sync bersamaan
bool isReconnecting = false;  // Flag untuk mencegah reconnect bersamaan
```

## Mekanisme Kerja Background Process (v2.2.2)

### 1. Background WiFi Reconnection

Sistem memiliki fungsi `connectToWiFiBackground()` yang berbeda dengan `connectToWiFi()`:

- **Tanpa Display Feedback:** Tidak ada tampilan "CONNECTING..." atau progress di OLED
- **Silent Operation:** Proses berjalan di latar belakang tanpa bunyi buzzer
- **Non-Blocking:** User tetap bisa melakukan tapping RFID selama proses reconnect
- **Flag Protection:** Menggunakan `isReconnecting` untuk mencegah multiple reconnect simultan

```cpp
// Pseudo-code mekanisme reconnect
if (WiFi.status() != WL_CONNECTED &&
    !isReconnecting &&
    millis() - lastReconnectAttempt >= RECONNECT_INTERVAL) {

    isReconnecting = true;
    connectToWiFiBackground(); // Silent reconnect

    if (WiFi.status() == WL_CONNECTED) {
        pingAPI();
        syncAllQueues(); // Auto sync setelah online
    }

    isReconnecting = false;
}
```

### 2. Background Bulk Sync

Fungsi `syncAllQueues()` telah dimodifikasi untuk operasi background:

- **Silent Sync:** Menghapus semua `showOLED()` dan `playTone()` saat sync
- **Flag Management:** Set `isSyncing = true` di awal, clear setelah selesai
- **Skip Prevention:** `periodicSync()` akan skip jika `isSyncing == true`
- **Automatic Trigger:** Sync otomatis terjadi setelah WiFi restored

```cpp
// Pseudo-code mekanisme sync
bool syncAllQueues() {
    isSyncing = true; // Lock sync process

    // Sync semua queue files tanpa feedback visual/audio
    for (int i = 0; i < MAX_QUEUE_FILES; i++) {
        // ... proses sync silent ...
    }

    isSyncing = false; // Unlock
    return success;
}
```

### 3. Display Update Strategy

Layar OLED hanya menampilkan informasi standby utama:

- **Status Connection:** ONLINE/OFFLINE di kiri atas (auto-update berdasarkan `isOnline` flag)
- **TAP KARTU:** Teks utama di tengah
- **Waktu Real-time:** Jam:menit di bawah teks utama
- **Queue Counter:** Jumlah data pending (Q:xx) jika ada
- **WiFi Signal:** Bar sinyal di kanan atas (hanya jika connected)

**Tidak ada tampilan "SYNCING", "RECONNECTING", atau progress bar** selama operasi background.

## Mekanisme Kerja Antrean (Queue Logic)

1.  **Segmentasi:** Data disimpan dalam berkas kecil (`queue_N.csv`) berisi maksimal 50 baris untuk mencegah _buffer overflow_.
2.  **Rotasi:** Saat berkas `queue_N` penuh, sistem secara otomatis membuat `queue_N+1`.
3.  **Sinkronisasi Background:**
    - Sistem mencari berkas antrean yang ada secara sekuensial.
    - Data dibaca, diparsing ke JSON, dan dikirim ke server **tanpa feedback visual**.
    - Jika server merespons HTTP 200, berkas `queue_N` dihapus secara fisik dari SD Card.
    - Jika gagal, berkas dipertahankan untuk percobaan berikutnya.
    - Proses ini **tidak mengganggu** operasi tapping RFID.

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

- SDSC (Standard Capacity) <= 2GB (termasuk kartu lama 128MB/256MB).
- SDHC (High Capacity) 4GB - 32GB.
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
[WiFi Disconnected] → [Wait 5 minutes] → [Silent Reconnect Attempt]
                                                ↓
                                          [Success?]
                                         ↙          ↘
                                    [Yes]            [No]
                                      ↓                ↓
                              [Update isOnline]   [Retry later]
                              [Auto Sync Queue]
```

## User Experience Improvements (v2.2.2)

### Sebelum v2.2.2:

- Tampilan "RECONNECTING WIFI..." mengganggu user
- Tampilan "SYNCING File 0 (15)" membuat bingung
- Progress bar sync menghambat tapping
- Buzzer berbunyi saat background process

### Sesudah v2.2.2:

- Display hanya menampilkan "TAP KARTU" dan info penting
- Status ONLINE/OFFLINE update otomatis tanpa gangguan
- Sync berjalan diam-diam di background
- Tapping RFID tidak pernah terblokir
- User experience yang mulus dan profesional

## Riwayat Perubahan (Changelog)

### v2.2.2 (Desember 2025) - Background Operations

- **Background WiFi Reconnect:** Reconnect otomatis setiap 5 menit tanpa tampilan
- **Background Bulk Sync:** Sinkronisasi data offline berjalan di latar belakang
- **Dual Process Flags:** Implementasi `isSyncing` dan `isReconnecting` untuk thread safety
- **Silent Operations:** Semua operasi network tanpa feedback visual/audio yang mengganggu
- **Enhanced UX:** Display hanya fokus pada informasi penting (status, waktu, queue counter)
- **Improved Responsiveness:** Tapping RFID tidak pernah terblokir oleh operasi background

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

### Masalah: Sync lambat atau terblokir

**Solusi:** Pastikan tidak ada `showOLED()` atau `playTone()` di dalam `syncAllQueues()`. Semua feedback visual harus dihapus untuk operasi background yang optimal.

### Masalah: Double sync/reconnect

**Solusi:** Periksa implementasi flag `isSyncing` dan `isReconnecting`. Pastikan flag di-set sebelum operasi dan di-clear setelah selesai.

### Masalah: Display tidak update status online/offline

**Solusi:** Pastikan `showStandbySignal()` membaca variable `isOnline` yang selalu diupdate di loop utama.

## Best Practices

1. **Jangan Tambahkan Feedback Visual di Background Process:** Semua operasi yang berjalan di `periodicSync()` dan `connectToWiFiBackground()` harus silent.

2. **Gunakan Flag untuk Thread Safety:** Selalu cek flag sebelum memulai operasi paralel.

3. **Prioritas Responsivitas:** RFID tapping adalah prioritas utama, semua operasi lain harus non-blocking.

4. **Minimal OLED Updates:** Hanya update display untuk informasi critical (status, waktu, queue count).

## Lisensi

Hak Cipta © 2025 Yahya Zulfikri. Kode sumber ini dilisensikan di bawah MIT License untuk penggunaan pendidikan dan pengembangan profesional.
