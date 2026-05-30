# Dokumentasi Teknis
# Sistem Presensi Pintar (RFID) - Queue System v2.3.0

**Author:** Yahya Zulfikri  
**Versi:** 2.3.0  
**Platform:** ESP32-C3 Super Mini  
**Dibuat:** Juli 2025  
**Diperbarui:** Mei 2026  

---

## Daftar Isi

1. [Gambaran Umum](#1-gambaran-umum)
2. [Spesifikasi Hardware](#2-spesifikasi-hardware)
3. [Arsitektur Sistem](#3-arsitektur-sistem)
4. [Konfigurasi Pin](#4-konfigurasi-pin)
5. [Dependensi Library](#5-dependensi-library)
6. [Struktur Data](#6-struktur-data)
7. [Sistem Penyimpanan](#7-sistem-penyimpanan)
8. [Alur Kerja Presensi](#8-alur-kerja-presensi)
9. [Sistem Sinkronisasi](#9-sistem-sinkronisasi)
10. [Manajemen Konektivitas WiFi](#10-manajemen-konektivitas-wifi)
11. [Keamanan dan Enkripsi](#11-keamanan-dan-enkripsi)
12. [OTA Firmware Update](#12-ota-firmware-update)
13. [FreeRTOS Tasks](#13-freertos-tasks)
14. [Manajemen Daya dan Deep Sleep](#14-manajemen-daya-dan-deep-sleep)
15. [Watchdog Timer (WDT)](#15-watchdog-timer-wdt)
16. [Provisioning Mode](#16-provisioning-mode)
17. [Admin RFID](#17-admin-rfid)
18. [Telemetri dan Remote Config](#18-telemetri-dan-remote-config)
19. [API Endpoint](#19-api-endpoint)
20. [Kode Error dan Pesan](#20-kode-error-dan-pesan)
21. [Prosedur Factory Reset](#21-prosedur-factory-reset)
22. [Alur Boot (Flowchart)](#22-alur-boot-flowchart)
23. [Batasan dan Limitasi Sistem](#23-batasan-dan-limitasi-sistem)

---

## 1. Gambaran Umum

Sistem ini adalah mesin presensi berbasis RFID yang berjalan di atas mikrokontroler ESP32-C3 Super Mini. Sistem dirancang untuk beroperasi dalam dua mode:

- **Online:** Data presensi dikirim langsung ke server melalui HTTPS.
- **Offline:** Data disimpan secara lokal di SD card (queue file CSV) atau NVS (Non-Volatile Storage) internal, lalu disinkronisasi secara otomatis ketika koneksi tersedia kembali.

Fitur utama:

- Pembacaan kartu RFID menggunakan modul RC522 (MFRC522).
- Penyimpanan data offline berbasis queue file di SD card dengan kapasitas hingga 60.000 file (25 record per file).
- Enkripsi kredensial WiFi dan API key menggunakan AES-128-CBC dengan kunci turunan dari eFuse MAC.
- Sinkronisasi data secara chunked untuk menghindari timeout.
- Update firmware OTA dari server.
- Manajemen daya dengan deep sleep terjadwal.
- Provisioning awal via Access Point (AP) dan web interface.
- Validasi RFID melalui database lokal di SD card.

---

## 2. Spesifikasi Hardware

| Komponen      | Model / Spesifikasi                  |
|---------------|--------------------------------------|
| MCU           | ESP32-C3 Super Mini                  |
| RFID Reader   | MFRC522 (RC522), antarmuka SPI       |
| Display       | OLED SSD1306, 128x64 px, I2C        |
| Penyimpanan   | MicroSD card, SPI, FAT32            |
| Buzzer        | Passive buzzer, aktif via PWM/tone() |
| Tombol        | BOOT button (GPIO9), pull-up internal|

---

## 3. Arsitektur Sistem

```
+---------------------------+
|         setup()           |  Inisialisasi semua komponen
+---------------------------+
            |
            v
+---------------------------+
|        loop()             |  Baca RFID, cek sleep schedule
+---------------------------+
            |
    +-------+-------+
    |               |
    v               v
+--------+    +-----------+
|taskRfid|    | taskSync  |   FreeRTOS Tasks
+--------+    +-----------+
                    |
            +-------+-------+
            |               |
            v               v
    +-----------+   +---------------+
    |taskDisplay|   | SD / WiFi / API|
    +-----------+   +---------------+
```

Sistem menggunakan tiga FreeRTOS task yang berjalan paralel di core 0:

- **taskRfid:** Memproses event scan kartu dari queue.
- **taskSync:** Mengelola sinkronisasi data, OTA, telemetri, dan koneksi WiFi.
- **taskDisplay:** Mengelola tampilan OLED dan feedback visual.

Loop utama (`loop()`) berjalan di core 0 untuk membaca RFID hardware secara langsung dan memeriksa jadwal deep sleep.

---

## 4. Konfigurasi Pin

| Fungsi         | GPIO |
|----------------|------|
| SPI SCK        | 4    |
| SPI MOSI       | 6    |
| SPI MISO       | 5    |
| RFID SS (CS)   | 7    |
| RFID RST       | 3    |
| SD Card CS     | 1    |
| OLED SDA       | 8    |
| OLED SCL       | 9    |
| Buzzer         | 10   |
| BOOT Button    | 9    |

Catatan: PIN_BOOT (GPIO9) berbagi dengan PIN_OLED_SCL. Ini digunakan untuk mendeteksi tombol BOOT pada saat runtime (taskDisplay), sementara SCL digunakan selama inisialisasi I2C di setup.

---

## 5. Dependensi Library

| Library               | Kegunaan                                    |
|-----------------------|---------------------------------------------|
| WiFi.h                | Koneksi WiFi                                |
| WiFiClientSecure.h    | HTTPS / TLS client                          |
| HTTPClient.h          | HTTP request GET/POST                       |
| Wire.h                | Komunikasi I2C (OLED)                       |
| MFRC522.h             | Driver RFID RC522                           |
| SPI.h                 | Komunikasi SPI (RFID, SD)                   |
| Adafruit_SSD1306.h    | Driver OLED SSD1306                         |
| ArduinoJson.h         | Parsing dan serialisasi JSON                |
| SdFat.h               | Manajemen file SD card                      |
| Preferences.h         | NVS read/write                              |
| Update.h / esp_ota    | OTA firmware update                         |
| WebServer.h           | Web server untuk provisioning               |
| DNSServer.h           | Captive portal DNS                          |
| mbedtls/aes.h         | Enkripsi AES-128-CBC                        |
| mbedtls/md.h          | Hashing SHA-256 untuk derivasi kunci        |
| FreeRTOS              | Task, semaphore, queue                      |
| esp_task_wdt.h        | Watchdog timer                              |
| time.h                | NTP dan manajemen waktu                     |

---

## 6. Struktur Data

### OfflineRecord
Menyimpan satu data presensi.

| Field       | Tipe          | Ukuran | Keterangan                     |
|-------------|---------------|--------|--------------------------------|
| rfid        | char[]        | 11     | UID kartu RFID (10 digit + \0) |
| timestamp   | char[]        | 20     | Format: YYYY-MM-DD HH:MM:SS    |
| deviceId    | char[]        | 20     | ID atau nama perangkat         |
| unixTime    | unsigned long | 4      | Unix timestamp saat scan       |

### RfidScanEvent
Event yang dikirim dari loop() ke taskRfid melalui FreeRTOS queue.

| Field   | Tipe    | Keterangan                     |
|---------|---------|--------------------------------|
| uid     | uint8_t[10] | Raw UID byte dari RC522   |
| uidLen  | uint8_t | Panjang UID (biasanya 4 byte)  |

### EncryptedCredential
Format penyimpanan kredensial terenkripsi di NVS.

| Field | Tipe       | Ukuran | Keterangan              |
|-------|------------|--------|-------------------------|
| iv    | uint8_t[16]| 16     | IV acak untuk AES-CBC   |
| data  | uint8_t[48]| 48     | Ciphertext (padded)     |
| len   | uint8_t    | 1      | Panjang plaintext asli  |

### RuntimeConfig
Konfigurasi yang dapat diubah via remote config.

| Field              | Default | Keterangan                        |
|--------------------|---------|-----------------------------------|
| sleepStartHour     | 18      | Jam mulai deep sleep              |
| sleepEndHour       | 5       | Jam bangun dari deep sleep        |
| dimStartHour       | 8       | Jam OLED dimatikan                |
| dimEndHour         | 12      | Jam OLED dinyalakan kembali       |
| syncIntervalMs     | 300000  | Interval sinkronisasi (ms)        |
| otaCheckIntervalMs | 30000   | Interval cek OTA (ms)             |

---

## 7. Sistem Penyimpanan

### 7.1 SD Card - Queue File System

Data presensi offline disimpan dalam format CSV di SD card. Setiap file menampung maksimal 25 record.

**Format nama file:** `/queue_N.csv` (N = 0 hingga 59999)

**Format isi file:**
```
rfid,timestamp,device_id,unix_time,crc8
1234567890,2025-07-15 07:30:00,GERBANG UTAMA,1752550200,A3
```

**Mekanisme rolling:**
- Ketika file aktif sudah mencapai 25 record, sistem berpindah ke file berikutnya.
- Jika slot berikutnya masih berisi data valid, sistem melaporkan SAVE_QUEUE_FULL.
- Jika slot berikutnya kosong atau semua record sudah kadaluwarsa (> 1 tahun), slot akan di-reset.

**Validasi integritas:** Setiap record memiliki field CRC8 yang dihitung dari kombinasi UID (10 byte) dan unix_time (4 byte). Record dengan CRC tidak cocok akan dibuang saat sinkronisasi.

**File metadata:** `/queue_meta.txt` menyimpan `cachedPendingRecords,currentQueueFile` untuk mempercepat boot tanpa harus menghitung ulang semua file.

**File RFID database:** `/rfid_db.txt` berisi daftar UID yang diizinkan, satu UID per baris (10 digit desimal).

**File admin RFID:** `/admin_rfid.txt` berisi maksimal 5 UID dengan hak akses admin.

**File log gagal:** `/failed_log.csv` mencatat record yang ditolak server, maksimal 500 baris.

### 7.2 NVS (Non-Volatile Storage)

NVS digunakan sebagai buffer fallback ketika SD card tidak tersedia, dengan kapasitas maksimal 40 record.

| Namespace  | Key             | Keterangan                         |
|------------|-----------------|------------------------------------|
| presensi   | nvs_count       | Jumlah record tersimpan            |
| presensi   | rec_N           | Record ke-N (OfflineRecord bytes)  |
| presensi   | last_time       | Unix time terakhir yang valid      |
| presensi   | rfid_db_ver     | Versi database RFID lokal          |
| presensi   | scan_date       | Tanggal hitungan scan hari ini     |
| presensi   | scan_count      | Jumlah scan hari ini               |
| presensi   | last_rfid       | UID terakhir yang di-scan          |
| presensi   | last_scan_t     | Unix time scan terakhir            |
| cfg        | ssid1/2/3       | SSID WiFi (terenkripsi)            |
| cfg        | pass1/2/3       | Password WiFi (terenkripsi)        |
| cfg        | apikey          | API key (terenkripsi)              |
| cfg        | devname         | Nama perangkat (terenkripsi)       |
| cfg        | prov            | Status provisioning (bool)         |

---

## 8. Alur Kerja Presensi

### 8.1 Fungsi `kirimPresensi(rfid, msg)`

```
Mulai
  |
  +-- Waktu valid? --> Tidak --> Return FALSE "WAKTU INVALID"
  |
  +-- SD tersedia?
  |     |
  |     +-- RFID ada di cache? --> Tidak --> Return FALSE "HUBUNGI ADMIN"
  |     |
  |     +-- Recent scan (< 30 menit)? --> Ya --> Return FALSE "CUKUP SEKALI!"
  |     |
  |     +-- saveToQueue()
  |           |-- SAVE_OK         --> Return TRUE "DATA TERSIMPAN" / "QUEUE HAMPIR PENUH!"
  |           |-- SAVE_DUPLICATE  --> Return FALSE "CUKUP SEKALI!"
  |           |-- SAVE_QUEUE_FULL --> Return FALSE "QUEUE PENUH!"
  |           |-- SAVE_SD_ERROR   --> Return FALSE "SD CARD ERROR"
  |
  +-- WiFi tersedia? (fallback tanpa SD)
  |     |
  |     +-- kirimLangsung() --> OK  --> Return TRUE
  |     |                  --> 400  --> Return FALSE "CUKUP SEKALI!"
  |     |                  --> 403  --> Return FALSE "HARI LIBUR!"
  |     |                  --> 404  --> Return FALSE "RFID NONAKTIF"
  |     |
  |     +-- HTTP gagal --> nvsSaveToBuffer()
  |           |-- OK   --> Return TRUE "BUFFER N/40"
  |           |-- Full --> Return FALSE "BUFFER PENUH!"
  |
  +-- Offline total --> nvsSaveToBuffer()
        |-- OK   --> Return TRUE "BUFFER N/40"
        |-- Full --> Return FALSE "BUFFER PENUH!"
```

### 8.2 Konversi UID ke String

UID 4 byte dari RC522 dikonversi ke format 10 digit desimal dengan urutan little-endian:

```
value = uid[3]<<24 | uid[2]<<16 | uid[1]<<8 | uid[0]
output = sprintf("%010lu", value)
```

### 8.3 Deteksi Duplikat

Sistem memeriksa duplikat di dua tempat:

1. **NVS:** Fungsi `nvsIsRecentScan()` mengecek UID dan timestamp terakhir.
2. **SD Card:** Fungsi `isDuplicateLocked()` memeriksa 3 file queue terakhir, maksimal 26 baris per file. Scan dianggap duplikat jika selisih waktu kurang dari 1800 detik (30 menit).

---

## 9. Sistem Sinkronisasi

### 9.1 Chunked Sync

Sinkronisasi dilakukan secara bertahap (`chunkedSync()`) untuk menghindari timeout pada koneksi lambat:

- Memproses maksimal 5 file per siklus (`MAX_SYNC_FILES_PER_CYCLE`).
- Jika ditemukan 20 slot kosong berturut-turut, proses dianggap selesai.
- Status `syncState.inProgress` dipertahankan antar panggilan sehingga sesi berikutnya melanjutkan dari file terakhir.

### 9.2 Sync dengan Retry

Setiap file disinkronisasi dengan mekanisme retry eksponensial:

- Maksimal 2 retry (`MAX_SYNC_RETRIES`).
- Delay retry: `2000ms * 2^attempt` (2 detik, lalu 4 detik).

### 9.3 Format Payload Sinkronisasi

```json
{
  "data": [
    {
      "rfid": "1234567890",
      "timestamp": "2025-07-15 07:30:00",
      "device_id": "GERBANG UTAMA",
      "sync_mode": true
    }
  ]
}
```

### 9.4 NVS Sync

Record di buffer NVS disinkronisasi melalui endpoint yang sama. Setelah berhasil, seluruh record NVS dihapus.

---

## 10. Manajemen Konektivitas WiFi

### 10.1 Konfigurasi

Mendukung 3 SSID dengan fallback otomatis. Semua kredensial disimpan terenkripsi di NVS.

### 10.2 State Machine Reconnect

| State             | Transisi ke                  | Kondisi                               |
|-------------------|------------------------------|---------------------------------------|
| RECONNECT_IDLE    | RECONNECT_INIT               | WiFi putus, interval 300 detik lewat  |
| RECONNECT_INIT    | RECONNECT_TRYING             | WiFi disconnect, pilih SSID berikutnya|
| RECONNECT_TRYING  | RECONNECT_SUCCESS / FAILED   | Connected atau timeout 15 detik       |
| RECONNECT_SUCCESS | RECONNECT_IDLE               | Sinkronisasi waktu dan data dilakukan |
| RECONNECT_FAILED  | RECONNECT_IDLE               | Tandai offline                        |

### 10.3 Kualitas Sinyal

| RSSI            | Level | Keterangan             |
|-----------------|-------|------------------------|
| > -67 dBm       | 4     | Sangat kuat            |
| -70 s/d -67 dBm | 3     | Kuat                   |
| -80 s/d -70 dBm | 2     | Sedang                 |
| -90 s/d -80 dBm | 1     | Lemah (SIGNAL_WEAK)    |
| < -90 dBm       | 0     | Kritis (SIGNAL_CRITICAL)|

Jika sinyal kritis, operasi jaringan (NTP, sync, OTA) dibatalkan.

---

## 11. Keamanan dan Enkripsi

### 11.1 Derivasi Kunci AES

Kunci AES-128 diturunkan secara deterministik dari MAC address eFuse perangkat menggunakan SHA-256:

```
seed = MAC[6] + "ZEDLABS_PRESENSI"[16]  (total 22 byte)
hash = SHA256(seed)
key  = hash[0:15]  (16 byte pertama)
```

Kunci tidak disimpan di flash, melainkan diturunkan ulang setiap kali dibutuhkan dari hardware ID yang unik per perangkat.

### 11.2 Enkripsi Kredensial

- Algoritma: AES-128-CBC
- IV: 16 byte acak yang di-generate menggunakan `esp_fill_random()` setiap kali enkripsi.
- Plaintext di-pad ke blok 48 byte sebelum enkripsi.
- IV disimpan bersama ciphertext dalam struct `EncryptedCredential`.
- Panjang string yang dapat dienkripsi maksimal 47 karakter.

### 11.3 Integritas Record

Setiap record di SD card dilindungi dengan CRC8 (polinomial 0x07) dari 10 byte RFID + 4 byte unix_time. Record dengan CRC tidak cocok dibuang saat pembacaan.

### 11.4 Transport Security

Semua komunikasi HTTP menggunakan TLS (`WiFiClientSecure`). Verifikasi sertifikat dinonaktifkan (`setInsecure()`) dengan handshake timeout 10 detik. API key dikirim pada setiap request melalui header `X-API-KEY`.

---

## 12. OTA Firmware Update

### 12.1 Alur Pemeriksaan

1. Setiap `otaCheckIntervalMs` (default 30 detik), sistem mengirim POST ke `/api/presensi/firmware/check` dengan payload `{"version":"2.3.0","device_id":"..."}`.
2. Server merespons dengan `{"update":true,"version":"X.Y.Z","url":"...","md5":"..."}`.
3. Jika versi server lebih baru, status `otaState.updateAvailable` diset true.

### 12.2 Proses Update

1. WDT diperpanjang ke 180 detik.
2. Firmware diunduh streaming dari URL yang diberikan server.
3. `Update.setMD5()` digunakan untuk verifikasi integritas.
4. Jika berhasil, perangkat restart otomatis.
5. Setelah boot, `esp_ota_mark_app_valid_cancel_rollback()` dipanggil untuk mengonfirmasi firmware baru.

### 12.3 Kondisi OTA Dibatalkan

- Sinyal WiFi lemah (`isSignalWeak()`).
- Sedang menampilkan feedback RFID (`rfidFeedback.active`).
- HTTP response code bukan 200.
- Tidak cukup ruang flash (`Update.begin()` gagal).

---

## 13. FreeRTOS Tasks

| Task         | Stack   | Prioritas | Core | Fungsi Utama                                    |
|--------------|---------|-----------|------|-------------------------------------------------|
| taskRfid     | 8192    | 3         | 0    | Proses event scan RFID dari queue               |
| taskSync     | 8192    | 2         | 0    | Sinkronisasi data, OTA, telemetri, reconnect    |
| taskDisplay  | 12288   | 1         | 0    | Update OLED, cek jadwal dim, factory reset      |
| loop (main)  | default | default   | 0/1  | Baca RFID hardware, cek sleep schedule          |

### Sinkronisasi Antar Task

- `xSdMutex`: Mutex untuk semua akses SD card. Timeout default 5000 ms.
- `xDisplayMutex`: Mutex untuk akses display SSD1306. Timeout 50-200 ms.
- `xRfidQueue`: Queue dengan kapasitas 8 event `RfidScanEvent` dari loop ke taskRfid.

---

## 14. Manajemen Daya dan Deep Sleep

### 14.1 Jadwal Sleep

Default: tidur pukul 18:00, bangun pukul 05:00. Dapat diubah via remote config.

### 14.2 Prosedur Masuk Sleep

1. Cek apakah sinkronisasi sedang berjalan; jika ya, tunda.
2. Set flag `sleepRequested = true` untuk menghentikan aktivitas semua task.
3. Tunggu 5 detik (`DEEP_SLEEP_TASK_WAIT_MS`) agar task idle.
4. Flush semua file SD card terbuka.
5. Matikan OLED.
6. Hitung durasi sleep berdasarkan waktu saat ini dan jam bangun target.
7. Simpan `sleepDurationSeconds` di RTC memory.
8. Hapus semua task dari WDT, deinit WDT.
9. Set timer wakeup, panggil `esp_deep_sleep_start()`.

### 14.3 Pemulihan Waktu Setelah Sleep

Setelah bangun dari deep sleep, sistem menambahkan `sleepDurationSeconds` ke `lastValidTime` yang tersimpan di RTC memory, sehingga waktu tetap akurat tanpa menunggu sinkronisasi NTP.

### 14.4 Estimasi Waktu Tanpa NTP

Jika NTP tidak tersedia, sistem menggunakan estimasi berbasis `millis()` sejak boot:

```
waktu_estimasi = lastValidTime + (millis() - bootTime) / 1000
```

Estimasi dianggap tidak valid jika lebih dari 12 jam (`MAX_TIME_ESTIMATE_AGE = 43200`).

---

## 15. Watchdog Timer (WDT)

| Kondisi          | Timeout    |
|------------------|------------|
| Normal           | 90 detik   |
| Saat Sync / OTA  | 180 detik  |

Fungsi `extendWdtForSync()` dan `restoreWdtNormal()` mengelola perubahan timeout secara thread-safe menggunakan `portMUX_TYPE wdtMux`. Semua task dan idle task core 0 didaftarkan ke WDT.

---

## 16. Provisioning Mode

### 16.1 Trigger

Provisioning aktif jika:
- Key `prov` di NVS belum ada atau bernilai false.
- API key atau SSID utama kosong setelah `loadCredentials()`.

### 16.2 Mekanisme

1. Perangkat membuat Access Point dengan SSID `ATTENDANCE MACHINE`, password `P@ssw0rd`.
2. DNS server menjawab semua query ke IP AP (captive portal).
3. Web server melayani halaman konfigurasi di port 80.
4. Pengguna mengisi form: SSID 1-3, password 1-3, API key, nama perangkat.
5. Setelah submit, kredensial dienkripsi dan disimpan ke NVS, perangkat restart.
6. Jika tidak ada koneksi dalam 300 detik (`PROVISIONING_TIMEOUT_MS`), perangkat restart otomatis.

---

## 17. Admin RFID

UID yang terdaftar di `/admin_rfid.txt` (maksimal 5 UID) mendapat perlakuan khusus saat di-scan:

1. Menampilkan status perangkat: jumlah queue dan scan count hari ini.
2. Tidak menyimpan record presensi.
3. Memicu sinkronisasi manual jika WiFi tersedia.

---

## 18. Telemetri dan Remote Config

### 18.1 Telemetri Heartbeat

Dikirim setiap 300 detik ke `/api/presensi/heartbeat`:

```json
{
  "device_id": "GERBANG UTAMA",
  "device_name": "GERBANG UTAMA",
  "firmware": "2.3.0",
  "uptime_sec": 3600,
  "heap_free": 180000,
  "pending_records": 15,
  "scan_today": 42,
  "rssi": -65,
  "sd_ok": true,
  "rfid_db_entries": 350,
  "online": true
}
```

### 18.2 Remote Config

Diambil setiap 600 detik dari `/api/presensi/config?device_id=...`:

```json
{
  "sleep_start": 18,
  "sleep_end": 5,
  "oled_dim_start": 8,
  "oled_dim_end": 12,
  "sync_interval_ms": 300000,
  "ota_check_interval_ms": 30000
}
```

---

## 19. API Endpoint

Semua endpoint menggunakan base URL `https://presensi.mtsn1pandeglang.sch.id` dan memerlukan header `X-API-KEY`.

| Method | Endpoint                           | Fungsi                               |
|--------|------------------------------------|--------------------------------------|
| GET    | /api/presensi/ping                 | Cek konektivitas API                 |
| POST   | /api/presensi                      | Kirim presensi langsung (realtime)   |
| POST   | /api/presensi/sync-bulk            | Kirim batch record offline           |
| GET    | /api/presensi/rfid-list            | Unduh database RFID (teks)           |
| GET    | /api/presensi/rfid-list/version    | Cek versi database RFID              |
| POST   | /api/presensi/firmware/check       | Cek ketersediaan update firmware     |
| GET    | /api/presensi/firmware/[url]       | Unduh binary firmware                |
| POST   | /api/presensi/heartbeat            | Kirim data telemetri                 |
| GET    | /api/presensi/config               | Ambil konfigurasi remote             |

### Format Response Presensi Langsung

| HTTP Code | Makna                            |
|-----------|----------------------------------|
| 200       | Presensi berhasil tercatat       |
| 400       | Sudah presensi hari ini          |
| 403       | Hari libur / di luar jadwal      |
| 404       | RFID tidak dikenal / nonaktif    |

### Format Response RFID Database

Baris pertama: `ver:<unix_timestamp>`  
Baris berikutnya: satu UID 10 digit per baris.

---

## 20. Kode Error dan Pesan

| Pesan               | Penyebab                                           |
|---------------------|----------------------------------------------------|
| WAKTU INVALID       | Waktu belum tersinkronisasi dan estimasi tidak ada |
| HUBUNGI ADMIN       | UID tidak ada di rfid_db.txt                       |
| CUKUP SEKALI!       | Duplikat dalam interval 30 menit                   |
| DATA TERSIMPAN      | Berhasil disimpan ke queue SD card                 |
| QUEUE HAMPIR PENUH! | Jumlah file queue mendekati 48.000                 |
| QUEUE PENUH!        | Semua slot queue terisi data valid                 |
| SD CARD ERROR       | Gagal menulis ke SD card                           |
| HARI LIBUR!         | Server menolak (HTTP 403)                          |
| RFID NONAKTIF       | Server menolak (HTTP 404)                          |
| BUFFER N/40         | Disimpan ke NVS buffer (tanpa SD)                  |
| BUFFER PENUH!       | NVS buffer sudah penuh (40 record)                 |
| PRESENSI OK         | Berhasil dikirim langsung ke server                |
| SERVER ERR N        | HTTP error tidak terduga dari server               |

---

## 21. Prosedur Factory Reset

1. Tekan dan tahan tombol BOOT selama minimal 5 detik saat perangkat berjalan.
2. Konfirmasi akan ditampilkan di OLED.
3. Sistem akan menghapus semua namespace NVS (`cfg` dan `presensi`).
4. File `/rfid_db.txt`, `/queue_meta.txt`, dan `/failed_log.csv` dihapus dari SD card.
5. Perangkat restart dan masuk ke provisioning mode.

Catatan: File queue presensi (`/queue_N.csv`) tidak dihapus saat factory reset.

---

## 22. Alur Boot (Flowchart)

```
Power ON
  |
  +-- Init WDT (90s)
  +-- OTA rollback cancel
  +-- Init OLED, Buzzer, BOOT pin
  +-- Animasi startup
  +-- Buat FreeRTOS mutex dan queue
  +-- Buat Device ID dari MAC address
  +-- Pulihkan waktu dari RTC / NVS
  |
  +-- checkProvisioned()
  |     |-- Belum --> startProvisioningMode() --> [restart setelah save]
  |
  +-- loadCredentials()
  +-- Validasi API key dan SSID
  |     |-- Kosong --> startProvisioningMode()
  |
  +-- Override deviceId dengan deviceName jika ada
  +-- Init SPI
  +-- Init SD Card
  |     |-- OK  --> loadMetadata, refreshPendingCache, loadRfidCache, loadAdminList
  |     |-- FAIL --> tampilkan peringatan, cek NVS buffer
  |
  +-- connectToWiFi()
  |     |-- OK  --> syncTime, pingAPI, nvsSyncToServer, chunkedSync, checkRfidDb
  |     |-- FAIL --> offline mode
  |
  +-- Init RC522
  |     |-- Gagal detect --> restart
  |
  +-- Tampilkan "SISTEM SIAP"
  +-- Inisialisasi timers
  +-- Start taskRfid, taskSync, taskDisplay
  +-- Masuk loop()
```

---

## 23. Batasan dan Limitasi Sistem

| Parameter                    | Nilai           |
|------------------------------|-----------------|
| Kapasitas queue SD card      | 1.500.000 record (60.000 file x 25) |
| Kapasitas NVS buffer         | 40 record       |
| Kapasitas RFID cache memori  | 5.000 UID       |
| Kapasitas admin RFID         | 5 UID           |
| Maksimal panjang device name | 31 karakter     |
| Panjang API key              | Maks 47 karakter|
| Umur record offline          | 1 tahun (31.536.000 detik) |
| Interval minimum re-scan     | 30 menit (1.800 detik)     |
| Baris maksimal failed_log    | 500 baris       |
| Timeout HTTP presensi langsung | 4 detik      |
| Timeout HTTP sinkronisasi bulk | 45 detik     |
| Timeout HTTP OTA download    | 60 detik        |
| Kecepatan SD card SPI        | 10 MHz          |
