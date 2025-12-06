Berikut adalah pembaruan untuk file **README.md** utama (root) repositori Anda. Dokumen ini telah disesuaikan untuk mencerminkan status proyek yang kini telah mencapai **Versi 2.1.0 (Hybrid/Offline Capable)**.

---

# Sistem Presensi Pintar (RFID) - Hybrid Edition

> Solusi presensi cerdas berbasis IoT yang tangguh, mendukung penyimpanan offline, dan sinkronisasi data otomatis (_Self-Healing_).

Sistem ini adalah evolusi dari perangkat presensi konvensional. Dibangun di atas platform **ESP32-C3**, perangkat ini tidak hanya bergantung pada koneksi internet terus-menerus. Dengan arsitektur _Hybrid_, sistem mampu beroperasi penuh dalam kondisi tanpa sinyal (Offline) dan menyinkronkan data secara cerdas saat koneksi pulih, menjamin integritas data 100% tanpa kehilangan.

![Diagram Sistem](firmware/v2.1.0/diagram.svg)

## Latar Belakang Proyek

Proyek ini bertujuan menyediakan solusi perangkat keras terbuka (_open-source hardware_) untuk manajemen kehadiran yang mengatasi tantangan infrastruktur jaringan tidak stabil, dengan standar:

- **Resilien:** Tetap berfungsi mencatat kehadiran meskipun jaringan mati total.
- **Otomatis:** Mekanisme _Store-and-Forward_ (Simpan dan Kirim) bekerja di latar belakang tanpa intervensi manusia.
- **Efisien:** Menggunakan sistem antrean file terpartisi untuk menjaga kinerja memori perangkat tetap ringan.
- **Aman:** Data terenkripsi via HTTPS dan tervalidasi untuk mencegah duplikasi data.

## Fitur Utama (Versi Stabil v2.1.0)

- **Offline-First Capability:** Penyimpanan data lokal menggunakan SD Card saat internet terputus.
- **Partitioned Queue System:** Manajemen memori cerdas dengan memecah data ke dalam file-file antrean kecil (`queue_x.csv`) untuk mencegah _crash_ sistem.
- **Bulk Synchronization:** Pengiriman data massal (50 data/detik) untuk efisiensi bandwidth dan waktu.
- **Identifikasi Cepat:** Validasi anti-duplikasi lokal (_Local Debounce_) untuk mencegah scan ganda dalam periode tertentu.
- **Waktu Presisi Hybrid:** Menggunakan sinkronisasi NTP saat online dan estimasi RTC internal saat offline.
- **Manajemen Daya Cerdas:** Fitur _Deep Sleep_ otomatis sesuai jadwal operasional.

## Rencana Pengembangan (Roadmap)

Kami memiliki visi jangka panjang untuk mengembangkan ekosistem pendidikan dan perkantoran pintar:

- **v1.x:** Presensi Online Dasar (Selesai).
- **v2.x:** **Sistem Hybrid, Antrean Offline, & Sinkronisasi Massal (Rilis Saat Ini).**
- **v3.x:** Integrasi biometrik (Sidik Jari) sebagai cadangan kartu.
- **v4.x:** Sistem buku tamu digital dan integrasi data PPDB.
- **v5.x:** Ekosistem perpustakaan pintar (sirkulasi mandiri).
- **v6.x:** Pemantauan lokasi siswa/pegawai berbasis BLE Beacon dan LoRa.

Detail teknis lengkap mengenai visi masa depan ini dapat dibaca pada dokumen **[Cetak Biru Proyek (Blueprint)](docs/README.md)**.

## Struktur Repositori

Repositori ini diatur dengan struktur direktori sebagai berikut:

```text
.
├── docs/                  # Cetak biru (Blueprint) dan Roadmap pengembangan
├── firmware/              # Kode sumber perangkat keras
│   ├── v1.0.0/            # Versi Legacy (Online Only)
│   ├── v2.0.0/            # Versi Legacy (Offline Capable/Hybrid Support)
│   └── v2.1.0/            # Versi Stabil (Offline/Hybrid Support/Queue System)
│       ├── main.ino       # Kode program utama (Queue System Support)
│       ├── diagram.svg    # Diagram skematik rangkaian dengan SD Card
│       └── README.md      # Catatan rilis & panduan teknis v2.1.0
└── LICENSE                # Lisensi penggunaan proyek
```

## Perangkat Keras yang Didukung

Sistem v2.1.0 memerlukan komponen tambahan (SD Card) dibandingkan versi sebelumnya:

1.  **Mikrokontroler:** ESP32-C3 Super Mini
2.  **Sensor:** RFID MFRC522 (Interface SPI)
3.  **Penyimpanan:** Modul MicroSD Card (Interface SPI)
4.  **Display:** OLED 0.96 inci (Interface I2C, Driver SSD1306)
5.  **Indikator:** Active Buzzer 5V

## Panduan Memulai

Untuk mulai merakit dan menggunakan perangkat ini:

1.  **Pahami Konsep:**
    Baca dokumen **[Cetak Biru Proyek](docs/README.md)** untuk memahami arsitektur data dan alur kerja sistem Hybrid.

2.  **Persiapan Perangkat Keras:**
    Versi ini menggunakan jalur SPI bersama (_Shared SPI_) untuk RFID dan SD Card. Pastikan Anda melihat skematik pinout yang diperbarui.

3.  **Instalasi Firmware:**
    Masuk ke direktori versi terbaru untuk mendapatkan kode program dan dependensi pustaka yang diperlukan.

    - **[Lihat Panduan & Firmware v2.1.0](firmware/v2.1.0/README.md)**

4.  **Konfigurasi Server:**
    Pastikan backend Anda mendukung endpoint `POST /api/presensi/sync-bulk` untuk menerima data array JSON.

## Lisensi

Proyek ini bersifat _Open Source_. Anda bebas memodifikasi dan mendistribusikan ulang sesuai dengan ketentuan yang tercantum dalam file [LICENSE](./LICENSE).
