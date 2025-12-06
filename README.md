# Sistem Presensi Pintar (RFID)

> Solusi presensi modern berbasis _Internet of Things_ (IoT) yang efisien, aman, dan hemat energi.

Sistem ini adalah perangkat presensi mandiri yang dirancang untuk mencatat kehadiran menggunakan kartu RFID. Dibangun di atas platform ESP32-C3, perangkat ini terintegrasi langsung dengan server backend melalui protokol internet yang aman, menghilangkan kebutuhan akan pencatatan manual atau pemindahan data fisik.

![Diagram Sistem](firmware/v1.0.0/diagram.svg)

## Mengapa Proyek Ini Dibuat?

Proyek ini bertujuan untuk menyediakan solusi perangkat keras terbuka (_open-source hardware_) untuk kebutuhan manajemen kehadiran yang:

- **Otomatis:** Sinkronisasi waktu dan pengiriman data terjadi secara _real-time_.
- **Hemat Energi:** Mampu menonaktifkan diri secara otomatis di luar jam kerja.
- **Aman:** Data yang dikirimkan terenkripsi melalui HTTPS.
- **Mudah Digunakan:** Cukup tempelkan kartu, dan sistem memberikan umpan balik visual serta suara.

## Fitur Utama

- 🆔 **Identifikasi Cepat:** Pembacaan kartu RFID instan dengan validasi anti-duplikasi (_debounce_).
- ☁️ **Konektivitas Cloud:** Terhubung ke API server menggunakan WiFi (mendukung Multi-SSID untuk cadangan koneksi).
- 🕒 **Waktu Presisi:** Jam internal yang selalu akurat berkat sinkronisasi NTP otomatis.
- 🔋 **Manajemen Daya Cerdas:** Fitur _Deep Sleep_ otomatis sesuai jadwal operasional kantor/sekolah.
- 🖥️ **Antarmuka Informatif:** Layar OLED menampilkan status koneksi, sinyal, jam, dan notifikasi presensi.

## Struktur Repositori

Repositori ini diatur dengan struktur sebagai berikut untuk memudahkan pengembangan dan penggunaan:

```text
.
├── doc/                   # Dokumentasi umum dan panduan pengguna
├── firmware/              # Kode sumber perangkat keras
│   └── v1.0.0/            # Versi rilis stabil saat ini
│       ├── main.ino       # Kode program (Arduino IDE)
│       ├── diagram.svg    # Skema jalur rangkaian
│       └── README.md      # Detail teknis & panduan instalasi firmware
└── LICENSE                # Lisensi penggunaan proyek
```

## Perangkat Keras yang Didukung

Sistem ini dikembangkan dan diuji secara spesifik menggunakan komponen berikut:

1.  **Mikrokontroler:** ESP32-C3 Super Mini
2.  **Sensor:** RFID MFRC522
3.  **Display:** OLED 0.96" I2C (SSD1306)
4.  **Indikator:** Active Buzzer 5V

## Memulai (Getting Started)

Untuk mulai membuat atau mengembangkan perangkat ini, silakan ikuti langkah berikut:

1.  **Siapkan Perangkat Keras:** Rakit komponen sesuai diagram yang tersedia.
2.  **Instalasi Firmware:** Masuk ke direktori firmware versi terbaru untuk panduan teknis lengkap, daftar pustaka (_library_), dan kode program.
    - 👉 **[Lihat Panduan Firmware v1.0.0](firmware/v1.0.0/README.md)**
3.  **Konfigurasi Server:** Pastikan Anda memiliki endpoint API yang siap menerima data JSON dari perangkat.

## Lisensi

Proyek ini bersifat _Open Source_. Anda bebas memodifikasi dan mendistribusikan ulang sesuai dengan ketentuan yang tercantum dalam file [LICENSE](./LICENSE).

---

**Pengelola Proyek:** Yahya Zulfikri (ZedLabs)
