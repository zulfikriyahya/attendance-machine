# SPESIFIKASI KEBUTUHAN PERANGKAT LUNAK (SRS)
## Sistem: ATTENDANCE MACHINE (ESP32-C3)

Dokumen ini disusun murni berdasarkan isi source code yang diberikan (file firmware ESP32-C3, Arduino IDE 2.3.6, Author: Yahya Zulfikri, Versi Firmware: 2.3.1). Tidak ada penambahan fitur di luar apa yang secara eksplisit terdapat dalam kode.

---

## 1. PENDAHULUAN

### 1.1 Tujuan
Mendeskripsikan kebutuhan fungsional dan non-fungsional dari firmware "Attendance Machine" yaitu perangkat presensi berbasis RFID dengan mikrokontroler ESP32-C3 Super Mini, yang terhubung ke backend server melalui HTTP/HTTPS.

### 1.2 Ruang Lingkup
Perangkat lunak berjalan pada mikrokontroler ESP32-C3, memiliki fungsi utama:
- Membaca kartu RFID (MFRC522) dan mengirimkan data presensi ke server backend.
- Menyimpan data presensi secara lokal (SD Card dan/atau NVS) apabila koneksi jaringan tidak tersedia.
- Menyediakan mode provisioning berbasis Access Point dan web server untuk konfigurasi awal.
- Melakukan sinkronisasi waktu (NTP), sinkronisasi database RFID, pembaruan firmware (OTA), pengiriman telemetri, dan pengambilan konfigurasi jarak jauh.
- Menampilkan status pada layar OLED dan memberikan umpan balik suara melalui buzzer.
- Mengatur mode deep sleep berbasis jadwal jam.

### 1.3 Definisi dan Akronim
- RFID: Radio Frequency Identification
- NVS: Non-Volatile Storage (Preferences ESP32)
- OTA: Over-The-Air (pembaruan firmware)
- NTP: Network Time Protocol
- WDT: Watchdog Timer
- SD: Secure Digital (kartu penyimpanan eksternal)
- AP: Access Point
- CRC: Cyclic Redundancy Check

---

## 2. DESKRIPSI UMUM SISTEM

### 2.1 Perangkat Keras yang Digunakan
| Komponen | Pin | Keterangan |
|---|---|---|
| SPI SCK | GPIO4 | Shared bus SD Card & RFID |
| SPI MOSI | GPIO6 | Shared bus SD Card & RFID |
| SPI MISO | GPIO5 | Shared bus SD Card & RFID |
| RFID SS (Chip Select) | GPIO7 | MFRC522 |
| RFID RST | GPIO3 | MFRC522 |
| SD Card CS | GPIO1 | SdFat |
| OLED SDA | GPIO8 | I2C |
| OLED SCL | GPIO9 | I2C |
| Buzzer | GPIO10 | Output tone |
| Tombol Boot (Factory Reset) | GPIO9 | Input pull-up |

Catatan: PIN_BOOT dan PIN_OLED_SCL sama-sama didefinisikan sebagai GPIO9 (lihat bagian Catatan Kekurangan).

### 2.2 Pustaka (Library) yang Digunakan
WiFi.h, WiFiClientSecure.h, HTTPClient.h, Wire.h, MFRC522.h, SPI.h, Adafruit_SSD1306.h, ArduinoJson.h, time.h, SdFat.h, esp_mac.h, esp_efuse_table.h, esp_task_wdt.h, Preferences.h, Update.h, esp_ota_ops.h, WebServer.h, DNSServer.h, mbedtls/aes.h, mbedtls/md.h, freertos/FreeRTOS.h, freertos/task.h, freertos/semphr.h, freertos/queue.h.

### 2.3 Parameter Konfigurasi Statis Utama
| Parameter | Nilai | Keterangan |
|---|---|---|
| DEBOUNCE_TIME | 150 ms | Debounce pembacaan kartu RFID |
| SYNC_INTERVAL | 300000 ms (5 menit) | Interval sinkronisasi default |
| MAX_OFFLINE_AGE | 31536000 detik (1 tahun) | Umur maksimum record offline valid |
| MIN_REPEAT_INTERVAL | 1800 detik (30 menit) | Interval minimum antar-scan kartu sama |
| TIME_SYNC_INTERVAL | 1800000 ms (30 menit) | Interval sinkronisasi ulang waktu NTP |
| RECONNECT_INTERVAL | 60000 ms | Interval percobaan reconnect WiFi |
| RECONNECT_TIMEOUT | 20000 ms | Timeout percobaan reconnect |
| MAX_RECORDS_PER_FILE | 25 | Jumlah maksimum record per file antrian |
| MAX_QUEUE_FILES | 60000 | Jumlah maksimum file antrian pada SD |
| QUEUE_WARN_THRESHOLD | 48000 | Ambang batas peringatan antrian hampir penuh |
| MAX_SYNC_FILES_PER_CYCLE | 5 | Jumlah file yang disinkronkan per siklus |
| MAX_SYNC_RETRIES | 2 | Jumlah percobaan ulang sinkronisasi per file |
| FAILED_LOG_MAX_LINES | 500 | Batas baris log kegagalan |
| NVS_MAX_RECORDS | 40 | Kapasitas buffer NVS offline |
| RFID_CACHE_MAX | 5000 | Kapasitas cache RFID di RAM |
| SLEEP_START_HOUR_DEFAULT | 18 | Default mulai sleep |
| SLEEP_END_HOUR_DEFAULT | 5 | Default akhir sleep |
| OLED_DIM_START_HOUR_DEFAULT | 8 | Default mulai OLED mati |
| OLED_DIM_END_HOUR_DEFAULT | 12 | Default akhir OLED mati |
| GMT_OFFSET_SEC | 25200 (GMT+7) | Zona waktu |
| SIGNAL_THRESHOLD_WEAK | -85 dBm | Ambang sinyal lemah |
| SIGNAL_THRESHOLD_CRITICAL | -90 dBm | Ambang sinyal kritis |
| WDT_NORMAL_TIMEOUT_MS | 90000 ms | Timeout watchdog normal |
| WDT_SYNC_TIMEOUT_MS | 180000 ms | Timeout watchdog saat sync/OTA |
| PROVISIONING_TIMEOUT_MS | 300000 ms | Timeout mode provisioning |
| FACTORY_RESET_HOLD_MS | 5000 ms | Waktu tahan tombol untuk factory reset |
| DEVICE_NAME_MAX_LEN | 31 | Panjang maksimum nama perangkat |

---

## 3. KEBUTUHAN FUNGSIONAL

### 3.1 Provisioning Perangkat
- Sistem memeriksa status provisioning melalui NVS (`NVS_KEY_PROVISIONED`).
- Jika belum diprovisioning, atau apiKey/SSID1 kosong, sistem masuk ke mode Access Point dengan SSID `ATTENDANCE MACHINE` dan password `P@ssw0rd`.
- Sistem menjalankan DNS Server (captive portal, port 53) dan Web Server (port 80).
- Halaman web (`/`) menampilkan formulir konfigurasi: SSID/Password utama dan dua cadangan, API URL, API Key, Nama Perangkat, jadwal jam sleep (mulai/selesai), jadwal jam dim OLED (mulai/selesai).
- Endpoint `/save` (POST) melakukan validasi:
  - SSID1, API Key, API URL wajib diisi.
  - API URL harus diawali `http://` atau `https://`.
  - Trailing slash pada API URL dihapus.
  - Panjang API URL maksimum 79 karakter, jika melebihi maka ditolak.
  - Nama perangkat dipotong otomatis jika melebihi 31 karakter.
  - Jam divalidasi pada rentang 0–23, jika tidak valid menggunakan nilai default.
- Data kredensial disimpan terenkripsi AES-128 CBC ke NVS namespace `cfg`.
- Setelah data tersimpan, perangkat memanggil `markProvisioned()` dan melakukan restart otomatis.
- Jika tidak ada request dalam `PROVISIONING_TIMEOUT_MS` (5 menit), perangkat restart otomatis.
- Endpoint yang tidak dikenal (`onNotFound`) diarahkan kembali ke halaman provisioning.

### 3.2 Manajemen Koneksi WiFi
- Mendukung hingga 3 kredensial WiFi (SSID1–SSID3) tersimpan terenkripsi di NVS.
- `connectToWiFi()` mencoba SSID1, kemudian SSID2, kemudian SSID3 secara berurutan saat boot.
- Setiap percobaan koneksi menampilkan animasi "CONNECTING..." di OLED, maksimum 20 iterasi @300 ms (6 detik) per SSID.
- Mode WiFi diatur STA, TX power 19.5 dBm, WiFi sleep mode WIFI_PS_MAX_MODEM, auto-reconnect aktif, IPv6 dinonaktifkan.
- Reconnect otomatis dikelola melalui mesin status (`RECONNECT_IDLE`, `RECONNECT_INIT`, `RECONNECT_TRYING`, `RECONNECT_SUCCESS`, `RECONNECT_FAILED`) dijalankan pada task sinkronisasi setiap 1 detik.
- Saat status RECONNECT_INIT, sistem mencoba SSID berikutnya secara round-robin dari indeks SSID aktif terakhir.
- Saat status RECONNECT_SUCCESS: dilakukan sinkronisasi waktu, sinkronisasi buffer NVS, dan sinkronisasi antrian SD (jika tersedia).
- Fungsi `isSignalWeak()` dan `isSignalCritical()` digunakan sebagai syarat sebelum melakukan operasi jaringan (RSSI < -85 dBm dianggap lemah, < -90 dBm dianggap kritis; tidak terkoneksi WiFi dianggap keduanya).

### 3.3 Sinkronisasi Waktu
- Server NTP yang dicoba secara berurutan: `pool.ntp.org`, `time.google.com`, `id.pool.ntp.org`, masing-masing dicoba maksimum 2.5 detik.
- Waktu tervalidasi apabila `tm_year >= 120` (tahun ≥ 2020).
- Waktu valid disimpan ke variabel RTC memory (`lastValidTime`, `timeWasSynced`, `bootTime`) dan ke NVS (`nvsSaveLastTime`).
- Jika NTP gagal, sistem menggunakan estimasi waktu berbasis `lastValidTime + elapsed millis()` selama tidak melebihi `MAX_TIME_ESTIMATE_AGE` (12 jam / 43200 detik).
- `periodicTimeSync()` dijalankan setiap `TIME_SYNC_INTERVAL` (30 menit) apabila sinyal tidak kritis.
- Setelah bangun dari deep sleep, `lastValidTime` disesuaikan dengan menambahkan `sleepDurationSeconds` yang tersimpan di RTC memory.

### 3.4 Pembacaan Kartu RFID
- Pembacaan kartu dilakukan pada `loop()` utama menggunakan `PICC_IsNewCardPresent()` dan `PICC_ReadCardSerial()`, hasil UID dikirim ke antrian FreeRTOS (`xRfidQueue`, kapasitas 8) untuk diproses oleh task RFID terpisah.
- `uidToString()` mengubah UID kartu menjadi string 10 digit:
  - Jika panjang UID ≥ 4 byte, menggunakan 4 byte pertama sebagai angka 32-bit ditulis dalam format desimal 10 digit.
  - Jika panjang UID < 4 byte, menggunakan 2 byte pertama dalam format heksadesimal.
- Debounce diterapkan: kartu yang sama dalam rentang `DEBOUNCE_TIME` (150 ms) diabaikan.
- Kartu admin (terdaftar di `admin_rfid.txt`, maksimum 5 entri) memicu `handleAdminScan()`: menampilkan status antrian dan jumlah scan hari ini, serta memicu sinkronisasi manual jika WiFi terhubung.
- Kartu non-admin memicu proses `kirimPresensi()`.
- Umpan balik ditampilkan di OLED selama `RFID_FEEDBACK_DISPLAY_MS` (1800 ms).

### 3.5 Proses Presensi (`kirimPresensi`)
- Jika waktu tidak valid, proses dibatalkan dengan pesan "WAKTU INVALID".
- Apabila SD Card tersedia:
  - RFID harus terdapat pada cache database RFID lokal (`isRfidInCache`), jika tidak ditemukan pesan "RFID NONAKTIF".
  - Diperiksa apakah scan berulang dalam `MIN_REPEAT_INTERVAL` melalui data NVS (`nvsIsRecentScan`), jika ya pesan "CUKUP SEKALI!".
  - Data disimpan ke antrian file SD melalui `saveToQueue()`.
  - Hasil penyimpanan menentukan pesan: "DATA TERSIMPAN" atau "QUEUE HAMPIR PENUH!" (jika jumlah file antrian ≥ `QUEUE_WARN_THRESHOLD`), "CUKUP SEKALI!" (duplikat), "QUEUE PENUH!", atau "SD CARD ERROR".
- Apabila SD Card tidak tersedia namun WiFi terhubung:
  - Percobaan pengiriman langsung (`kirimLangsung`) ke endpoint `/api/presensi`.
  - Jika gagal dan bukan penolakan server (duplikat/RFID nonaktif/hari libur), data disimpan ke buffer NVS.
- Apabila offline total (tidak ada SD, tidak ada WiFi): data disimpan langsung ke buffer NVS.
- Setiap presensi berhasil memperbarui penghitung scan harian (`nvsBumpScanCount`) dan data scan terakhir (`nvsSaveLastScan`).

### 3.6 Penyimpanan Data Offline (SD Card Queue)
- Data presensi disimpan pada file `/queue_N.csv` (N = 0 hingga `MAX_QUEUE_FILES`-1), format kolom: `rfid,timestamp,device_id,unix_time,crc8`.
- Setiap file memiliki header kolom dan maksimum 25 record.
- Duplikasi diperiksa dengan memindai maksimum 3 file antrian terakhir (`MAX_DUPLICATE_CHECK_FILES`) dan maksimum 26 baris per file (`MAX_DUPLICATE_CHECK_LINES`), membandingkan RFID dan selisih waktu terhadap `MIN_REPEAT_INTERVAL`.
- CRC8 (polinomial 0x07) dihitung dari RFID + timestamp Unix untuk validasi integritas saat pembacaan kembali (`readQueueFileLocked`).
- Ketika file aktif penuh, sistem berpindah ke file berikutnya (index+1 modulo `MAX_QUEUE_FILES`); jika file berikutnya sudah memiliki record valid yang belum kedaluwarsa, status `SAVE_QUEUE_FULL` dikembalikan.
- Record dengan umur melebihi `MAX_OFFLINE_AGE` (1 tahun) dianggap kedaluwarsa dan tidak dihitung sebagai valid.
- Metadata jumlah record tertunda dan indeks file aktif disimpan pada `/queue_meta.txt` dan dimuat kembali saat boot.
- Akses ke SD Card dilindungi oleh mutex (`xSdMutex`) dengan timeout 5000 ms.

### 3.7 Penyimpanan Data Offline (NVS Buffer)
- Digunakan sebagai cadangan bila SD Card tidak tersedia, kapasitas maksimum 40 record (`NVS_MAX_RECORDS`).
- Data disimpan sebagai struktur biner `OfflineRecord` per key (`rec_0`, `rec_1`, dst.) pada namespace `presensi`.
- Duplikasi diperiksa melalui `nvsIsDuplicate()` dengan membandingkan seluruh isi buffer terhadap `MIN_REPEAT_INTERVAL`.
- Setelah sinkronisasi berhasil (`nvsSyncToServer`), seluruh record dihapus dan counter direset ke 0.

### 3.8 Sinkronisasi Data ke Server
- Sinkronisasi buffer NVS (`nvsSyncToServer`) dan file SD (`syncQueueFile`, `chunkedSync`) mengirim data ke endpoint `POST /api/presensi/sync-bulk` dengan header `X-API-KEY`.
- Payload berupa array JSON objek `{rfid, timestamp, device_id, sync_mode: true}`.
- Respons server berkode HTTP 200 diproses per item; status `"error"` per item dicatat ke `failed_log.csv` melalui `appendFailedLogToSD`.
- Sinkronisasi file SD dilakukan secara bertahap (chunked): maksimum `MAX_SYNC_FILES_PER_CYCLE` (5) file per siklus, dengan retry maksimum `MAX_SYNC_RETRIES` (2) kali dan delay eksponensial (`SYNC_RETRY_DELAY_MS * 2^attempt`).
- Selama proses sinkronisasi, watchdog timer diperpanjang ke 180 detik (`extendWdtForSync`) dan dikembalikan ke 90 detik setelah selesai (`restoreWdtNormal`).
- File yang kosong atau seluruh isinya kedaluwarsa akan dihapus otomatis tanpa dikirim ke server.

### 3.9 Pembaruan Database RFID
- Versi database lokal disimpan di NVS (`nvsGetRfidDbVer`/`nvsSetRfidDbVer`).
- Pemeriksaan versi server dilakukan melalui `GET /api/presensi/rfid-list/version`, mengharapkan respons JSON `{ "ver": <angka> }`.
- Apabila versi server lebih besar dari versi lokal, dilakukan unduhan penuh melalui `GET /api/presensi/rfid-list`.
- Format respons unduhan: baris pertama opsional `ver:<angka>`, diikuti daftar RFID (masing-masing 10 digit numerik per baris).
- Data disimpan sementara ke `/rfid_db.tmp`, lalu menggantikan `/rfid_db.txt` setelah selesai diunduh.
- Cache RFID di RAM dimuat ulang dari file setelah unduhan (`loadRfidCacheFromFileLocked`), kapasitas maksimum 5000 entri (`RFID_CACHE_MAX`).
- Pemeriksaan versi dijalankan setiap `RFID_DB_CHECK_INTERVAL` (30 detik) apabila SD tersedia dan sinyal tidak lemah.

### 3.10 Pembaruan Firmware (OTA)
- Pemeriksaan pembaruan dilakukan melalui `POST /api/presensi/firmware/check` dengan payload `{version, device_id}`, interval sesuai `rtCfg.otaCheckIntervalMs` (default `OTA_CHECK_INTERVAL` = 30 detik, dapat diubah melalui konfigurasi jarak jauh).
- Respons diharapkan berisi `{update: bool, version, url, md5}`.
- Versi baru dibandingkan terhadap `FIRMWARE_VERSION` menggunakan `strcmp(ver, FIRMWARE_VERSION) <= 0` untuk menentukan apakah versi server lebih baru.
- Apabila pembaruan tersedia, status disimpan (`otaState`), notifikasi tampil di OLED dan buzzer berbunyi.
- Proses unduhan (`performOtaUpdate`) menggunakan `Update.begin()` dengan ukuran dari `http.getSize()`, verifikasi MD5 (`Update.setMD5`) apabila tersedia.
- Setelah unduhan selesai dan `Update.end()` berhasil serta `Update.isFinished()` bernilai benar, perangkat melakukan restart otomatis.
- Kegagalan pada tahap manapun (HTTP gagal, ruang tidak cukup, `Update.end()` gagal) menampilkan pesan error di OLED dan `otaState.updateAvailable` diset false.
- Proses OTA memperpanjang watchdog timer sebelum mulai dan mengembalikannya setelah selesai/gagal.

### 3.11 Telemetri
- `sendTelemetry()` dijalankan setiap `TELEMETRY_INTERVAL` (5 menit) apabila sinyal tidak lemah.
- Data dikirim ke `POST /api/presensi/heartbeat` berisi: device_id, device_name, firmware, uptime_sec, heap_free, pending_records (SD + NVS), scan_today, rssi, sd_ok, rfid_db_entries, online.

### 3.12 Konfigurasi Jarak Jauh
- `fetchRemoteConfig()` dijalankan setiap `REMOTE_CONFIG_INTERVAL` (10 menit) apabila sinyal tidak lemah, memanggil `GET /api/presensi/config?device_id=<id>`.
- Field yang dapat diperbarui secara jarak jauh (jika terdapat pada respons JSON): `sleep_start`, `sleep_end`, `oled_dim_start`, `oled_dim_end`, `sync_interval_ms`, `ota_check_interval_ms`.
- Nilai-nilai ini disimpan hanya di RAM (`rtCfg`), tidak dipersist ke NVS.

### 3.13 Tampilan OLED
- Layar OLED 128x64 (SSD1306) menampilkan status koneksi (ONLINE/OFFLINE/CONNECTING/SYNCING), waktu (format HH:MM), jumlah record tertunda (Q:n), dan indikator kekuatan sinyal WiFi (4 bar berdasarkan rentang RSSI: >-67 dBm = 4 bar, >-70 = 3, >-80 = 2, >-90 = 1, selain itu 0).
- Update tampilan hanya dilakukan jika terdapat perubahan status (`displayStateChanged`), dengan interval pemeriksaan `DISPLAY_UPDATE_INTERVAL` (1000 ms).
- OLED otomatis dimatikan (`turnOffOLED`) pada rentang jam sesuai `dimStartHour`–`dimEndHour`, dan dinyalakan kembali (`turnOnOLED`) di luar rentang tersebut, diperiksa setiap `OLED_SCHEDULE_CHECK_INTERVAL` (60 detik).
- Akses ke display dilindungi mutex (`xDisplayMutex`).
- Animasi startup menampilkan judul "ZEDLABS", subjudul "INNOVATE BEYOND LIMITS", dan versi firmware.

### 3.14 Indikator Suara (Buzzer)
- `playToneSuccess()`: 2x nada 3000 Hz, 100 ms.
- `playToneError()`: 3x nada 3000 Hz, 150 ms.
- `playToneNotify()`: 1x nada 3000 Hz, 100 ms.
- `playStartupMelody()`: 4 nada bergantian 2500/3000 Hz saat boot.

### 3.15 Factory Reset
- Dipicu dengan menahan tombol Boot (GPIO9) selama minimum `FACTORY_RESET_HOLD_MS` (5 detik), dengan debounce 3x pemeriksaan awal @100 ms.
- Proses menghapus seluruh data pada NVS namespace `cfg` dan `presensi`, serta file `rfid_db.txt`, `queue_meta.txt`, dan `failed_log.csv` pada SD Card (jika tersedia).
- Perangkat melakukan restart otomatis setelah proses selesai.

### 3.16 Mode Deep Sleep
- Diperiksa pada `loop()` utama berdasarkan jam saat ini dibandingkan `sleepStartHour` dan `sleepEndHour`.
- Jika sinkronisasi sedang berlangsung (`syncState.inProgress`), deep sleep ditunda.
- Sebelum tidur: seluruh task diberi sinyal (`sleepRequested`) untuk idle selama `DEEP_SLEEP_TASK_WAIT_MS` (5 detik), file di-flush, OLED dimatikan.
- Durasi tidur dihitung berdasarkan selisih waktu saat ini terhadap jam akhir sleep, dibatasi minimum 60 detik dan maksimum 43200 detik (12 jam).
- Durasi tidur disimpan ke RTC memory (`sleepDurationSeconds`) untuk penyesuaian waktu setelah bangun.
- Watchdog task dihapus sebelum `esp_deep_sleep_start()` dipanggil dengan `esp_sleep_enable_timer_wakeup`.

### 3.17 Watchdog Timer (WDT)
- WDT normal diinisialisasi dengan timeout 90 detik (`WDT_NORMAL_TIMEOUT_MS`) saat boot, dengan `trigger_panic = true`.
- Selama proses sinkronisasi/OTA, timeout diperpanjang menjadi 180 detik (`WDT_SYNC_TIMEOUT_MS`) melalui `extendWdtForSync()`, dan dikembalikan melalui `restoreWdtNormal()`.
- Task-task utama (`hTaskLoop`, `hTaskRfid`, `hTaskSync`, `hTaskDisplay`) serta idle task core 0 didaftarkan ke WDT.

### 3.18 Pemantauan Kesehatan SD Card
- `checkSDHealth()` dijalankan setiap `SD_REDETECT_INTERVAL` (30 detik).
- Jika SD tidak tersedia, dilakukan percobaan reinisialisasi (`reinitSDCard`).
- Jika SD tersedia, pemeriksaan kesehatan dilakukan melalui `sd.vol()->fatType() > 0`; jika gagal, status SD diubah menjadi tidak tersedia dan cache RFID dikosongkan.

### 3.19 Pencatatan Log Kegagalan
- Kegagalan sinkronisasi dari respons server dicatat ke `/failed_log.csv` dengan kolom `rfid,timestamp,reason`.
- Log dibatasi maksimum `FAILED_LOG_MAX_LINES` (500 baris); jika sudah mencapai batas, penulisan baru dihentikan.

---

## 4. KEBUTUHAN ANTARMUKA EKSTERNAL (HTTP API)

| Endpoint | Method | Fungsi | Header Wajib |
|---|---|---|---|
| `/api/presensi` | POST | Pengiriman presensi langsung (mode online tanpa SD) | Content-Type, X-API-KEY |
| `/api/presensi/sync-bulk` | POST | Sinkronisasi data massal (dari SD/NVS) | Content-Type, X-API-KEY |
| `/api/presensi/rfid-list/version` | GET | Mendapatkan versi database RFID server | X-API-KEY |
| `/api/presensi/rfid-list` | GET | Mengunduh seluruh database RFID | X-API-KEY |
| `/api/presensi/config` | GET | Mendapatkan konfigurasi jarak jauh (query: device_id) | X-API-KEY |
| `/api/presensi/firmware/check` | POST | Pemeriksaan versi firmware terbaru | Content-Type, X-API-KEY |
| (URL dinamis dari respons firmware/check) | GET | Unduhan berkas firmware OTA | X-API-KEY |
| `/api/presensi/heartbeat` | POST | Pengiriman data telemetri | Content-Type, X-API-KEY |
| `/api/presensi/ping` | GET | Pemeriksaan konektivitas API | Content-Type, X-API-KEY |

Kode HTTP yang ditangani secara khusus pada `kirimLangsung()`: 200 (berhasil), 400 (duplikat), 403 (hari libur), 404 (RFID nonaktif), selain itu dianggap error server generik.

---

## 5. STRUKTUR DATA UTAMA

- `OfflineRecord`: rfid[11], timestamp[20], deviceId[20], unixTime (unsigned long).
- `EncryptedCredential`: iv[16], data[48], len (uint8_t).
- `WifiCredential`: ssid[32], pass[64].
- `RuntimeConfig`: sleepStartHour, sleepEndHour, dimStartHour, dimEndHour, syncIntervalMs, otaCheckIntervalMs.
- `Timers`: menyimpan timestamp `millis()` terakhir untuk setiap operasi periodik (scan, sync, time sync, reconnect, display update, dsb).
- `DisplayState`: isOnline, time[6], pendingRecords, wifiSignal.
- `SyncState`: currentFile, inProgress, startTime, filesProcessed, filesSucceeded.
- `RfidFeedback`: active, shownAt, wasOledOff.
- `OtaState`: updateAvailable, version[16], url[128], md5[36].
- `RfidScanEvent`: uid[10], uidLen.

---

## 6. KEBUTUHAN NON-FUNGSIONAL

- Sistem menggunakan arsitektur multi-tasking FreeRTOS dengan 3 task utama (RFID, Sync, Display) berjalan di core 0 dengan prioritas berbeda (RFID=3, Sync=2, Display=1), ditambah task `loop()` bawaan Arduino.
- Kredensial WiFi, API key, nama perangkat, dan API URL dienkripsi menggunakan AES-128 CBC sebelum disimpan ke NVS; kunci diturunkan dari alamat MAC perangkat melalui SHA-256.
- Komunikasi HTTPS menggunakan `WiFiClientSecure` dengan mode `setInsecure()` (tanpa verifikasi sertifikat).
- Sistem menyediakan mekanisme ketahanan data offline melalui dua lapis penyimpanan (SD Card sebagai prioritas utama, NVS sebagai cadangan).
- Sistem dirancang untuk berjalan otonom tanpa intervensi manual, dengan mekanisme reconnect, retry, dan watchdog untuk mencegah macet total (hang).

---

## 7. CATATAN KEKURANGAN/GAP/BUG PADA SOURCE CODE

1. Konflik pin: `PIN_BOOT` (untuk tombol factory reset) didefinisikan pada GPIO9, nilai yang sama persis dengan `PIN_OLED_SCL` (jalur clock I2C OLED). Kedua fungsi (pembacaan tombol digital dan komunikasi I2C) menggunakan pin fisik yang sama, berpotensi menimbulkan konflik fungsi dan pembacaan tombol yang tidak akurat.

2. Password Access Point provisioning (`PROV_AP_PASS`) berupa string tetap "P@ssw0rd" yang tertanam langsung di source code (hardcoded), sehingga bersifat sama pada seluruh unit perangkat dan mudah diketahui jika source terekspos.

3. Validasi sertifikat TLS dinonaktifkan sepenuhnya melalui `setInsecure()` pada seluruh komunikasi HTTPS (`getHttpClient()`), sehingga tidak ada verifikasi identitas server; berpotensi terhadap serangan man-in-the-middle.

4. Kunci enkripsi AES diturunkan dari kombinasi alamat MAC perangkat (dapat dibaca secara terbuka melalui WiFi) dan salt tetap ("ZEDLABS_PRESENSI") yang juga tertanam di source code. Karena algoritma dan salt diketahui, serta MAC address dapat diperoleh pihak luar, tingkat proteksi kerahasiaan kredensial yang tersimpan di NVS menjadi rendah.

5. Enkripsi AES-CBC yang digunakan tidak disertai mekanisme autentikasi/integritas (tidak ada HMAC atau mode AEAD seperti GCM), sehingga modifikasi data terenkripsi pada NVS tidak dapat terdeteksi oleh sistem.

6. Fungsi `uidToString()` untuk UID sepanjang ≥4 byte hanya menggunakan 4 byte pertama UID untuk membentuk identitas RFID (10 digit desimal), mengabaikan byte sisanya. Untuk kartu dengan UID lebih panjang (misalnya 7 byte), hal ini berpotensi menimbulkan tabrakan (collision) antara dua kartu fisik berbeda yang menghasilkan identitas RFID yang sama pada sistem.

7. Perbandingan versi firmware pada `checkOtaUpdate()` menggunakan `strcmp(ver, FIRMWARE_VERSION) <= 0`, yaitu perbandingan string secara leksikal, bukan perbandingan versi semantik. Hal ini dapat menghasilkan keputusan yang salah, misalnya string "2.10.0" akan dianggap lebih kecil dari "2.9.0" secara leksikal karena karakter '1' < '9'.

8. Pada `performOtaUpdate()`, ukuran firmware diambil dari `http.getSize()` yang dapat bernilai -1 apabila server tidak mengirimkan header Content-Length (misalnya menggunakan chunked transfer encoding). Nilai -1 yang dikonversi ke `size_t` (unsigned) akan menjadi nilai sangat besar, berpotensi menyebabkan `Update.begin()` gagal atau berperilaku tidak terduga.

9. Pada `saveToQueue()`, ketika file antrian aktif penuh, sistem hanya memeriksa satu file berikutnya (`currentQueueFile + 1`). Jika file tersebut masih berisi record valid yang belum kedaluwarsa, sistem langsung mengembalikan status `SAVE_QUEUE_FULL` tanpa mencoba mencari slot kosong lain di antara total 60000 kemungkinan file, sehingga antrian dapat dilaporkan penuh secara prematur meski banyak slot lain sebenarnya masih tersedia.

10. Pemeriksaan duplikasi presensi tersebar pada beberapa mekanisme berbeda yang tidak saling terhubung sepenuhnya: NVS "recent scan" (`nvsIsRecentScan`), duplikasi antar-file SD (`isDuplicateLocked`, hanya memeriksa 3 file terakhir), dan duplikasi buffer NVS (`nvsIsDuplicate`, tidak dipanggil pada jalur `kirimPresensi` untuk kasus dengan SD Card tersedia). Terdapat potensi celah dimana pengecekan duplikasi tidak konsisten antar-jalur penyimpanan (SD vs NVS vs pengiriman langsung).

11. Cache RFID di RAM (`rfidCacheFlat`) memiliki kapasitas tetap 5000 entri (`RFID_CACHE_MAX`). Apabila database RFID dari server melebihi jumlah tersebut, entri di luar 5000 pertama akan diabaikan tanpa peringatan atau pencatatan kepada server/administrator.

12. Variabel bersama seperti `rtCfg`, `apiKey`, `wifiCreds`, dan `deviceId` diakses dan dimodifikasi oleh beberapa task FreeRTOS berbeda (task Sync melalui `fetchRemoteConfig()`, task RFID, dan `loop()` utama) tanpa perlindungan mutex/semaphore, berbeda dengan akses ke SD Card dan Display yang sudah dilindungi mutex. Berpotensi menimbulkan kondisi balapan (race condition) pada pembacaan/penulisan data multi-byte.

13. Field `Timers::lastFactoryCheck` dideklarasikan pada struktur `Timers` namun tidak pernah digunakan/dibaca di manapun dalam kode (variabel mati).

14. Halaman web provisioning (`/save`) dikirim melalui HTTP biasa (bukan HTTPS) pada Access Point lokal, sehingga kredensial WiFi dan API Key yang dikirimkan dari browser ke perangkat berada dalam bentuk plaintext saat transmisi, meskipun terbatas pada jaringan AP lokal.

15. Proses unduhan database RFID (`downloadRfidDb()`) mem-parsing aliran data HTTP karakter-per-karakter dengan buffer baris tetap `lineBuf[32]`. Baris yang melebihi 31 karakter tidak menimbulkan overflow (karena ada pembatas indeks), namun berpotensi menghasilkan pemotongan data yang salah tanpa penanganan/pencatatan kesalahan format.

16. Tidak terdapat mekanisme autentikasi per-perangkat yang dinamis; seluruh permintaan API menggunakan satu API Key statis yang tersimpan di perangkat, dikombinasikan dengan TLS yang tidak diverifikasi (poin 3), sehingga tingkat keamanan komunikasi bergantung penuh pada kerahasiaan API Key tersebut.

17. Pada `checkFactoryReset()`, tiga pemeriksaan debounce awal (`vTaskDelay` 100 ms x3) tidak diikuti pemanggilan eksplisit `esp_task_wdt_reset()` sebelum memasuki loop utama penghitungan waktu tahan tombol, meskipun secara durasi total (300 ms) masih jauh di bawah batas watchdog normal (90 detik).

18. Konfigurasi yang diperoleh dari `fetchRemoteConfig()` (jadwal sleep, jadwal dim OLED, interval sinkronisasi, interval OTA) hanya disimpan di RAM (`rtCfg`) dan tidak dipersist ke NVS, sehingga seluruh pengaturan hasil konfigurasi jarak jauh akan hilang dan kembali ke nilai default/NVS setiap kali perangkat melakukan restart atau bangun dari deep sleep.
