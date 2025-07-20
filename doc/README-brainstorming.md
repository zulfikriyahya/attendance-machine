# 🧠 Brainstorming – Attendance Machine (ESP32 + RFID + Laravel + WhatsApp)

Dokumen ini digunakan untuk memetakan ide, visi, dan fitur utama dari proyek **Attendance Machine**, sebuah sistem presensi otomatis generasi baru yang menggabungkan teknologi RFID, ESP32, Laravel API, serta notifikasi real-time via WhatsApp.

Fokus utama proyek ini adalah pada **perangkat presensi (mesin)** — firmware dan hardware — yang bekerja terintegrasi dengan sistem server modular (dibuat terpisah).

---

## 🎯 Visi Proyek

> “Mendigitalisasi, mengautomatisasi, dan mengintegrasikan data kehadiran seluruh lembaga di Provinsi Banten — khususnya Kabupaten Pandeglang — dengan sistem presensi cerdas yang real-time, fleksibel, efisien, dan terjangkau.”

---

## 📌 Deskripsi Singkat

Attendance Machine adalah perangkat presensi otomatis yang:

- Menggunakan kartu RFID untuk pencatatan kehadiran
- Mengirim data ke server Laravel secara real-time via WiFi
- Menyimpan data secara **offline** jika tidak ada koneksi, lalu menyinkronkan kembali saat online
- Memberikan **notifikasi WhatsApp** kepada orang tua siswa atau pihak terkait secara instan
- Menampilkan hasil scan via layar OLED dan buzzer
- Untuk pengajuan ketidakhadiran pegawai atau siswa bisa dilakukan pada aplikasi backendnya, tanpa khawatir presensi di alfakan
- Bisa digunakan **tanpa instalasi atau reset**, cukup dinyalakan di lokasi mana pun

---

## 🚀 Fitur Utama Perangkat

| Fitur                    | Penjelasan                                                                |
| ------------------------ | ------------------------------------------------------------------------- |
| 📡 Presensi RFID         | Menggunakan modul RC522 atau PN532 via SPI                                |
| 🖥️ Layar OLED            | Menampilkan status presensi (nama, waktu, hasil)                          |
| 🔊 Buzzer Aktif          | Feedback suara setelah scan berhasil/gagal                                |
| ⚙️ Konfigurasi Mudah     | Hanya melalui file `config.h`, plug & play tanpa pengaturan ulang         |
| 📶 Multi WiFi SSID       | Otomatis konek ke jaringan WiFi mana pun yang tersedia                    |
| 💤 Sleep Mode Terjadwal  | Otomatis hemat daya di luar jam operasional                               |
| 📨 Notifikasi WhatsApp   | Dikirim via backend Laravel ke orang tua/pegawai secara real-time         |
| 💾 Offline Mode          | Rekam presensi tanpa internet/listrik → sinkron otomatis saat online      |
| 🔐 API Auth + Debounce   | Kirim data via API Key + anti dobel scan per waktu/jam tertentu           |
| 🔋 Portable & Hemat Daya | Bisa pakai baterai BL-5C / Li-ion, tahan berjam-jam tanpa pengisian ulang |
| ⚡ Super Cepat           | Kecepatan scan ± 1 detik per pengguna                                     |
| 📡 Sync Indicator        | Indikator visual saat sinkronisasi offline sedang berlangsung             |

---

## 🧠 Backend (Dikerjakan Terpisah)

> Mesin ini hanya fokus pada firmware dan perangkat keras. Sistem backend Laravel sudah disiapkan secara modular untuk:

- Manajemen akun siswa/pegawai
- Laporan PDF & Excel yang **fleksibel berdasarkan filter waktu, jabatan, instansi**
- Tanda tangan elektronik yang **sah dan terverifikasi**
- Notifikasi WhatsApp otomatis
- Dashboard admin berbasis FilamentPHP
- Log sinkronisasi offline
- Monitoring status mesin (ping, baterai, histori)

---

## 👥 Dukungan untuk Banyak Pengguna

Sistem ini didesain untuk:

- **Skala besar tanpa reset**: cukup sambungkan ke WiFi dan nyalakan
- Mendukung **ratusan hingga ribuan pengguna**
- Presensi otomatis tanpa antrian, anti dobel tap
- Tidak tergantung lokasi atau konfigurasi ulang
- Bisa deployment masal ke sekolah/kantor hanya dalam hitungan menit

---

## 🌍 Target Utama Implementasi

- Sekolah dasar, menengah, pesantren
- Kantor kecamatan, kelurahan, dinas daerah
- Instansi pemerintah daerah dan lembaga pendidikan
- Layanan publik di wilayah Provinsi Banten (terutama Pandeglang)

---

## 📈 Keunggulan Sistem

| Aspek                 | Nilai Unggul                                                     |
| --------------------- | ---------------------------------------------------------------- |
| 💰 Biaya Operasional  | Lebih murah dari fingerprint/alat presensi komersial lainnya     |
| ⚙️ Skalabilitas       | Bisa digunakan di banyak tempat tanpa pengaturan ulang           |
| ⏱️ Efisiensi Waktu    | Scan cepat, respon langsung, minim error                         |
| 📊 Akurasi Data       | Presensi lengkap, dilengkapi tanda tangan elektronik yang sah    |
| 📡 Konektivitas       | Tidak butuh server lokal, cukup WiFi publik/sekolah/kantor       |
| 🛠️ Maintenance Rendah | Tidak perlu reset, tidak tergantung kartu SIM, tidak mudah rusak |
| 🔒 Aman               | API dengan autentikasi dan enkripsi ringan                       |

---

## 🛠️ Komponen Mesin (Firmware & Hardware)

| Komponen     | Keterangan                           |
| ------------ | ------------------------------------ |
| ESP32-C3     | Board utama dengan WiFi & hemat daya |
| RFID RC522   | Pembaca kartu RFID via SPI           |
| OLED 0.96"   | Tampilan informasi presensi          |
| Buzzer Aktif | Feedback suara presensi              |
| Power        | USB / Baterai (3.7–5V)               |
| SD Card      | (Opsional) Penyimpanan cadangan      |

---

## 🔮 Fitur Futuristik yang Direncanakan

- 📦 Penyimpanan internal ke SD Card untuk backup tambahan
- 🔐 Validasi UID RFID secara lokal (tanpa query ke server)
- 📡 OTA Firmware Update dari backend
- 🧑‍🎓 Pemanggilan nama dengan suara (Text-to-Speech)
- 📷 Snapshot presensi dengan kamera mini (opsional)
- 📊 Statistik lokal di layar ePaper/OLED sekunder
- 📤 Backup data offline ke USB flash drive
- 🧠 Integrasi AI ringan untuk analisis kebiasaan (eksperimental)
- 🎯 Presensi berbasis zona lokasi (geo-fencing via WiFi SSID mapping)

---

## 📄 Catatan Penutup

Proyek ini bukan sekadar alat presensi biasa. Ini adalah **bagian dari solusi digitalisasi dan integrasi data kehadiran secara menyeluruh**. Dengan pendekatan yang portable, scalable, dan open-architecture, sistem ini dapat diadopsi secara cepat oleh berbagai lembaga di seluruh wilayah Provinsi Banten — terutama Pandeglang — tanpa hambatan teknis maupun biaya besar.

---

> 📌 _Dokumen ini akan terus diperbarui sesuai dengan perkembangan fitur dan penerapan di lapangan._
