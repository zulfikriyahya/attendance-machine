# Cetak Biru Proyek: Attendance Machine (IoT) - Hybrid Edition

Dokumen ini menguraikan arsitektur, visi, dan spesifikasi teknis dari **Attendance Machine**, sebuah solusi presensi cerdas yang mengintegrasikan perangkat keras IoT (ESP32 + RFID + SD Storage) dengan ekosistem digital modern (Laravel + WhatsApp).

Fokus utama pembaruan ini adalah pada kemampuan **Offline-First** menggunakan mekanisme _Partitioned Queue System_ untuk menjamin integritas data di wilayah dengan konektivitas internet yang tidak stabil.

---

## Visi & Misi

> **"Mendigitalisasi, mengautomatisasi, dan mengintegrasikan data kehadiran seluruh lembaga di Provinsi Banten — khususnya Kabupaten Pandeglang — dengan sistem presensi cerdas yang tangguh (resilient), real-time, dan tanpa kehilangan data (zero data loss)."**

Tujuan utamanya adalah menghapus hambatan teknis akibat gangguan jaringan, menciptakan transparansi data, dan mempermudah pelaporan administratif bagi sekolah maupun instansi pemerintahan.

---

## Gambaran Sistem

**Attendance Machine v2.1** dirancang sebagai gerbang fisik data kehadiran dengan filosofi **"Self-Healing"**. Sistem mampu menyembuhkan diri sendiri dari kegagalan jaringan dengan menyimpan data secara lokal dan mengirimkannya secara cerdas saat koneksi pulih.

### Alur Kerja Utama

1.  **Identifikasi:** Pengguna menempelkan kartu RFID.
2.  **Validasi Lokal:** Perangkat memeriksa _debounce_ dan duplikasi data dalam 1 jam terakhir langsung dari penyimpanan lokal (SD Card) untuk mencegah input ganda.
3.  **Penyimpanan Cerdas (Queue System):**
    - **Online/Offline Agnostic:** Secara default, data diproses masuk ke dalam sistem antrean file CSV lokal (`queue_X.csv`).
    - **Rotasi File:** Data dipecah menjadi file-file kecil (maksimal 50 baris per file) untuk menjaga kestabilan memori RAM.
4.  **Sinkronisasi Latar Belakang:**
    - Sistem secara berkala (interval 60 detik) memeriksa keberadaan file antrean.
    - Jika WiFi terhubung, data dikirim per _batch_ (1 file sekaligus) ke server.
    - File lokal otomatis dihapus hanya jika server merespons sukses (HTTP 200).
5.  **Notifikasi:** Server memproses data _batch_ dan mengirim pesan WhatsApp ke orang tua/atasan.

---

## Fitur Unggulan (Pembaruan v2.1.0)

### A. Fitur Perangkat (Firmware & Hardware)

Fitur-fitur yang tertanam langsung pada mesin ESP32-C3:

- **Partitioned Queue System:** Menggunakan sistem penyimpanan multi-file (`queue_0.csv` s/d `queue_99.csv`). Mencegah _memory crash_ pada ESP32 dengan membatasi beban muat data hanya 50 rekaman per siklus sinkronisasi. Kapasitas total mencapai ±5.000 data offline.
- **SD Card Local Storage:** Penyimpanan fisik pada MicroSD memastikan data tetap aman meskipun perangkat kehilangan daya (_power loss_).
- **Smart Duplicate Prevention:** Algoritma cerdas yang memindai seluruh file antrean untuk menolak kartu yang sama jika di-scan ulang dalam waktu kurang dari 1 jam (bisa dikonfigurasi).
- **Bulk Upload Efficiency:** Menghemat bandwidth dengan mengirimkan 50 data presensi dalam satu kali permintaan HTTP (`POST /sync-bulk`), bukan satu per satu.
- **Hybrid Visual Feedback:** Layar OLED menampilkan status koneksi, jumlah antrean data yang belum terkirim (misal: `Q:150`), dan indikator kekuatan sinyal.
- **Zero Configuration:** Fitur _Multi-SSID Failover_ (otomatis pindah ke WiFi cadangan jika utama mati) tetap dipertahankan.

### B. Ekosistem Backend (Laravel - Modul Terpisah)

Layanan yang dijalankan oleh server untuk mendukung perangkat:

- **Bulk API Endpoint:** Menerima _array_ JSON data presensi dan memprosesnya secara asinkron.
- **Notifikasi WhatsApp:** Informasi kehadiran dikirim langsung ke wali siswa/pegawai.
- **Laporan Digital & E-Signature:** PDF otomatis dengan tanda tangan digital.
- **Monitoring:** Dashboard admin kini menampilkan status "Last Sync" untuk memantau kesehatan perangkat.

---

## Spesifikasi Perangkat Keras

Sistem v2.1.0 menambahkan modul penyimpanan eksternal untuk ketahanan data:

| Komponen      | Spesifikasi              | Fungsi Utama                                          |
| :------------ | :----------------------- | :---------------------------------------------------- |
| **MCU**       | ESP32-C3 Super Mini      | Otak pemrosesan utama, WiFi, manajemen file system.   |
| **Sensor**    | RFID RC522 (SPI)         | Membaca UID kartu presensi (Mifare 13.56MHz).         |
| **Storage**   | **MicroSD Module (SPI)** | **Menyimpan antrean data offline (CSV) & Log.**       |
| **Display**   | OLED 0.96" (I2C)         | Visualisasi status antrean (Queue Counter) & Koneksi. |
| **Indikator** | Active Buzzer            | Memberikan sinyal audio (Sukses/Gagal/Antrean Penuh). |
| **Power**     | 5V USB / 3.7V Li-ion     | Sumber daya fleksibel.                                |

> **Catatan Teknis:** RFID dan SD Card menggunakan jalur SPI yang sama (_Shared Bus_) pada pin SCK(4), MISO(5), MOSI(6), namun dengan pin _Chip Select_ (CS) yang berbeda untuk efisiensi pin pada ESP32-C3.

---

## Implementasi & Target Pengguna

Sistem ini didesain untuk lokasi dengan infrastruktur internet yang "hidup-mati" (intermittent connectivity).

**Sektor Utama:**

1.  **Sekolah Pedesaan:** Wilayah dengan sinyal seluler/WiFi tidak stabil. Data siswa aman di SD Card dan terkirim saat sinyal kembali.
2.  **Pabrik/Industri:** Presensi shift ribuan karyawan dalam waktu singkat tanpa _bottleneck_ pengiriman data satu per satu.
3.  **Kegiatan Luar Ruang (Field Events):** Presensi peserta di lokasi tanpa internet, sinkronisasi dilakukan saat kembali ke kantor.

---

## Peta Jalan Pengembangan (Roadmap)

### Versi 2.x: Era Penyimpanan Hibrida (Status: Rilis v2.1.0)

Fokus pada ketahanan data dan manajemen memori.

- **Queue System:** Implementasi _batch processing_ 50 data/file.
- **Fail-safe:** Mekanisme pembersihan file otomatis pasca-sinkronisasi.
- **Indikator Offline:** Tampilan jumlah antrean (Pending Count) pada layar.

### Versi 3.x: Integrasi Biometrik (Fingerprint)

Penambahan lapisan keamanan dan redundansi perangkat keras.

- **Modul Sidik Jari:** Integrasi sensor sidik jari optikal (UART) sebagai backup jika kartu RFID hilang.
- **Mode Ganda:** Mendukung input dari kartu RFID maupun sidik jari secara bersamaan.

### Versi 4.x: Manajemen Tamu & Integrasi PPDB

Transformasi perangkat menjadi kios pelayanan mandiri.

- **Buku Tamu Digital:** Pencatatan kunjungan menggunakan pemindaian KTP (NIK/RFID).
- **Integrasi PPDB:** Data identitas diambil langsung dari basis data Pendaftaran Peserta Didik Baru.

### Versi 5.x: Ekosistem Perpustakaan & Aset

Ekspansi fungsionalitas menuju manajemen aset.

- **Sirkulasi Mandiri:** Peminjaman buku perpustakaan mandiri (Self-Service Kiosk).
- **Pelacakan Aset:** Peminjaman barang inventaris sekolah/kantor.

### Versi 6.x: IoT Gateway & LoRa

Implementasi komunikasi jarak jauh hemat daya.

- **Presence Monitoring:** Deteksi otomatis BLE Beacon siswa di kelas.
- **LoRaWAN:** Pengiriman data presensi via gelombang radio jarak jauh untuk area _blank spot_ total.

---

## Catatan Integrasi

> **Penting untuk Backend Developer:**
> Firmware v2.1.0 mengubah metode pengiriman data dari _Single Entry_ menjadi _Bulk Array_.

Backend wajib menyediakan endpoint:

- **Method:** `POST`
- **URL:** `/api/presensi/sync-bulk`
- **Payload Example:**
  ```json
  {
    "data": [
      { "rfid": "...", "timestamp": "...", "device_id": "..." },
      { "rfid": "...", "timestamp": "...", "device_id": "..." }
    ]
  }
  ```
- **Response:** Wajib mengembalikan kode HTTP `200` agar perangkat menghapus data dari SD Card. Kode selain 200 akan menyebabkan perangkat mencoba mengirim ulang (retry) terus menerus.
