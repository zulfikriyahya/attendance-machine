# Catatan Rilis Firmware v2.1.0 (Queue System)

- **Versi:** 2.1.0 (Queue System / Multi-file)
- **Tanggal Rilis:** Desember 2025
- **Perangkat Target:** ESP32-C3 Super Mini
- **Penulis:** Yahya Zulfikri

## Ringkasan

Versi 2.1.0 adalah penyempurnaan dari sistem penyimpanan offline. Pada versi sebelumnya (v2.0.0), data disimpan dalam satu file besar yang berisiko menyebabkan _Heap Overflow_ (kehabisan RAM) saat proses pembacaan dan pengiriman data.

Versi 2.1.0 memperkenalkan **Sistem Antrean Multi-File (Queue System)**. Data kini dipecah menjadi file-file kecil (`queue_0.csv`, `queue_1.csv`, dst.) dengan batasan jumlah baris yang ketat. Ini memastikan penggunaan memori tetap rendah dan stabil meskipun perangkat menyimpan ribuan data offline.

## Fitur Baru dan Perubahan

### 1. Sistem Antrean Terpartisi (Partitioned Queue)

- **Multi-File Storage:** Sistem tidak lagi menggunakan satu file `presensi.csv`. Sebagai gantinya, sistem membuat file berurutan: `/queue_0.csv`, `/queue_1.csv`, hingga `/queue_99.csv`.
- **Rotasi Otomatis:** Setiap file dibatasi maksimal **50 data**. Jika file aktif penuh, sistem otomatis membuat file baru dengan indeks berikutnya.
- **Kapasitas Besar:** Mendukung hingga 100 file antrean, memungkinkan penyimpanan total ±5.000 data kehadiran secara offline.

### 2. Manajemen Memori & Stabilitas

- **Sinkronisasi Per-File:** Saat koneksi internet tersedia, sistem mengirimkan satu file (50 data) ke server. Jika sukses, file tersebut dihapus dari SD Card, membebaskan ruang memori sebelum memproses file berikutnya.
- **Pencegahan RAM Crash:** Dengan membatasi 50 data per batch, alokasi JSON Document tidak akan melebihi kapasitas RAM ESP32-C3.

### 3. Manajemen Perangkat Keras (SPI)

- **Manual Chip Select (CS) Toggle:** Kode program kini secara eksplisit mematikan (HIGH) dan menghidupkan (LOW) pin CS untuk RFID dan SD Card secara bergantian sebelum setiap operasi SPI. Ini mencegah konflik data (_bus contention_) karena kedua perangkat berbagi jalur SPI yang sama.

## Detail Teknis

### Mekanisme Queue (Cara Kerja)

1.  **Penyimpanan (Write):**
    - Cek file antrean terakhir (misal: `queue_0.csv`).
    - Jika isi < 50 baris, tambahkan data baru.
    - Jika isi = 50 baris, buat file baru (`queue_1.csv`) dan tulis di sana.
2.  **Sinkronisasi (Read & Upload):**
    - Cari file antrean yang ada.
    - Baca isi file ke dalam array buffer.
    - Kirim ke API (`POST /sync-bulk`).
    - Jika respons **200 OK**, hapus file tersebut dari SD Card.
    - Ulangi untuk file berikutnya.

### Diagram Pin (ESP32-C3 Super Mini)

Perhatikan bahwa **CS SD Card** dan **SS RFID** harus terpisah, namun SCK, MISO, dan MOSI digabung.

| Komponen    | Pin ESP32  | Fungsi       | Status         |
| :---------- | :--------- | :----------- | :------------- |
| **SPI BUS** | GPIO 4     | SCK          | Shared         |
|             | GPIO 6     | MOSI         | Shared         |
|             | GPIO 5     | MISO         | Shared         |
| **RFID**    | **GPIO 7** | **SDA (SS)** | **Individual** |
|             | GPIO 3     | RST          | -              |
| **SD Card** | **GPIO 1** | **CS**       | **Individual** |
| **OLED**    | GPIO 8     | SDA          | I2C            |
|             | GPIO 9     | SCL          | I2C            |
| **Buzzer**  | GPIO 10    | (+)          | Output         |

### Spesifikasi API

Backend harus siap menerima data dalam format array (Batch).

- **Endpoint:** `POST /api/presensi/sync-bulk`
- **Header:** `Content-Type: application/json`, `X-API-KEY: [Kunci]`
- **Contoh Payload:**
  ```json
  {
    "data": [
      {
        "rfid": "1234567890",
        "timestamp": "2025-12-07 08:00:00",
        "device_id": "ESP32_A1B2",
        "sync_mode": true
      },
      ... (maksimal 50 item)
    ]
  }
  ```

## Indikator Layar OLED

Versi ini menambahkan indikator antrean:

- **Q:120** (Pojok Kanan Atas): Menandakan ada total 120 data antrean yang tersimpan di SD Card dan menunggu sinkronisasi.
- **ONLINE / OFFLINE**: Status koneksi saat ini.

## Instalasi

1.  **Format SD Card:** Pastikan SD Card diformat menggunakan **FAT32**.
2.  **Bersihkan SD Card (Opsional):** Jika Anda upgrade dari v2.0.0, disarankan menghapus file `presensi.csv` lama atau membackupnya, karena v2.1.0 menggunakan format penamaan file yang berbeda (`queue_X.csv`).
3.  **Upload Firmware:**
    - Board: ESP32C3 Dev Module.
    - USB CDC On Boot: Enabled.
    - Pastikan library **ArduinoJson** dan **MFRC522** terupdate.

## Troubleshooting

- **Gagal Init SD Card:**
  - Pastikan GPIO 1 (CS SD Card) terhubung dengan benar. Pada beberapa board ESP32-C3, GPIO 1 terhubung ke LED internal, yang mungkin menyebabkan gangguan sinyal jika tidak menggunakan modul SD Card dengan rangkaian pull-up yang baik.
- **RFID Tidak Terbaca saat SD Card Terpasang:**
  - Ini biasanya masalah jalur MISO yang "tidak dilepas" oleh modul SD Card murah. Firmware v2.1.0 sudah menangani ini secara software dengan manajemen CS yang ketat, namun pastikan kabel ground terhubung solid.
