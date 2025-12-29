# Sistem Presensi Pintar Berbasis IoT (Hybrid Edition)

**Attendance Machine** adalah solusi presensi cerdas berbasis _Internet of Things_ (IoT) yang dirancang untuk mengatasi tantangan infrastruktur jaringan yang tidak stabil. Dibangun di atas mikrokontroler ESP32-C3, sistem ini menerapkan arsitektur _Hybrid_ yang menggabungkan kemampuan pemrosesan daring (_online_) dan luring (_offline_) secara mulus.

Sistem ini beroperasi dengan filosofi _Self-Healing_ dan _Store-and-Forward_, menjamin integritas data kehadiran tanpa kehilangan (_zero data loss_) melalui mekanisme antrean terpartisi (_Partitioned Queue System_), sinkronisasi otomatis, dan **operasi latar belakang yang tidak mengganggu pengguna** (_Non-Intrusive Background Operations_).

---

## Visi dan Misi Proyek

Proyek ini bertujuan mendigitalisasi, mengautomatisasi, dan mengintegrasikan data kehadiran lembaga pendidikan dan instansi pemerintahan dengan sistem yang tangguh (_resilient_) dan **user-friendly**. Fokus utamanya adalah menghapus hambatan teknis akibat gangguan jaringan, menciptakan transparansi data, mempermudah pelaporan administratif melalui ekosistem digital terintegrasi, serta memberikan **pengalaman pengguna yang mulus tanpa gangguan proses teknis**.

## Arsitektur Sistem

Sistem dirancang sebagai gerbang fisik data kehadiran yang agnostik terhadap status konektivitas dengan prioritas pada responsivitas dan user experience.

### Mekanisme Operasional Utama

1.  **Identifikasi:** Pengguna memindai kartu RFID pada perangkat.
2.  **Validasi Lokal:** Perangkat melakukan verifikasi _debounce_ dan pengecekan duplikasi data dalam interval waktu tertentu (default: 30 menit) langsung pada penyimpanan lokal untuk mencegah input ganda.
3.  **Manajemen Penyimpanan (Queue System):**
    - Data tidak dikirim langsung satu per satu, melainkan masuk ke dalam sistem antrean berkas CSV (`queue_X.csv`).
    - **Rotasi Berkas:** Data dipecah menjadi berkas-berkas kecil (maksimal 50 rekaman per berkas) untuk menjaga stabilitas memori RAM mikrokontroler.
4.  **Sinkronisasi Latar Belakang (_Background Sync_):**
    - Sistem secara berkala (setiap 5 menit) memeriksa keberadaan berkas antrean.
    - Jika koneksi internet tersedia, data dikirim secara _batch_ (satu berkas sekaligus) ke server **tanpa menampilkan proses atau mengganggu pengguna**.
    - Berkas lokal dihapus secara otomatis hanya jika server memberikan respons sukses (HTTP 200), menjamin konsistensi data.
    - **Non-Blocking:** Proses sinkronisasi berjalan di background tanpa memblokir operasi tapping RFID.
5.  **Reconnect Otomatis (_Silent Auto-Reconnect_):**
    - Jika WiFi terputus, sistem akan mencoba reconnect setiap 5 menit secara otomatis.
    - Proses reconnect berjalan **diam-diam di latar belakang** tanpa menampilkan loading atau progress bar.
    - Setelah WiFi kembali online, sistem otomatis melakukan sync data offline yang tertunda.
6.  **Integrasi Hilir:** Server memproses data _batch_ untuk keperluan notifikasi WhatsApp, laporan digital, dan analisis kehadiran.

## Spesifikasi Teknis

### Perangkat Keras

Sistem ini menggunakan arsitektur bus SPI bersama (_Shared SPI Bus_) untuk efisiensi pin pada ESP32-C3.

| Komponen             | Spesifikasi              | Fungsi Utama                                                               |
| :------------------- | :----------------------- | :------------------------------------------------------------------------- |
| **Unit Pemroses**    | ESP32-C3 Super Mini      | Manajemen logika utama, konektivitas WiFi, dan sistem berkas.              |
| **Sensor Identitas** | RFID RC522 (13.56 MHz)   | Pembacaan UID kartu presensi (Protokol SPI).                               |
| **Penyimpanan**      | Modul MicroSD (SPI)      | Penyimpanan antrean data offline (CSV) dan log sistem.                     |
| **Antarmuka Visual** | OLED 0.96 inci (SSD1306) | Visualisasi status koneksi, jam, dan penghitung antrean (_Queue Counter_). |
| **Indikator Audio**  | Buzzer Aktif 5V          | Umpan balik audio untuk status sukses, gagal, atau kesalahan sistem.       |
| **Catu Daya**        | 5V USB / 3.7V Li-ion     | Sumber daya operasional.                                                   |

### Fitur Perangkat Lunak (Firmware)

#### Core Features

- **Offline-First Capability:** Prioritas penyimpanan data lokal saat jaringan tidak tersedia atau tidak stabil.
- **Partitioned Queue System:** Manajemen memori tingkat lanjut yang memecah penyimpanan data menjadi ratusan berkas kecil untuk mencegah _buffer overflow_ pada mikrokontroler.
- **Smart Duplicate Prevention:** Algoritma _sliding window_ yang memindai indeks antrean lokal untuk menolak pemindaian kartu yang sama dalam periode waktu yang dikonfigurasi.
- **Bulk Upload Efficiency:** Mengoptimalkan penggunaan _bandwidth_ dengan mengirimkan himpunan data (50 rekaman) dalam satu permintaan HTTP POST.
- **Hybrid Timekeeping:** Sinkronisasi waktu presisi menggunakan NTP saat daring, dan estimasi waktu berbasis RTC internal saat luring.
- **Deep Sleep Scheduling:** Manajemen daya otomatis untuk menonaktifkan sistem di luar jam operasional.

#### Advanced Features (v2.2.2+)

- **Silent Background Sync:** Sinkronisasi data berjalan di latar belakang tanpa feedback visual atau audio yang mengganggu.
- **Non-Intrusive Reconnect:** Auto-reconnect WiFi tanpa menampilkan loading screen atau progress bar.
- **Zero-Interruption UX:** Pengguna dapat melakukan tapping RFID kapan saja tanpa terblokir oleh proses background.
- **Thread-Safe Operations:** Implementasi dual-flag system (`isSyncing`, `isReconnecting`) untuk mencegah race condition.
- **Smart Display Management:** Layar OLED hanya menampilkan informasi penting (status, waktu, queue counter) tanpa distraksi proses teknis.
- **Legacy Storage Support:** Dukungan penuh untuk SD Card lama (128MB-256MB) menggunakan pustaka SdFat.

## Struktur Repositori

Repositori ini mencakup kode sumber perangkat keras (firmware) dan dokumentasi pendukung.

```text
.
├── firmware/              # Kode sumber perangkat keras
│   ├── v1.0.0/            # Versi Awal (Online Only)
│   ├── v2.0.0/            # Versi Hibrida Awal (Single CSV)
│   ├── v2.1.0/            # Versi Stabil (Queue System Base)
│   ├── v2.2.0/            # Versi Ultimate (SdFat, Legacy Card Support, Optimized)
│   ├── v2.2.1/            # Auto-Reconnect & Bug Fixes
│   └── v2.2.2/            # Background Operations & Enhanced UX
└── LICENSE                # Lisensi penggunaan proyek
```

## Integrasi Backend

Untuk mendukung fungsionalitas sinkronisasi massal, server backend wajib menyediakan _endpoint_ API dengan spesifikasi sebagai berikut:

### Endpoint Sync Bulk

- **URL Endpoint:** `/api/presensi/sync-bulk`
- **Metode HTTP:** `POST`
- **Header Wajib:**
  - `Content-Type: application/json`
  - `X-API-KEY: [Token Keamanan]`
- **Format Payload (JSON):**
  ```json
  {
  	"data": [
  		{
  			"rfid": "1234567890",
  			"timestamp": "2025-12-07 07:00:00",
  			"device_id": "ESP32_A1B2",
  			"sync_mode": true
  		}
  	]
  }
  ```
- **Respons:** Server harus mengembalikan kode status **HTTP 200 OK** untuk mengonfirmasi bahwa data telah disimpan. Kode respons selain 200 akan menyebabkan perangkat menyimpan kembali data tersebut dan mencoba pengiriman ulang di siklus berikutnya.

### Endpoint Health Check

- **URL Endpoint:** `/api/presensi/ping`
- **Metode HTTP:** `GET`
- **Header Wajib:**
  - `X-API-KEY: [Token Keamanan]`
- **Respons:** Server harus mengembalikan **HTTP 200 OK** jika service aktif dan dapat menerima data.

## Background Operations Architecture

### Filosofi Desain

Versi 2.2.2 menerapkan prinsip **"Invisible Infrastructure"** - semua operasi teknis (network, sync, reconnect) harus tidak terlihat oleh pengguna akhir. User experience harus fokus pada satu hal: **tap kartu → lihat feedback → selesai**.

### Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     USER INTERACTION                         │
│                                                               │
│  [Tap RFID Card] → [Save to Queue] → [Success Feedback]     │
│                                                               │
└─────────────────────────────────────────────────────────────┘
                            ▲
                            │ Non-blocking
                            │
┌─────────────────────────────────────────────────────────────┐
│                  BACKGROUND OPERATIONS                       │
│                                                               │
│  ┌─────────────────┐         ┌──────────────────┐          │
│  │ WiFi Reconnect  │◄────────┤  Every 5 minutes │          │
│  │   (Silent)      │         └──────────────────┘          │
│  └────────┬────────┘                                         │
│           │                                                   │
│           ▼                                                   │
│  ┌─────────────────┐         ┌──────────────────┐          │
│  │ Data Sync       │◄────────┤  Auto-triggered  │          │
│  │   (Silent)      │         │  after online    │          │
│  └─────────────────┘         └──────────────────┘          │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

### Key Implementation Details

1. **Flag-Based Synchronization**

   ```cpp
   // Global flags untuk thread safety
   bool isSyncing = false;
   bool isReconnecting = false;
   ```

2. **Silent WiFi Reconnect**

   ```cpp
   // connectToWiFiBackground() - tanpa display feedback
   // - No OLED updates
   // - No buzzer sounds
   // - Non-blocking timeout
   ```

3. **Background Sync Process**
   ```cpp
   // syncAllQueues() - silent operation
   // - Set isSyncing flag
   // - No visual/audio feedback
   // - Auto-cleanup after success
   ```

## User Experience Improvements

### Before v2.2.2

- Tampilan "CONNECTING WIFI..." mengganggu user
- Progress bar "SYNCING File 0 (15)" membingungkan
- Buzzer berbunyi saat background process
- User harus menunggu saat reconnect/sync
- Display penuh dengan informasi teknis

### After v2.2.2

- Display minimalis: hanya "TAP KARTU" dan info penting
- Status ONLINE/OFFLINE update otomatis tanpa gangguan
- Silent background operations (no sound, no loading)
- Tapping RFID tidak pernah terblokir
- Professional dan user-friendly
- Queue counter transparan (Q:xx) jika ada pending data

## Peta Jalan Pengembangan (Roadmap)

Proyek ini dikembangkan secara bertahap menuju ekosistem manajemen kehadiran yang komprehensif:

1.  **Versi 1.x (Selesai):** Presensi Daring Dasar.
2.  **Versi 2.x (Stabil Saat Ini):** Sistem Hibrida, Antrean Offline Terpartisi, Sinkronisasi Massal, Auto Reconnect Wifi, Dukungan Penyimpanan Warisan (_Legacy Storage_), dan **Background Operations**.
3.  **Versi 3.x (In Progress):** Integrasi Biometrik (Sidik Jari) sebagai metode autentikasi sekunder.
4.  **Versi 4.x (Planned):** Ekspansi fungsi menjadi Buku Tamu Digital dan Integrasi PPDB.
5.  **Versi 5.x (Planned):** Ekosistem Perpustakaan Pintar (Sirkulasi Mandiri).
6.  **Versi 6.x (Future):** IoT Gateway & LoRaWAN untuk pemantauan area tanpa jangkauan seluler/WiFi.

## Version History Highlights

### v2.2.2 (Desember 2025) - Background Operations

**Focus:** Enhanced UX through invisible infrastructure

- Background WiFi reconnection (every 5 minutes)
- Silent bulk sync operations
- Zero-interruption tapping experience
- Thread-safe dual-flag system
- Minimalist display design

### v2.2.1 (Desember 2025) - Stability & Auto-Reconnect

- Automatic WiFi reconnection
- System bug fixes and optimizations

### v2.2.0 (Desember 2025) - Ultimate Edition

- Migration to SdFat library
- Legacy SD Card support (128MB+)
- Memory optimization
- Enhanced visual feedback

### v2.1.0 - Queue System Foundation

- Multi-file queue implementation
- Sliding window duplicate check

### v2.0.0 - Hybrid Architecture

- Offline capability introduction
- Single CSV queue system

### v1.0.0 - Initial Release

- Online-only operations
- Basic RFID attendance

## Installation & Configuration

### Hardware Setup

1. Connect components according to pinout specification
2. Insert formatted SD Card (FAT16/FAT32)
3. Power via USB 5V or Li-ion battery

### Firmware Configuration

Edit configuration constants in firmware source:

```cpp
// Network Settings
const char WIFI_SSID[] = "Your_SSID";
const char WIFI_PASSWORD[] = "Your_Password";
const char API_BASE_URL[] = "https://your-server.com";
const char API_SECRET_KEY[] = "Your_Secret_Key";

// Queue Settings
const int MAX_RECORDS_PER_FILE = 50;
const unsigned long SYNC_INTERVAL = 300000; // 5 minutes
const unsigned long RECONNECT_INTERVAL = 300000; // 5 minutes
```

### Library Dependencies

Install via Arduino Library Manager:

- `SdFat` by Bill Greiman (v2.x) - **Required**
- `MFRC522` by GithubCommunity
- `Adafruit SSD1306` & `Adafruit GFX`
- `ArduinoJson`

## Troubleshooting

### Common Issues

**Issue:** Sync appears slow or blocked  
**Solution:** Ensure no `showOLED()` or `playTone()` calls exist inside `syncAllQueues()`. All feedback must be removed for optimal background operations.

**Issue:** Double sync/reconnect operations  
**Solution:** Check `isSyncing` and `isReconnecting` flag implementation. Flags must be set before operation and cleared after completion.

**Issue:** Display not updating online/offline status  
**Solution:** Verify `showStandbySignal()` reads `isOnline` variable that's constantly updated in main loop.

**Issue:** RFID tapping feels laggy  
**Solution:** Check for blocking operations in main loop. All network operations must be background-only.

## Contributing

Contributions are welcome! Please follow these guidelines:

1. Focus on user experience and responsiveness
2. Keep background operations silent and non-intrusive
3. Maintain backward compatibility with existing API
4. Document all configuration changes
5. Test thoroughly with various SD Card types

## Performance Metrics

- **Tap-to-Feedback Latency:** < 200ms
- **Background Sync Frequency:** Every 5 minutes (configurable)
- **Auto-Reconnect Interval:** Every 5 minutes (configurable)
- **Queue Capacity:** Up to 50,000 records (1000 files × 50 records)
- **Time Sync Accuracy:** ±1 second (NTP-based)
- **Power Consumption:** ~150mA active, <5mA deep sleep

## Best Practices for Developers

1. **Never Block Main Loop:** All network operations must be non-blocking
2. **Use Flags Consistently:** Always check and set operation flags
3. **Minimal Display Updates:** Only update OLED for critical information
4. **Silent by Default:** No audio/visual feedback for background processes
5. **Error Handling:** Graceful degradation, never crash on network errors
6. **Memory Management:** Use static buffers, avoid dynamic allocation

## Lisensi

Hak Cipta © 2025 Yahya Zulfikri. Proyek ini didistribusikan di bawah MIT License. Penggunaan, modifikasi, dan distribusi diperbolehkan dengan menyertakan atribusi yang sesuai.
