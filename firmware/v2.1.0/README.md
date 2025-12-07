# Sistem Presensi RFID dengan Manajemen Antrean (Queue System) Berbasis ESP32-C3

## Deskripsi Proyek

Sistem ini adalah perangkat presensi kehadiran berbasis IoT yang dirancang untuk lingkungan dengan konektivitas intermiten. Versi 2.1.0 memperkenalkan arsitektur penyimpanan **Sistem Antrean Berbasis Berkas (_File-based Queue System_)**.

Pembaruan ini mengatasi keterbatasan memori (RAM) pada mikrokontroler ESP32-C3 saat menangani data offline dalam jumlah besar. Alih-alih menyimpan ribuan data dalam satu berkas CSV besar yang membebani _buffer_, sistem memecah data menjadi beberapa berkas antrean kecil yang diproses secara berurutan.

## Fitur Utama (Versi 2.1.0)

### 1. Arsitektur Antrean Terdistribusi

- **Segmentasi Data:** Data presensi disimpan dalam berkas terpisah dengan penamaan sekuensial (`/queue_0.csv`, `/queue_1.csv`, dst).
- **Batas Memori:** Setiap berkas dibatasi maksimal **50 rekaman**. Hal ini memastikan penggunaan buffer RAM tetap rendah (< 2KB) saat proses pembacaan dan pengiriman data.
- **Kapasitas Masif:** Mendukung hingga 100 berkas antrean, memberikan kapasitas penyimpanan total 5.000+ data presensi tanpa risiko _heap fragmentation_.

### 2. Manajemen Sinkronisasi Efisien

- **Sync Per-File:** Proses sinkronisasi melakukan pengunggahan satu berkas antrean pada satu waktu.
- **Penghapusan Otomatis:** Berkas antrean (`.csv`) hanya dihapus dari SD Card setelah server memberikan respon sukses (HTTP 200), menjamin integritas data (transaksi atomik).
- **Rotasi Otomatis:** Sistem secara otomatis membuat berkas antrean baru saat berkas aktif telah mencapai batas 50 rekaman.

### 3. Pencegahan Redundansi

- **Pengecekan Duplikasi Global:** Meskipun data terpecah dalam banyak berkas, algoritma sistem memindai seluruh indeks antrean yang tersedia untuk mencegah pencatatan kartu yang sama dalam interval waktu yang ditentukan (default: 1 jam).

## Spesifikasi Perangkat Keras dan Pinout

Sistem menggunakan bus SPI bersama (_shared SPI bus_) untuk modul RFID dan SD Card. Manajemen perangkat dilakukan melalui pin _Chip Select_ (CS) yang berbeda.

| Komponen       | Pin Modul | Pin ESP32-C3 | Fungsi                       |
| :------------- | :-------- | :----------- | :--------------------------- |
| **Bus SPI**    | SCK       | GPIO 4       | Clock (Shared)               |
|                | MOSI      | GPIO 6       | Master Out Slave In (Shared) |
|                | MISO      | GPIO 5       | Master In Slave Out (Shared) |
| **RFID RC522** | SDA (SS)  | GPIO 7       | Chip Select RFID             |
|                | RST       | GPIO 3       | Reset                        |
| **SD Card**    | CS        | GPIO 1       | Chip Select SD Card          |
| **OLED**       | SDA       | GPIO 8       | I2C Data                     |
|                | SCL       | GPIO 9       | I2C Clock                    |
| **Buzzer**     | VCC       | GPIO 10      | Output Audio                 |

## Prasyarat Perangkat Lunak

Pastikan pustaka berikut terinstal pada lingkungan pengembangan:

1.  **MFRC522** (Akses perangkat keras RFID).
2.  **Adafruit SSD1306** & **Adafruit GFX** (Antarmuka OLED).
3.  **ArduinoJson** (Serialisasi data JSON untuk API).
4.  **SD** & **FS** (Sistem berkas, bawaan paket ESP32).
5.  **WiFi**, **HTTPClient**, **Wire**, **SPI** (Bawaan paket ESP32).

## Konfigurasi Parameter Antrean

Parameter berikut dalam kode sumber mengontrol perilaku sistem antrean:

```cpp
// Batas rekaman per file CSV untuk menjaga stabilitas RAM
const int MAX_RECORDS_PER_FILE      = 50;

// Jumlah maksimal file antrean (queue_0.csv s.d queue_99.csv)
const int MAX_QUEUE_FILES           = 100;

// Interval antar sinkronisasi (ms)
const unsigned long SYNC_INTERVAL   = 30000;

// Batas waktu pencegahan tap ganda (detik)
const unsigned long MIN_REPEAT_INTERVAL = 3600;
```

## Mekanisme Operasional

### 1. Logika Penyimpanan (Write)

1.  Saat kartu dipindai, sistem memeriksa apakah `queue_N.csv` saat ini memiliki jumlah baris < `MAX_RECORDS_PER_FILE`.
2.  Jika penuh, sistem melakukan increment pada indeks (`N+1`) dan membuat berkas baru.
3.  Jika indeks mencapai `MAX_QUEUE_FILES`, sistem kembali ke indeks 0 (mekanisme _overwrite_ sirkular, namun dalam praktik normal data lama sudah tersinkronisasi dan terhapus).

### 2. Logika Sinkronisasi (Read/Upload)

1.  Fungsi sinkronisasi memindai keberadaan berkas dari `queue_0.csv` hingga `queue_99.csv`.
2.  Jika berkas ditemukan dan berisi data, sistem membaca seluruh isinya ke dalam memori.
3.  Data dikirim ke endpoint `sync-bulk`.
4.  Jika API merespon `200 OK`, berkas tersebut dihapus dari SD Card.
5.  Proses berlanjut ke berkas antrean berikutnya.

## Spesifikasi API Backend

### Endpoint Sinkronisasi Massal (Bulk Sync)

Sistem menggunakan endpoint ini untuk mengirimkan satu _batch_ (maksimal 50 data) sekaligus.

- **URL:** `/api/presensi/sync-bulk`
- **Method:** `POST`
- **Header:** `Content-Type: application/json`, `X-API-KEY: [Token]`
- **Payload Request:**
  ```json
  {
    "data": [
      {
        "rfid": "1234567890",
        "timestamp": "2025-12-07 08:00:01",
        "device_id": "ESP32_A1B2",
        "sync_mode": true
      },
      ... (hingga 50 objek)
    ]
  }
  ```

## Struktur Penyimpanan File

Pada SD Card, struktur berkas akan terlihat sebagai berikut saat mode offline aktif:

```text
/
├── queue_0.csv   (Berisi data presensi 1-50)
├── queue_1.csv   (Berisi data presensi 51-100)
├── queue_2.csv   (Berisi data presensi 101-...)
```

Setiap berkas CSV memiliki header: `rfid,timestamp,device_id,unix_time`.

## Riwayat Versi

- **v2.1.0 (Desember 2025):** Implementasi Sistem Antrean Multi-File untuk optimalisasi memori dan peningkatan kapasitas penyimpanan offline.
- **v2.0.0:** Implementasi awal penyimpanan offline (Single CSV file).
- **v1.0.0:** Rilis awal (Online Only).

## Lisensi

Hak Cipta © 2025. Perangkat lunak ini dikembangkan untuk penggunaan internal dan tujuan edukasi. Distribusi ulang diizinkan dengan menyertakan atribusi penulis.
