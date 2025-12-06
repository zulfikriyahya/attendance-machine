# Cetak Biru Proyek: Attendance Machine (IoT)

Dokumen ini menguraikan arsitektur, visi, dan spesifikasi teknis dari **Attendance Machine**, sebuah solusi presensi cerdas yang mengintegrasikan perangkat keras IoT (ESP32 + RFID) dengan ekosistem digital modern (Laravel + WhatsApp).

Fokus utama dokumentasi ini adalah pada sisi **Perangkat Keras & Firmware**, yang dirancang sebagai modul _plug-and-play_ yang terhubung ke sistem backend terpisah.

---

## Visi & Misi

> **"Mendigitalisasi, mengautomatisasi, dan mengintegrasikan data kehadiran seluruh lembaga di Provinsi Banten — khususnya Kabupaten Pandeglang — dengan sistem presensi cerdas yang real-time, fleksibel, efisien, dan terjangkau."**

Tujuan utamanya adalah menghapus hambatan teknis dalam pencatatan kehadiran, menciptakan transparansi data, dan mempermudah pelaporan administratif bagi sekolah maupun instansi pemerintahan.

---

## Gambaran Sistem

**Attendance Machine** bukan sekadar alat scan kartu. Ini adalah gerbang fisik untuk data kehadiran yang dirancang dengan filosofi **"Zero Configuration"** di sisi pengguna akhir.

### Alur Kerja Utama

1.  **Identifikasi:** Pengguna menempelkan kartu RFID (atau sidik jari pada pengembangan masa depan).
2.  **Pemrosesan:** Perangkat memvalidasi input dan memberikan umpan balik (Suara/Visual).
3.  **Sinkronisasi:**
    - **Online:** Data dikirim instan ke server via WiFi.
    - **Offline:** Data disimpan di memori internal dan dikirim otomatis saat koneksi pulih.
4.  **Notifikasi:** Server mengirim pesan WhatsApp ke orang tua/atasan secara _real-time_.

---

## Fitur Unggulan (Versi Saat Ini)

### A. Fitur Perangkat (Firmware & Hardware)

Fitur-fitur yang tertanam langsung pada mesin ESP32:

- **Konektivitas Cerdas (Multi-SSID):** Perangkat otomatis mencari dan terhubung ke jaringan WiFi yang terdaftar tanpa perlu konfigurasi ulang manual.
- **Hybrid Mode (Online/Offline):** Ketahanan terhadap gangguan jaringan. Presensi tetap berjalan tanpa internet, data akan disinkronkan kemudian (Store & Forward).
- **Instant Scan:** Waktu pemrosesan ±1 detik per pengguna dengan mekanisme _debounce_ untuk mencegah input ganda.
- **Antarmuka Interaktif:** Layar OLED menampilkan Nama, Waktu, dan Status, serta Buzzer untuk indikator suara.
- **Manajemen Daya:** Mode _Deep Sleep_ otomatis berdasarkan jadwal operasional untuk menghemat energi (mendukung penggunaan baterai).
- **Zero Maintenance:** Desain _Plug & Play_. Tidak perlu instalasi driver atau reset ulang saat berpindah lokasi.

### B. Ekosistem Backend (Laravel - Modul Terpisah)

Layanan yang dijalankan oleh server untuk mendukung perangkat:

- **Notifikasi WhatsApp:** Informasi kehadiran dikirim langsung ke wali siswa/pegawai.
- **Laporan Digital:** Generasi laporan PDF/Excel otomatis dengan filter instansi dan jabatan.
- **E-Signature:** Tanda tangan elektronik yang sah dan terverifikasi pada laporan kehadiran.
- **Manajemen Izin:** Pengajuan ketidakhadiran via aplikasi agar tidak dianggap alpa oleh mesin.
- **Monitoring:** Dashboard admin untuk memantau status baterai dan koneksi perangkat.

---

## Spesifikasi Perangkat Keras

Sistem ini dibangun dengan komponen yang terjangkau namun handal:

| Komponen      | Spesifikasi          | Fungsi Utama                                           |
| :------------ | :------------------- | :----------------------------------------------------- |
| **MCU**       | ESP32-C3 Super Mini  | Otak pemrosesan utama, koneksi WiFi, manajemen daya.   |
| **Sensor**    | RFID RC522 (SPI)     | Membaca UID kartu presensi (Mifare 13.56MHz).          |
| **Display**   | OLED 0.96" (I2C)     | Menampilkan status koneksi, jam, dan feedback user.    |
| **Indikator** | Active Buzzer        | Memberikan sinyal audio (Sukses/Gagal).                |
| **Power**     | 5V USB / 3.7V Li-ion | Sumber daya fleksibel (Listrik langsung atau Baterai). |

---

## Implementasi & Target Pengguna

Sistem ini didesain untuk skalabilitas tinggi, mampu melayani ratusan hingga ribuan pengguna tanpa membebani kinerja lokal.

**Target Wilayah:** Provinsi Banten (Fokus: Kab. Pandeglang)

**Sektor Utama:**

1.  **Pendidikan:** Sekolah Dasar, Menengah, dan Pondok Pesantren (Notifikasi ke Orang Tua).
2.  **Pemerintahan:** Kantor Kecamatan, Kelurahan, dan Dinas Daerah (Disiplin Pegawai).
3.  **Layanan Publik:** Instansi yang membutuhkan data kehadiran shift.

---

## Peta Jalan Pengembangan (Roadmap)

Pengembangan sistem dibagi menjadi beberapa fase versi untuk memastikan stabilitas, efisiensi data, dan perluasan fungsionalitas:

### Versi 2.x: Optimasi Sinkronisasi & Ketahanan Data

Fokus pada efisiensi pengiriman data (bandwidth) dan penanganan kondisi jaringan yang tidak stabil.

- **Auto Sync-Bulk:** Mekanisme antrean cerdas dimana data presensi disimpan sementara di memori lokal (buffer) secara default.
- **Interval Sinkronisasi:** Sistem akan memeriksa tumpukan data offline dan melakukan pengunggahan massal (bulk upload) setiap 30 detik secara otomatis jika koneksi tersedia.

### Versi 3.x: Integrasi Biometrik (Fingerprint)

Penambahan lapisan keamanan dan redundansi perangkat keras.

- **Modul Sidik Jari:** Integrasi sensor sidik jari optikal sebagai metode alternatif (backup) jika kartu RFID siswa hilang atau tertinggal.
- **Mode Ganda:** Perangkat mendukung input dari kartu RFID maupun sidik jari secara bersamaan.

### Versi 4.x: Manajemen Tamu & Integrasi PPDB

Transformasi perangkat menjadi kios pelayanan mandiri untuk wali siswa dan tamu sekolah.

- **Buku Tamu Digital:** Orang tua atau wali siswa dapat melakukan pencatatan kunjungan menggunakan pemindaian KTP (NIK/RFID) atau sidik jari.
- **Integrasi PPDB:** Data sidik jari atau identitas wali diambil langsung dari basis data Pendaftaran Peserta Didik Baru (PPDB), sehingga tidak perlu registrasi ulang.

### Versi 5.x: Ekosistem Perpustakaan Pintar

Ekspansi fungsionalitas menuju manajemen aset pendidikan dan literasi.

- **Pencatatan Pengunjung:** Mode khusus untuk mencatat statistik kunjungan siswa ke perpustakaan.
- **Sirkulasi Mandiri:** Integrasi sistem untuk peminjaman dan pengembalian buku perpustakaan secara mandiri menggunakan kartu siswa, menghubungkan data siswa dengan inventaris buku.

### Versi 6.x: Pemantauan Lokasi & Jaringan LoRa

Implementasi teknologi pelacakan aset dan komunikasi jarak jauh hemat daya untuk kompleks madrasah/kantor yang luas.

- **Pemantauan Keberadaan (Presence Monitoring):** Perangkat difungsikan sebagai pemindai (scanner) yang ditempatkan di setiap kelas atau ruang kantor untuk mendeteksi Bluetooth Tag (BLE Beacon) yang dibawa oleh setiap siswa atau pegawai secara otomatis.
- **Integrasi LoRa:** Menggunakan teknologi LoRa (Long Range) untuk mengirimkan data telemetri lokasi dari ruang kelas/kantor ke server pusat (Gateway), mengatasi keterbatasan jangkauan sinyal WiFi di area gedung yang luas atau terhalang tembok tebal.

---

## Catatan Integrasi

> **Penting:** Repositori ini berfokus pada pengembangan **Firmware (ESP32)**. Untuk dokumentasi API, struktur database, dan logika backend Laravel, silakan merujuk pada dokumentasi repositori _Backend_ yang terpisah.

Sistem ini menggunakan autentikasi berbasis **API Key** pada setiap _endpoint_ HTTP untuk menjamin keamanan pertukaran data antara mesin dan server.
