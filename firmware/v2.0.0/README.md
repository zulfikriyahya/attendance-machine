# Catatan Rilis Firmware v2.0.0 (Hybrid Mode)

**Versi:** 2.0.0 (Beta/Ongoing)
**Tanggal:** Desember 2025
**Platform:** ESP32-C3 Super Mini
**Status:** _Offline-First Architecture_

## Ringkasan

Versi 2.0.0 adalah pembaruan besar yang mengubah arsitektur sistem dari _Online-Only_ menjadi **Hybrid (Offline-First)**. Pembaruan ini menambahkan modul **MicroSD** sebagai penyimpanan lokal. Sistem kini mampu melakukan validasi pengguna dan menyimpan data presensi sepenuhnya tanpa koneksi internet, lalu menyinkronkannya secara massal (_bulk sync_) ketika koneksi tersedia.

## Fitur Baru

1.  **Penyimpanan Lokal (SD Card):**
    - Menyimpan database pengguna (`user_rfid.json`) untuk validasi offline.
    - Menyimpan jadwal operasional (`jadwal_presensi.json`).
    - Menyimpan antrean presensi (`presensi_queue.json`) saat offline.
2.  **Smart Bulk Sync:**
    - Mengirim data antrean ke server dalam satu paket JSON (batch) untuk menghemat bandwidth.
    - Sinkronisasi otomatis dipicu setiap 5 menit atau jika antrean > 10 data.
3.  **Manajemen Antrean (Queue):**
    - Mendukung hingga 50 data antrean lokal.
    - Mekanisme _retry_ otomatis jika sinkronisasi gagal.
4.  **Validasi Lokal:**
    - Nama dan Tipe pengguna (Siswa/Guru) langsung muncul di layar OLED meski internet mati.
5.  **Fail-Safe:**
    - Jika SD Card rusak/hilang, sistem memberikan peringatan dan mencoba beralih ke mode darurat (jika memungkinkan).

## Konfigurasi Perangkat Keras

Karena penambahan modul SD Card, penggunaan pin (GPIO) mengalami perubahan, terutama pada jalur SPI yang kini digunakan bersama (_Shared SPI Bus_).

### Pin Mapping (ESP32-C3 Super Mini)

| Komponen       | Pin Modul    | Pin ESP32 (GPIO) | Keterangan          |
| :------------- | :----------- | :--------------- | :------------------ |
| **Jalur SPI**  | **SCK**      | **4**            | Shared Clock        |
|                | **MISO**     | **5**            | Shared MISO         |
|                | **MOSI**     | **6**            | Shared MOSI         |
| **MicroSD**    | **CS**       | **1**            | Chip Select SD Card |
|                | VCC          | 3.3V / 5V        | Tergantung modul    |
| **RFID RC522** | **SDA (SS)** | **7**            | Chip Select RFID    |
|                | RST          | 3                | Reset               |
| **OLED**       | SDA          | 8                | I2C Data            |
|                | SCL          | 9                | I2C Clock           |
| **Buzzer**     | (+)          | 10               | Aktif High          |

> **PENTING:** Pastikan modul SD Card yang digunakan kompatibel dengan logika 3.3V ESP32. Format kartu memori harus **FAT32**.

## Struktur File SD Card

Sistem mengharapkan struktur file berikut pada _root_ direktori SD Card:

1.  **`user_rfid.json`** (Database Pengguna)

    - Diunduh otomatis dari server saat _booting_ (jika online).
    - Format:
      ```json
      {
        "version": 1,
        "data": [
          { "rfid": "1234567890", "nama": "Ahmad", "type": "SISWA" },
          { "rfid": "0987654321", "nama": "Budi", "type": "GURU" }
        ]
      }
      ```

2.  **`jadwal_presensi.json`** (Jadwal)

    - Format:
      ```json
      {
        "status": true,
        "masuk": "07:00:00",
        "pulang": "16:00:00"
      }
      ```

3.  **`presensi_queue.json`** (Cache Antrean)
    - Dibuat otomatis oleh sistem untuk menyimpan data yang belum terkirim.

## Spesifikasi API Backend (v2)

Firmware ini berinteraksi dengan endpoint berikut:

| Method | Endpoint                  | Fungsi                                     |
| :----- | :------------------------ | :----------------------------------------- |
| `GET`  | `/api/presensi/ping`      | Cek koneksi server                         |
| `GET`  | `/api/presensi/db/users`  | Download database pengguna terbaru         |
| `GET`  | `/api/presensi/db/jadwal` | Download konfigurasi jadwal                |
| `POST` | `/api/presensi/sync-bulk` | Mengirim array data presensi (Bulk Upload) |

**Contoh Payload `sync-bulk`:**

```json
{
  "data": [
    {
      "rfid": "1234567890",
      "timestamp": "2025-12-07T07:15:00",
      "device_id": "ESP32_GATE_1"
    },
    ...
  ]
}
```

## ⚙️ Cara Instalasi & Flash

1.  **Persiapan Library:**
    Pastikan library berikut terinstall di Arduino IDE:

    - `SD` (Built-in ESP32)
    - `ArduinoJson` (v6.x)
    - `MFRC522`
    - `Adafruit SSD1306` & `Adafruit GFX`

2.  **Konfigurasi Kode:**
    Buka `main.ino` dan sesuaikan variabel di bagian atas:

    ```cpp
    const char SSID1[] = "Nama_WiFi";
    const char PASS1[] = "Password_WiFi";
    const char API_URL[] = "http://ip-server-anda:8000";
    const char API_KEY[] = "API_Key_Anda";
    ```

3.  **Upload:**

    - Board: **ESP32C3 Dev Module**
    - USB CDC On Boot: **Enabled** (Agar serial monitor terbaca)
    - Flash Mode: **DIO**

4.  **Troubleshooting SD Card:**
    - Jika muncul "SD CARD GAGAL", cek kabel wiring terutama pin CS (GPIO 1).
    - Pastikan kartu terformat FAT32 (bukan exFAT atau NTFS).

## Known Issues (Isu yang Diketahui)

- **Batas Memori:** Kapasitas array `userDB` dibatasi 200 user di RAM (`UserCache userDB[200]`). Jika pengguna lebih dari 200, perlu optimasi kode untuk membaca langsung dari SD Card per baris (stream) daripada memuat semua ke RAM.
- **Waktu Booting:** Booting awal memakan waktu ±5-10 detik untuk mengunduh database json jika ukuran file besar.
