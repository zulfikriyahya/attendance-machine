# Sistem Presensi RFID - Queue System v2.2.4 (Ultimate - Eco Display Edition)

## Deskripsi Proyek
**Sistem Presensi Pintar v2.2.4** adalah iterasi terbaru yang berfokus pada **Efisiensi Energi dan Umur Komponen**. Versi ini mempertahankan semua keunggulan konektivitas jarak jauh dari v2.2.3, namun menambahkan manajemen cerdas untuk layar OLED guna mencegah *burn-in* (layar berbayang) dan menghemat konsumsi daya pada jam-jam operasional tertentu.

Pembaruan utama pada versi ini adalah fitur **Scheduled OLED Dimming** yang mematikan layar secara otomatis pada jam kerja siang hari, namun tetap responsif (menyala sebentar) saat ada interaksi pengguna.

## Fitur Baru (Versi 2.2.4)

### 1. Smart OLED Management (NEW!)
Untuk memperpanjang umur layar OLED SSD1306, sistem kini memiliki jadwal otomatis:
- **Auto-OFF (08:00 - 14:00):** Layar akan mati total secara otomatis. CPU dan WiFi tetap aktif bekerja di latar belakang (menerima sinkronisasi waktu, menjaga koneksi).
- **Wake-on-Tap:** Meskipun layar mati (dalam mode Dim), jika pengguna menempelkan kartu RFID, **layar akan menyala otomatis** untuk menampilkan status presensi, lalu mati kembali setelah selesai.
- **Auto-ON (14:00 - 18:00):** Layar kembali menyala standby normal.

### 2. Hierarki Manajemen Daya
Sistem kini memiliki dua tingkatan penghematan daya:
1.  **OLED Dim Mode (08:00 - 14:00):** Hanya layar mati. CPU aktif, WiFi Connected. Fungsi presensi berjalan 100%.
2.  **Deep Sleep Mode (18:00 - 05:00):** Sistem mati total (CPU Off, WiFi Off). Hemat daya ekstrem. Hanya RTC yang berjalan.

### 3. Warisan Fitur v2.2.3 (Connectivity)
Semua fitur optimasi sinyal tetap dipertahankan:
- **Max Transmission Power (19.5dBm)** untuk jangkauan WiFi jauh.
- **Granular Queue System** (25 record per file) untuk keamanan data.
- **Background Sync** untuk pengiriman data tanpa gangguan.

## Konfigurasi Jadwal (v2.2.4)
Pengaturan jadwal didefinisikan pada bagian awal kode sumber. Anda dapat menyesuaikan jam ini sesuai kebutuhan operasional:

```cpp
// Konfigurasi Deep Sleep (Sistem Mati Total)
const int SLEEP_START_HOUR    = 18;   // Jam 18:00 sore mesin mati
const int SLEEP_END_HOUR      = 5;    // Jam 05:00 pagi mesin nyala

// Konfigurasi OLED Dim (Layar Mati, Mesin Hidup) - NEW v2.2.4
const int OLED_DIM_START_HOUR = 8;    // Jam 08:00 pagi layar mati
const int OLED_DIM_END_HOUR   = 14;   // Jam 14:00 siang layar nyala kembali
```

## Perbedaan Perilaku Sistem

| Status | Jam | Perilaku Layar | Koneksi WiFi | Bisa Tap Kartu? |
| :--- | :--- | :--- | :--- | :--- |
| **Pagi (Start)** | 05:00 - 07:59 | **ON** (Standby) | Connected | YA |
| **Siang (Dim)** | 08:00 - 13:59 | **OFF** (Mati) | Connected | **YA** (Layar nyala sesaat) |
| **Sore (Normal)** | 14:00 - 17:59 | **ON** (Standby) | Connected | YA |
| **Malam (Sleep)** | 18:00 - 04:59 | **OFF** (Mati) | **OFF** | TIDAK |

## Flowchart Logika OLED v2.2.4

```
[Start Loop]
    ↓
[Cek Jam Sekarang]
    ↓
[Apakah jam 08:00 s.d 13:59?]
    │
    ├─► YA ──► [Matikan Layar / Jaga Layar Mati] ──► [Tunggu Tap RFID]
    │                                                   ↓
    │                                             [Ada Tap?] ──► YA ──► [Nyalakan Layar]
    │                                                                        ↓
    │                                                                   [Proses Data]
    │                                                                        ↓
    │                                                                   [Matikan Layar Kembali]
    │
    └─► TIDAK ──► [Nyalakan Layar / Jaga Layar Hidup]
```

## Spesifikasi Teknis
- **Mikrokontroler:** ESP32-C3 Super Mini
- **Display:** SSD1306 OLED (I2C)
- **Library Tambahan:** Tidak ada library baru dibanding v2.2.3.
- **Logic:** Implementasi flag `oledIsOn` untuk mencegah instruksi `display.display()` dikirim saat layar seharusnya mati, menghemat siklus CPU I2C.

## Troubleshooting

### Masalah: Layar mati padahal mesin hidup
**Analisa:** Cek jam saat ini.
1. Jika antara jam 08:00 - 14:00, ini adalah fitur normal (**OLED Dim**). Coba tap kartu, layar harusnya menyala.
2. Jika antara jam 18:00 - 05:00, ini adalah **Deep Sleep**.
3. Jika di luar jam tersebut layar mati, periksa kabel VCC/GND OLED.

### Masalah: Jam tidak sesuai (Layar mati di jam salah)
**Solusi:** Sistem bergantung pada sinkronisasi NTP (`pool.ntp.org`). Pastikan perangkat terhubung ke WiFi minimal sekali saat *booting* untuk mendapatkan jam yang akurat. Jika WiFi mati saat startup, jam internal mungkin salah (tahun 1970), menyebabkan jadwal kacau.

### Masalah: Tap kartu responnya lambat saat OLED Dim
**Penjelasan:** Saat layar mati, CPU tetap berjalan normal. Namun, ada sedikit delay (ms) tambahan untuk perintah "Wake Up" layar OLED sebelum menampilkan nama pemilik kartu. Ini normal.

## Riwayat Perubahan (Changelog)

### v2.2.4 (Desember 2025) - Eco Display
- **Smart OLED Dimming:** Otomatis mematikan layar di jam kerja siang (08:00-14:00) untuk mencegah burn-in.
- **Wake-on-Interaction:** Mekanisme *interrupt* logika untuk menyalakan layar sementara saat tapping.
- **I2C Optimization:** Mencegah pengiriman data buffer ke OLED saat mode Dim aktif.

### v2.2.3 (Desember 2025) - Connectivity
- WiFi Signal Boost (19.5dBm).
- Smart AP Sorting.
- Granular Queue (25 records).

### v2.2.2 (Desember 2025) - Ultimate
- Background Process (Sync & Reconnect).
- Migrasi SdFat.

## Lisensi
Hak Cipta © 2025 Yahya Zulfikri. MIT License.
