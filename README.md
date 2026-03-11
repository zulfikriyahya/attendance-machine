**Tambahan baru:**

**Pin & Konstanta**
- `PIN_FP_TX 20`, `PIN_FP_RX 21`, `PIN_FP_TOUCH 2`
- `FP_SCAN_TIMEOUT 3000UL`, `FP_ID_PREFIX "FP_"`, `FP_UID_LEN 10`

**Hardware Object**
- `HardwareSerial fpSerial(1)` pada UART1
- `Adafruit_Fingerprint finger(&fpSerial)`

**Global State**
- `bool fpAvailable = false`
- `timers.lastFpScan` ditambahkan ke struct `Timers`

**Fungsi baru**
- `initFingerprint()` — init UART1 57600 baud, verifikasi password sensor
- `fpIdToUid(uint16_t id, char *buf)` — konversi finger ID ke format string `FP_0000001` (10 karakter, sesuai panjang rfid field)
- `handleFingerprintScan()` — getImage → image2Tz → fingerSearch, lalu panggil `processPresensi()`
- `processPresensi(const char *uid)` — extracted shared handler yang dipakai baik oleh RFID maupun fingerprint, menghilangkan duplikasi kode

**Perubahan minor**
- `handleRFIDScan()` kini memanggil `processPresensi()` juga
- Standby display berubah dari `"TAP KARTU"` menjadi `"TAP KARTU/JARI"`
- `setup()` menambahkan init fingerprint dengan progress + info template count
- `loop()` menambahkan `handleFingerprintScan()` setelah cek RFID

**Wiring HLK-ZW101:**
| Sensor Pin | ESP32-C3 |
|---|---|
| PIN3 VCC | 3.3V |
| PIN6 GND | GND |
| PIN4 TX | GPIO21 (RX ESP) |
| PIN5 RX | GPIO20 (TX ESP) |
| PIN1 V_Touch | 3.3V |
| PIN2 TouchOut | GPIO2 (opsional, tidak digunakan dalam kode ini) |

> PIN2 TouchOut tidak digunakan karena `finger.getImage()` sudah mendeteksi sentuhan secara native melalui UART.
