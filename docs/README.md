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

1.  **Identifikasi:** Pengguna menempelkan kartu RFID.
2.  **Pemrosesan:** Perangkat memvalidasi input dan memberikan umpan balik (Suara/Visual).
3.  **Sinkronisasi:**
    - **Online:** Data dikirim instan ke server via WiFi.
    - **Offline:** Data disimpan di memori internal dan dikirim otomatis saat koneksi pulih.
4.  **Notifikasi:** Server mengirim pesan WhatsApp ke orang tua/atasan secara _real-time_.

---

## Fitur Unggulan

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

## Roadmap Pengembangan

Fitur-fitur berikut direncanakan untuk pembaruan firmware dan hardware di masa depan:

- [ ] **Penyimpanan Eksternal:** Slot SD Card untuk _backup log_ jangka panjang.
- [ ] **Validasi Lokal:** Sinkronisasi database UID ke perangkat untuk validasi nama tanpa internet.
- [ ] **OTA Update:** Pembaruan firmware jarak jauh melalui dashboard admin.
- [ ] **Smart Audio:** Fitur _Text-to-Speech_ untuk menyebutkan nama pengguna saat scan.
- [ ] **Snapshot:** Integrasi kamera mini (ESP32-CAM) untuk bukti visual.
- [ ] **Geo-Fencing:** Validasi lokasi berdasarkan pemetaan MAC Address WiFi sekitar.

---

## Catatan Integrasi

> **Penting:** Repositori ini berfokus pada pengembangan **Firmware (ESP32)**. Untuk dokumentasi API, struktur database, dan logika backend Laravel, silakan merujuk pada dokumentasi repositori _Backend_ yang terpisah.

Sistem ini menggunakan autentikasi berbasis **API Key** pada setiap _endpoint_ HTTP untuk menjamin keamanan pertukaran data antara mesin dan server.
