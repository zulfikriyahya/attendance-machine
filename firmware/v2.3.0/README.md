**SISTEM PRESENSI RFID ESP32-C3 — IMPLEMENTASI PENUH v2.3.1**

Kamu adalah pakar C/C++/Arduino dan ESP32-C3. Ini adalah sesi lanjutan project komersial sistem presensi RFID berbasis ESP32-C3 Super Mini.

**KONTEKS PROJECT:**
Firmware bernama `presensi.ino` untuk device presensi RFID dengan hardware:
- ESP32-C3 Super Mini (single core, tanpa PSRAM)
- MFRC522 (RFID reader, SPI)
- SSD1306 128x64 OLED (I2C)
- SD Card (SPI, shared bus dengan RFID)
- Buzzer aktif
- PIN_BOOT untuk factory reset

Backend: REST API di `https://presensi.mtsn1pandeglang.sch.id` dengan header `X-API-KEY`.
Versi firmware saat ini: **v2.3.0** (sudah dianalisis, semua bug dan fix sudah diidentifikasi).

---

**SEMUA FIX YANG SUDAH DIIDENTIFIKASI (belum diimplementasikan ke kode):**

**FIX 1 — Heap Fragmentation:**
Ganti semua `DynamicJsonDocument` dengan `static StaticJsonDocument<4096> gJsonDoc` + `static char gJsonPayload[3584]` global. Fungsi response kecil pakai `StaticJsonDocument<256>` lokal. Ganti `String body = http.getString()` + `deserializeJson(doc, body)` dengan `deserializeJson(doc, *http.getStreamPtr())` di semua fungsi.

**FIX 2 — Task Stack Overflow:**
`TASK_SYNC_STACK` naik ke 10240. Di `taskSync` tambahkan `static uint8_t subPhase = 0`, rotasi 4 operasi besar via `switch(subPhase % 4)`. Tambahkan field `stack_rfid_min`, `stack_sync_min`, `stack_disp_min` ke payload `sendTelemetry`.

**FIX 3 — SPI Bus Contention:**
Tambahkan `SemaphoreHandle_t xSpiBusMutex`. Buat `acquireSPIForSD()` dan `releaseSPIFromSD()`. Ganti semua `selectSD()`/`deselectSD()` dengan pair baru ini. Di `loop()` bungkus RFID scan dalam `xSpiBusMutex`.

**FIX 4 — WDT Per-Task:**
Tambahkan `esp_task_wdt_add(nullptr)` di awal setiap task. Tambahkan `esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(0))` di `setup()`. Tambahkan `restoreWdtNormal()` di semua exit path `chunkedSync`. Tambahkan `RTC_DATA_ATTR uint32_t lastWdtResetReason` dan field `reset_reason` ke telemetry.

**FIX 5 — Race Condition Queue File:**
Di `chunkedSync`, skip file yang `syncState.currentFile == currentQueueFile` (sedang aktif ditulis).

**FIX 6 — Deep Sleep Race:**
Tambahkan `volatile bool deepSleepRequested` dan `volatile uint64_t deepSleepUs`. Pindahkan logika cek sleep schedule dari `loop()` ke `taskSync`. `loop()` hanya eksekusi sleep jika flag aktif.

**FIX 7 — Wake dari Deep Sleep:**
Validasi `sleepDurationSeconds <= 43200ULL` sebelum apply ke `lastValidTime`. Ubah `MAX_TIME_ESTIMATE_AGE` ke `7200UL`. Tambahkan guard waktu kadaluarsa di `kirimPresensi()`.

**FIX 8 — Brown-out Mid-Write:**
Di `saveToQueue`, bangun record lengkap di RAM dulu via `snprintf`, lalu satu kali `file.write()`. Di `readQueueFileLocked`, wajibkan CRC ada (`!c4 → continue`) dan selalu validasi CRC.

**FIX 9 — Compile Error ESP32 Core 3.x:**
- Hapus `#include <esp_efuse.h>`
- Di `deriveAesKey()`, ganti `esp_efuse_read_field_blob(ESP_EFUSE_MAC_FACTORY, mac, 48)` dengan `WiFi.macAddress(mac)`
- Di `getHttpClient()`, hapus `_httpClient.setFingerprint()`, ganti dengan `_httpClient.setInsecure()` (migrasi ke `setCACert()` di versi berikutnya)

---

**FILE PENDUKUNG YANG DIBUTUHKAN:**

`partitions.csv`:
```
# Name,   Type, SubType,  Offset,   Size,
nvs,      data, nvs,      0x9000,   0x8000,
otadata,  data, ota,      0x11000,  0x2000,
app0,     app,  ota_0,    0x20000,  0x1E0000,
app1,     app,  ota_1,    0x200000, 0x1E0000,
spiffs,   data, spiffs,   0x3E0000, 0x20000,
```

`sdkconfig.defaults`:
```
CONFIG_ESP_BROWNOUT_DET=y
CONFIG_ESP_BROWNOUT_DET_LVL_SEL_5=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=60
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_HEAP_CORRUPTION_DETECTION=n
CONFIG_ESP_MAIN_TASK_STACK_SIZE=4096
CONFIG_FREERTOS_USE_TRACE_FACILITY=n
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=n
```

`build_opt.h`:
```cpp
#define RELEASE_BUILD
// #define DEBUG_BUILD
```

---

**OUTPUT YANG DIBUTUHKAN SESI INI:**
1. `presensi.ino` final v2.3.1 — semua fix di atas sudah diimplementasikan, kode lengkap tidak ada placeholder
2. `partitions.csv`
3. `sdkconfig.defaults`
4. `build_opt.h`

**ATURAN OUTPUT:**
- Kode harus compile-ready di Arduino IDE dengan ESP32 Arduino core 3.x
- Tidak ada `// TODO`, tidak ada `// ...`, tidak ada potongan — semua fungsi lengkap
- Versi string firmware diubah menjadi `"2.3.1"`
- Changelog v2.3.1 ditambahkan di header komentar file
