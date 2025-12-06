# Sistem Presensi Pintar (RFID)

> Solusi presensi modern berbasis _Internet of Things_ (IoT) yang efisien, aman, dan hemat energi.

Sistem ini merupakan perangkat presensi mandiri yang dirancang untuk mencatat kehadiran menggunakan kartu RFID. Dibangun di atas platform ESP32-C3, perangkat ini terintegrasi langsung dengan server backend melalui protokol internet yang aman, menghilangkan kebutuhan akan pencatatan manual atau pemindahan data fisik.

![Diagram Sistem](firmware/v1.0.0/diagram.svg)

## Latar Belakang Proyek

Proyek ini bertujuan untuk menyediakan solusi perangkat keras terbuka (_open-source hardware_) untuk kebutuhan manajemen kehadiran yang memenuhi standar berikut:

- **Otomatis:** Sinkronisasi waktu dan pengiriman data terjadi secara _real-time_.
- **Hemat Energi:** Mampu menonaktifkan diri secara otomatis di luar jam operasional.
- **Aman:** Data yang dikirimkan terenkripsi melalui protokol HTTPS.
- **Interaktif:** Memberikan umpan balik visual dan suara saat pengguna menempelkan kartu.

## Fitur Utama

- **Identifikasi Cepat:** Pembacaan kartu RFID instan dengan validasi anti-duplikasi (_debounce_).
- **Konektivitas Cloud:** Terhubung ke API server menggunakan WiFi dengan dukungan Multi-SSID untuk redundansi koneksi.
- **Waktu Presisi:** Jam internal yang selalu akurat berkat sinkronisasi NTP otomatis.
- **Manajemen Daya Cerdas:** Fitur _Deep Sleep_ otomatis sesuai jadwal operasional kantor atau sekolah.
- **Antarmuka Informatif:** Layar OLED menampilkan status koneksi, kekuatan sinyal, waktu saat ini, dan notifikasi presensi.

## Struktur Repositori

Repositori ini diatur dengan struktur direktori sebagai berikut:

```text
.
├── docs/                  # Dokumentasi cetak biru (blueprint) dan visi proyek
├── firmware/              # Kode sumber perangkat keras
│   └── v1.0.0/            # Versi rilis stabil saat ini
│       ├── main.ino       # Kode program utama (Arduino IDE)
│       ├── diagram.svg    # Diagram skematik rangkaian
│       └── README.md      # Catatan rilis & panduan teknis firmware
└── LICENSE                # Lisensi penggunaan proyek
```

## Perangkat Keras yang Didukung

Sistem ini dikembangkan dan diuji secara spesifik menggunakan komponen berikut:

1.  **Mikrokontroler:** ESP32-C3 Super Mini
2.  **Sensor:** RFID MFRC522 (Interface SPI)
3.  **Display:** OLED 0.96 inci (Interface I2C, Driver SSD1306)
4.  **Indikator:** Active Buzzer 5V

## Panduan Memulai

Untuk mulai membuat, mengembangkan, atau mempelajari perangkat ini, silakan ikuti langkah-langkah berikut:

1.  **Persiapan Perangkat Keras:**
    Rakit komponen sesuai dengan diagram jalur yang tersedia pada folder firmware.

2.  **Dokumentasi dan Konsep:**
    Pelajari visi jangka panjang, arsitektur sistem, dan rencana pengembangan masa depan.

    - **[Baca Cetak Biru Proyek](docs/README.md)**

3.  **Instalasi Firmware:**
    Masuk ke direktori versi firmware untuk mendapatkan panduan teknis lengkap, daftar pustaka (_library_) yang dibutuhkan, dan kode program.

    - **[Lihat Panduan Firmware v1.0.0](firmware/v1.0.0/README.md)**

4.  **Konfigurasi Server:**
    Pastikan Anda telah menyiapkan endpoint API Backend yang siap menerima data JSON dari perangkat.

## Lisensi

Proyek ini bersifat _Open Source_. Anda bebas memodifikasi dan mendistribusikan ulang sesuai dengan ketentuan yang tercantum dalam file [LICENSE](./LICENSE).
