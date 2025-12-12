# Sistem Presensi Pintar Berbasis IoT (Hybrid Edition)

**Attendance Machine** adalah solusi presensi cerdas berbasis _Internet of Things_ (IoT) yang dirancang untuk mengatasi tantangan infrastruktur jaringan yang tidak stabil. Dibangun di atas mikrokontroler ESP32-C3, sistem ini menerapkan arsitektur _Hybrid_ yang menggabungkan kemampuan pemrosesan daring (_online_) dan luring (_offline_) secara mulus.

Sistem ini beroperasi dengan filosofi _Self-Healing_ dan _Store-and-Forward_, menjamin integritas data kehadiran tanpa kehilangan (_zero data loss_) melalui mekanisme antrean terpartisi (_Partitioned Queue System_) dan sinkronisasi otomatis.

---

## Visi dan Misi Proyek

Proyek ini bertujuan mendigitalisasi, mengautomatisasi, dan mengintegrasikan data kehadiran lembaga pendidikan dan instansi pemerintahan dengan sistem yang tangguh (_resilient_). Fokus utamanya adalah menghapus hambatan teknis akibat gangguan jaringan, menciptakan transparansi data, dan mempermudah pelaporan administratif melalui ekosistem digital terintegrasi.

## Arsitektur Sistem

Sistem dirancang sebagai gerbang fisik data kehadiran yang agnostik terhadap status konektivitas.

### Mekanisme Operasional Utama

1.  **Identifikasi:** Pengguna memindai kartu RFID pada perangkat.
2.  **Validasi Lokal:** Perangkat melakukan verifikasi _debounce_ dan pengecekan duplikasi data dalam interval waktu tertentu (default: 30-60 menit) langsung pada penyimpanan lokal untuk mencegah input ganda.
3.  **Manajemen Penyimpanan (Queue System):**
    - Data tidak dikirim langsung satu per satu, melainkan masuk ke dalam sistem antrean berkas CSV (`queue_X.csv`).
    - **Rotasi Berkas:** Data dipecah menjadi berkas-berkas kecil (maksimal 50 rekaman per berkas) untuk menjaga stabilitas memori RAM mikrokontroler.
4.  **Sinkronisasi Latar Belakang:**
    - Sistem secara berkala memeriksa keberadaan berkas antrean.
    - Jika koneksi internet tersedia, data dikirim secara _batch_ (satu berkas sekaligus) ke server.
    - Berkas lokal dihapus secara otomatis hanya jika server memberikan respons sukses (HTTP 200), menjamin konsistensi data.
5.  **Integrasi Hilir:** Server memproses data _batch_ untuk keperluan notifikasi WhatsApp, laporan digital, dan analisis kehadiran.

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

- **Offline-First Capability:** Prioritas penyimpanan data lokal saat jaringan tidak tersedia atau tidak stabil.
- **Partitioned Queue System:** Manajemen memori tingkat lanjut yang memecah penyimpanan data menjadi ratusan berkas kecil untuk mencegah _buffer overflow_ pada mikrokontroler.
- **Smart Duplicate Prevention:** Algoritma _sliding window_ yang memindai indeks antrean lokal untuk menolak pemindaian kartu yang sama dalam periode waktu yang dikonfigurasi.
- **Bulk Upload Efficiency:** Mengoptimalkan penggunaan _bandwidth_ dengan mengirimkan himpunan data (50 rekaman) dalam satu permintaan HTTP POST.
- **Hybrid Timekeeping:** Sinkronisasi waktu presisi menggunakan NTP saat daring, dan estimasi waktu berbasis RTC internal saat luring.
- **Deep Sleep Scheduling:** Manajemen daya otomatis untuk menonaktifkan sistem di luar jam operasional.

## Struktur Repositori

Repositori ini mencakup kode sumber perangkat keras (firmware) dan dokumentasi pendukung.

```text
.
├── firmware/              # Kode sumber perangkat keras
│   ├── v1.0.0/            # Versi Awal (Online Only)
│   ├── v2.0.0/            # Versi Hibrida Awal (Single CSV)
│   ├── v2.1.0/            # Versi Stabil (Queue System Base)
│   └── v2.2.0/            # Versi Ultimate (SdFat, Legacy Card Support, Optimized)
└── LICENSE                # Lisensi penggunaan proyek
```

## Integrasi Backend

Untuk mendukung fungsionalitas sinkronisasi massal, server backend wajib menyediakan _endpoint_ API dengan spesifikasi sebagai berikut:

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

## Peta Jalan Pengembangan (Roadmap)

Proyek ini dikembangkan secara bertahap menuju ekosistem manajemen kehadiran yang komprehensif:

1.  **Versi 1.x (Selesai):** Presensi Daring Dasar.
2.  **Versi 2.x (Stabil Saat Ini):** Sistem Hibrida, Antrean Offline Terpartisi, Sinkronisasi Massal, Auto Reconnect Wifi dan Dukungan Penyimpanan Warisan (_Legacy Storage_).
3.  **Versi 3.x:** Integrasi Biometrik (Sidik Jari) sebagai metode autentikasi sekunder.
4.  **Versi 4.x:** Ekspansi fungsi menjadi Buku Tamu Digital dan Integrasi PPDB.
5.  **Versi 5.x:** Ekosistem Perpustakaan Pintar (Sirkulasi Mandiri).
6.  **Versi 6.x:** IoT Gateway & LoRaWAN untuk pemantauan area tanpa jangkauan seluler/WiFi.

## Lisensi

Hak Cipta © 2025. Proyek ini didistribusikan di bawah lisensi yang tercantum dalam berkas `LICENSE`. Penggunaan, modifikasi, dan distribusi diperbolehkan dengan menyertakan atribusi yang sesuai.
