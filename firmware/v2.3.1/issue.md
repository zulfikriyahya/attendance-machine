# Aturan

## Konteks Proyek
- Firmware ESP32-C3 "Attendance Machine" (Arduino IDE, C++), dokumen SRS hasil reverse-engineering source code adalah sumber kebenaran tunggal untuk perilaku sistem saat ini. Semua perubahan harus konsisten dengan SRS tersebut, kecuali secara eksplisit disetujui untuk diubah pada sesi ini.

## 1. Acuan Utama
Semua implementasi mengikuti SRS secara ketat. Jika ada bagian ambigu atau SRS tidak memberi formula pasti (contoh: penentuan slot antrian kosong, format versi firmware), **jangan menebak diam-diam** - tandai eksplisit dengan komentar `// TODO: ASUMSI - ...` beserta alasan.

## 2. Hindari Emoticon
Kode, komentar, dan pesan commit tetap formal, termasuk string yang tampil di OLED/Serial.

## 3. Prinsip DRY - Satu Sumber Kebenaran
- Definisi pin, konstanta interval, dan ukuran buffer **hanya** di header/`#define` yang sudah ada - jangan duplikasi angka literal (magic number) di file/fungsi lain.
- Logika enkripsi/dekripsi NVS hanya lewat `encryptString`/`decryptString`/`saveEncryptedNvs`/`loadEncryptedNvs` - jangan buat jalur enkripsi baru.
- Logika duplikasi scan (SD, NVS, direct HTTP) harus disatukan dalam satu fungsi pemeriksa bersama, bukan tiga implementasi terpisah yang berpotensi tidak konsisten (lihat Gap #10).
- Sebelum menambah fungsi/struct/enum baru, cek dulu apakah nama serupa sudah ada di file yang sama (satu file `.ino` besar, jadi risiko duplikasi nama tinggi).

## 4. Komentar Singkat
Komentar hanya sebagai penanda ringkas (mis. `// dilindungi mutex`, `// TODO: ...`), bukan narasi panjang.

## 5. Tandai Gap dengan TODO
Setiap bagian yang menyimpang dari SRS atau merupakan keputusan sepihak (pemilihan pin, pemilihan algoritma pembanding versi, dsb.) wajib ditandai `// TODO: GAP-SRS - ...` di lokasi kode terkait, bukan hanya di dokumen terpisah.

## 6. Tidak Ada File Placeholder Kosong
Jika project dipecah menjadi beberapa file (`.h`/`.cpp`) untuk modularisasi, setiap file wajib memiliki include guard/isi minimal - jangan menyerahkan file kosong yang membuat kompilasi Arduino IDE gagal.

## 7. Verifikasi API/Library Eksternal
Untuk setiap pemanggilan fungsi dari library eksternal yang aktif berkembang (MFRC522, SdFat, Adafruit_SSD1306, ArduinoJson, ESP-IDF `esp_task_wdt`, `Update`, `esp_ota_ops`), verifikasi signature terhadap dokumentasi/versi yang benar-benar terpasang **sebelum** menuliskan kode. Jika tidak bisa diverifikasi saat itu, tandai:
```cpp
// TODO: verifikasi signature terhadap versi library yang terpasang
```
Berlaku khusus untuk perubahan pada endpoint HTTP backend - verifikasi format request/response terhadap kontrak API backend yang aktual, jangan berasumsi dari kode lama.

## 8. Kembangkan Bertahap, Build Harus Lolos
Setiap iterasi yang diserahkan harus diasumsikan langsung dikompilasi lewat Arduino IDE/`arduino-cli compile --fqbn esp32:esp32:esp32c3`. Jangan menyerahkan potongan kode yang jelas belum lengkap (fungsi dipanggil tapi belum didefinisikan, header hilang) tanpa peringatan eksplisit bahwa build akan gagal dan alasannya.

## 9. Perbaikan Kecil → Full Fungsi
Jika perubahan hanya sedikit, kirim versi lengkap fungsi tersebut (`static`/global) beserta lokasi (nama fungsi yang diganti, baris kira-kira).

## 10. Perbaikan Besar → File Penuh
Jika perubahan signifikan (mis. refactor mutex, refactor duplikasi antrian), kirim keseluruhan blok kode terkait, bukan potongan parsial yang sulit ditempel manual.

## 11. Telusuri Semua Pemakaian Simbol
Jika sebuah nama fungsi/`#define`/struct diganti untuk mengatasi bug (mis. mengganti pendekatan `SAVE_QUEUE_FULL`, mengganti `NVS_KEY_*`), telusuri dan perbaiki **semua** titik pemakaian di seluruh file dalam balasan yang sama - jangan hanya memperbaiki definisi lalu meninggalkan pemanggil lama.

## 12. Verifikasi Berlapis Sebelum Menyatakan "Selesai"
Jangan menyatakan fitur/perbaikan "final"/"solid" hanya berdasarkan tinjauan statis. Nyatakan status jujur:
- Apakah sudah dikompilasi (`arduino-cli compile`/Arduino IDE) oleh pengguna?
- Apakah sudah diuji pada perangkat fisik (RFID, SD Card, WiFi, OTA)?
- Apakah ada race condition/mutex yang belum diverifikasi (lihat Gap #12)?
- Apa saja asumsi yang masih menunggu konfirmasi?

## 13. Target Akhir
Firmware stabil, tidak menimbulkan regresi pada mekanisme watchdog/deep sleep/antrian offline, dan setiap perbaikan keamanan tidak memutus kompatibilitas terhadap backend yang sudah berjalan.

## 14. Struktur Balasan Konsisten
1. Ringkasan singkat perubahan/penambahan.
2. Kode (sesuai poin 9/10).
3. Daftar fungsi/bagian file yang terdampak.
4. Status verifikasi (poin 12) - termasuk gap SRS mana yang tertutup dan mana yang masih terbuka.

## 15. Jangan Berasumsi Environment
Jika versi board ESP32 core, versi library (MFRC522, SdFat, ArduinoJson, dsb.), atau perilaku backend memengaruhi solusi, tanyakan dulu daripada menebak - kecuali sudah dinyatakan sebelumnya dalam sesi.

## 16. Perubahan Pin/Hardware Wajib Eksplisit
Jika sebuah perbaikan mengubah pemetaan pin (mis. memindahkan `PIN_BOOT` agar tidak bentrok dengan `PIN_OLED_SCL`), harus dinyatakan eksplisit sebagai perubahan hardware/wiring, bukan hanya perubahan software - karena berdampak ke perangkat fisik yang sudah terpasang di lapangan.

## 17. Kontrak Keamanan Mengikat
Setiap mekanisme kriptografi (kunci AES, salt, TLS) yang diubah harus dinyatakan dampaknya terhadap kompatibilitas data yang sudah tersimpan di perangkat existing (mis. kredensial NVS lama tidak bisa didekripsi jika skema kunci diganti). Penyimpangan dari ini dianggap bug kritis, bukan perbaikan kosmetik.

---

# Fitur/Gap yang ingin ditutup pada iterasi ini

Berdasarkan hasil review SRS terhadap source code (`ATTENDANCE MACHINE v2.3.1`), berikut daftar gap yang perlu ditindaklanjuti. Tandai setiap penyelesaian dengan nomor gap terkait pada komentar kode (`// TODO: GAP-01`, dst.) dan pada bagian "Status verifikasi" di balasan.

- [ ] **GAP-01** - Konflik pin `PIN_BOOT` (GPIO9) dengan `PIN_OLED_SCL` (GPIO9). Perlu keputusan: pindah pin fisik atau nonaktifkan salah satu fungsi selama pin dipakai bersama.
- [ ] **GAP-02** - Password AP provisioning `P@ssw0rd` hardcoded dan seragam di semua unit. Perlu mekanisme password unik per device (mis. turunan dari MAC) atau minimal parameter build-time yang tidak dikomit ke repo publik.
- [ ] **GAP-03** - `setInsecure()` pada seluruh koneksi HTTPS menonaktifkan verifikasi sertifikat TLS. Perlu keputusan: pin sertifikat/CA root backend atau terima risiko ini secara eksplisit dengan dokumentasi mitigasi.
- [ ] **GAP-04** - Kunci AES diturunkan dari MAC address (dapat diketahui publik) + salt tetap yang ada di source. Perlu evaluasi apakah tingkat proteksi ini cukup untuk kredensial WiFi/API key yang disimpan, atau perlu skema key management yang lebih kuat.
- [ ] **GAP-05** - AES-CBC tanpa autentikasi integritas (tidak ada HMAC/AEAD). Perlu keputusan: tambah HMAC-SHA256 atau migrasi ke AES-GCM.
- [ ] **GAP-06** - `uidToString()` untuk UID ≥4 byte hanya memakai 4 byte pertama, berisiko collision pada kartu ber-UID panjang (7 byte). Perlu keputusan apakah encoding RFID perlu diubah untuk menampung UID penuh.
- [ ] **GAP-07** - Perbandingan versi firmware OTA memakai `strcmp` leksikal, salah untuk kasus "2.10.0" vs "2.9.0". Perlu implementasi pembanding versi semantik (parse major.minor.patch).
- [ ] **GAP-08** - `http.getSize()` bisa bernilai -1 (chunked transfer), dikonversi ke `size_t` jadi nilai sangat besar dan dipakai di `Update.begin()`. Perlu validasi eksplisit sebelum `Update.begin()`.
- [ ] **GAP-09** - `saveToQueue()` hanya cek satu file berikutnya saat penuh, bisa melaporkan `SAVE_QUEUE_FULL` prematur. Perlu keputusan: perluas pencarian slot kosong atau terima batasan ini dengan dokumentasi.
- [ ] **GAP-10** - Logika duplikasi scan tersebar dan tidak konsisten antara jalur SD, NVS, dan direct HTTP. Perlu disatukan dalam satu fungsi pemeriksa duplikasi yang dipakai di semua jalur.
- [ ] **GAP-11** - Cache RFID RAM dibatasi 5000 entri tanpa peringatan jika database server lebih besar. Perlu logging/telemetri saat truncation terjadi.
- [ ] **GAP-12** - Akses ke `rtCfg`, `apiKey`, `wifiCreds`, `deviceId` lintas task tanpa mutex, berbeda dari SD/Display yang sudah dilindungi. Perlu evaluasi penambahan mutex atau justifikasi mengapa aman tanpa mutex.
- [ ] **GAP-13** - `Timers::lastFactoryCheck` adalah variabel mati (tidak dipakai). Perlu dihapus atau diimplementasikan sesuai maksud awal.
- [ ] **GAP-14** - Halaman provisioning `/save` dikirim lewat HTTP polos di AP lokal, kredensial transit sebagai plaintext. Perlu keputusan apakah risiko ini diterima (AP lokal terbatas) atau perlu HTTPS self-signed di web server provisioning.
- [ ] **GAP-15** - Parsing unduhan RFID DB per karakter dengan buffer baris tetap 32 byte tanpa logging saat data terpotong. Perlu tambah validasi/log kegagalan format baris.
- [ ] **GAP-16** - Seluruh device berbagi satu API key statis, dikombinasikan dengan TLS tanpa verifikasi (GAP-03). Perlu evaluasi apakah perlu API key per-device atau mekanisme device attestation.
- [ ] **GAP-17** - Tiga pemeriksaan debounce awal pada `checkFactoryReset()` tanpa `esp_task_wdt_reset()` eksplisit. Perlu ditambahkan sebagai praktik defensif meski durasi masih aman.
- [ ] **GAP-18** - Konfigurasi hasil `fetchRemoteConfig()` (jadwal sleep/dim, interval sync/OTA) hanya di RAM, hilang setelah restart/deep sleep. Perlu keputusan: persist ke NVS atau dokumentasikan sebagai perilaku yang disengaja (server harus resend tiap boot).

---

Lanjutkan/selesaikan implementasi proyek ini sesuai seluruh aturan di atas. Untuk setiap gap, jika penyelesaiannya memerlukan keputusan desain yang berdampak ke hardware (GAP-01), keamanan lapangan (GAP-02, GAP-04, GAP-16), atau kompatibilitas data existing (GAP-05 mengubah skema enkripsi), **tanyakan secara eksplisit sebelum menulis kode** - jangan menebak lalu menyerahkan perubahan yang berisiko memutus device yang sudah terpasang di lapangan.
