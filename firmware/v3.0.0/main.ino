/*
 * ======================================================================================
 * SISTEM PRESENSI PINTAR (RFID + FINGERPRINT) - QUEUE SYSTEM v3.0.0
 * ======================================================================================
 * Device  : ESP32-C3 Super Mini
 * Author  : Yahya Zulfikri
 * Created : Juli 2025
 * Updated : Februari 2026
 * Version : 3.0.0
 *
 * CHANGELOG v3.0.0:
 * - TAMBAH: Fingerprint HLK-ZW101 sebagai alternatif RFID
 * - Logika identik: tap kartu ATAU tempel jari → data terkirim ke queue/API
 * - Tidak ada enroll di alat saat absensi; template sudah ada di ZW101
 * - Fingerprint ID dikirim sebagai "FP_XXXXX" agar kompatibel field rfid di DB
 *
 * WIRING HLK-ZW101:
 * +-----------+------------------+
 * | ZW101 Pin | ESP32-C3 Pin     |
 * +-----------+------------------+
 * | VCC       | 3.3V             |
 * | GND       | GND              |
 * | TX        | GPIO2  (FP_RX)   |
 * | RX        | GPIO21 (FP_TX)   |
 * | TOUCH     | GPIO20 (opsional)|
 * +-----------+------------------+
 *
 * CARA ENROLL TEMPLATE KE ZW101 (via Serial Monitor 115200 baud):
 *   ENROLL <id>   → contoh: ENROLL 1  (lalu ikuti instruksi)
 *   FPDEL <id>    → hapus template ID tertentu
 *   FPCOUNT       → lihat jumlah template tersimpan
 *   FPEMPTY       → hapus semua template
 *
 * ID yang dienroll ke ZW101 harus cocok dengan mapping di database server.
 * Saat absensi, alat hanya mengirim "FP_00001" ke API — server yang
 * mencocokkan ke data pegawai.
 * ======================================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>
#include <SdFat.h>

// ========================================
// PIN DEFINITIONS - EXISTING
// ========================================
#define PIN_SPI_SCK 4
#define PIN_SPI_MOSI 6
#define PIN_SPI_MISO 5
#define PIN_RFID_SS 7
#define PIN_RFID_RST 3
#define PIN_SD_CS 1
#define PIN_OLED_SDA 8
#define PIN_OLED_SCL 9
#define PIN_BUZZER 10
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DEBOUNCE_TIME 150

// ========================================
// PIN DEFINITIONS - HLK-ZW101
// ========================================
#define PIN_FP_RX 2     // ESP32 RX <- TX ZW101
#define PIN_FP_TX 21    // ESP32 TX -> RX ZW101
#define PIN_FP_TOUCH 20 // Sinyal jari (set -1 jika tidak pakai)
#define FP_BAUD_RATE 57600
#define FP_MAX_TEMPLATES 50
#define FP_TIMEOUT_MS 3000

// ========================================
// ZW101 PROTOCOL CONSTANTS
// ========================================
#define FP_CMD_TYPE 0x01
#define FP_ACK_TYPE 0x07
#define FP_VERIFYPASSWORD 0x13
#define FP_GETIMAGE 0x01
#define FP_IMAGE2TZ 0x02
#define FP_SEARCH 0x04
#define FP_REGMODEL 0x05
#define FP_STORE 0x06
#define FP_DELETCHAR 0x0C
#define FP_EMPTYLIBRARY 0x0D
#define FP_TEMPLETENUM 0x1D
#define FP_OK 0x00
#define FP_NOFINGER 0x02
#define FP_NOMATCH 0x08
#define FP_NOTFOUND 0x09
#define FP_ENROLLMISMATCH 0x0A
#define FP_BADLOCATION 0x0B
#define FP_TIMEOUT_ERR 0xFF
#define FP_PACKETRECIEVEERR 0x01

// ========================================
// NETWORK CONFIG
// ========================================
const char WIFI_SSID_1[] PROGMEM = "SSID_WIFI_1";
const char WIFI_SSID_2[] PROGMEM = "SSID_WIFI_2";
const char WIFI_PASSWORD_1[] PROGMEM = "PasswordWifi1";
const char WIFI_PASSWORD_2[] PROGMEM = "PasswordWifi2";
const char API_BASE_URL[] PROGMEM = "https://zedlabs.id";
const char API_SECRET_KEY[] PROGMEM = "SecretAPIToken";
const char NTP_SERVER_1[] PROGMEM = "pool.ntp.org";
const char NTP_SERVER_2[] PROGMEM = "time.google.com";
const char NTP_SERVER_3[] PROGMEM = "id.pool.ntp.org";

// ========================================
// QUEUE SYSTEM CONFIG
// ========================================
const int MAX_RECORDS_PER_FILE = 25;
const int MAX_QUEUE_FILES = 2000;
const unsigned long SYNC_INTERVAL = 300000;
const unsigned long MAX_OFFLINE_AGE = 2592000;
const unsigned long MIN_REPEAT_INTERVAL = 1800;
const unsigned long TIME_SYNC_INTERVAL = 3600000;
const unsigned long RECONNECT_INTERVAL = 300000;
const int SLEEP_START_HOUR = 18;
const int SLEEP_END_HOUR = 5;
const long GMT_OFFSET_SEC = 25200; // GMT+7

// OLED AUTO DIM
const int OLED_DIM_START_HOUR = 8;
const int OLED_DIM_END_HOUR = 14;

// OPTIMIZATION
const int MAX_SYNC_FILES_PER_CYCLE = 5;
const unsigned long MAX_SYNC_TIME = 15000;
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000;
const unsigned long PERIODIC_CHECK_INTERVAL = 1000;
const int MAX_DUPLICATE_CHECK_LINES = 100;
const unsigned long RECONNECT_TIMEOUT = 15000;

// ========================================
// RTC MEMORY
// ========================================
RTC_DATA_ATTR time_t lastValidTime = 0;
RTC_DATA_ATTR bool timeWasSynced = false;
RTC_DATA_ATTR unsigned long bootTime = 0;
RTC_DATA_ATTR int currentQueueFile = 0;

// ========================================
// HARDWARE OBJECTS
// ========================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 rfidReader(PIN_RFID_SS, PIN_RFID_RST);
SdFat sd;
FsFile file;
HardwareSerial fpSerial(1); // UART1 untuk ZW101

// ========================================
// GLOBAL VARIABLES
// ========================================
char lastUID[16] = "";
unsigned long lastScanTime = 0;
unsigned long lastSyncTime = 0;
unsigned long lastTimeSyncAttempt = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastPeriodicCheck = 0;
char messageBuffer[64];
bool isOnline = false;
bool sdCardAvailable = false;
bool fpAvailable = false;
String deviceId = "ESP32_";
bool oledIsOn = true;

// Enroll state (via Serial Monitor)
bool fpEnrollMode = false;
int fpEnrollID = 0;
int fpEnrollStep = 0;

struct DisplayState
{
  bool isOnline;
  char time[6];
  int pendingRecords;
  int wifiSignal;
  bool needsUpdate;
};
DisplayState currentDisplay = {false, "00:00", 0, 0, true};
DisplayState previousDisplay = {false, "00:00", 0, 0, false};

int syncCurrentFile = 0;
bool syncInProgress = false;
unsigned long syncStartTime = 0;

enum ReconnectState
{
  RECONNECT_IDLE,
  RECONNECT_INIT_SSID1,
  RECONNECT_TRYING_SSID1,
  RECONNECT_INIT_SSID2,
  RECONNECT_TRYING_SSID2,
  RECONNECT_SUCCESS,
  RECONNECT_FAILED
};
ReconnectState reconnectState = RECONNECT_IDLE;
unsigned long reconnectStartTime = 0;

volatile bool isWritingToQueue = false;
volatile bool isReadingQueue = false;

struct OfflineRecord
{
  String rfid;
  String timestamp;
  String deviceId;
  unsigned long unixTime;
};

// ========================================
// FORWARD DECLARATIONS
// ========================================
bool getTimeWithFallback(struct tm *timeInfo);
bool syncTimeWithFallback();
bool kirimPresensi(const char *uid, char *message);
void showOLED(const __FlashStringHelper *line1, const char *line2);
void showOLED(const __FlashStringHelper *line1, const __FlashStringHelper *line2);
void showProgress(const __FlashStringHelper *message, int durationMs);
void playToneSuccess();
void playToneError();
void playToneNotify();
void playStartupMelody();
void fatalError(const __FlashStringHelper *errorMessage);
void checkOLEDSchedule();
void turnOnOLED();
void turnOffOLED();
void chunkedSync();
bool isDuplicate(const char *rfid, unsigned long currentUnixTime);

// ========================================================================
//                    HLK-ZW101 PROTOCOL FUNCTIONS
// ========================================================================

void fpSendPacket(uint8_t type, const uint8_t *data, uint16_t len)
{
  uint16_t sum = type + (len + 2);
  fpSerial.write(0xEF);
  fpSerial.write(0x01);
  fpSerial.write((uint8_t)0xFF);
  fpSerial.write((uint8_t)0xFF);
  fpSerial.write((uint8_t)0xFF);
  fpSerial.write((uint8_t)0xFF);
  fpSerial.write(type);
  uint16_t pktLen = len + 2;
  fpSerial.write((uint8_t)(pktLen >> 8));
  fpSerial.write((uint8_t)(pktLen & 0xFF));
  for (uint16_t i = 0; i < len; i++)
  {
    fpSerial.write(data[i]);
    sum += data[i];
  }
  fpSerial.write((uint8_t)(sum >> 8));
  fpSerial.write((uint8_t)(sum & 0xFF));
}

uint8_t fpReceivePacket(uint8_t *buf, uint16_t *len, unsigned long timeout)
{
  unsigned long start = millis();
  uint8_t state = 0, idx = 0;
  uint16_t pktLen = 0;
  uint8_t rawBuf[64];
  *len = 0;
  while (millis() - start < timeout)
  {
    if (!fpSerial.available())
    {
      delay(2);
      continue;
    }
    uint8_t c = fpSerial.read();
    switch (state)
    {
    case 0:
      if (c == 0xEF)
        state = 1;
      break;
    case 1:
      state = (c == 0x01) ? 2 : 0;
      break;
    case 2:
    case 3:
    case 4:
    case 5:
      state++;
      break; // skip 4 addr bytes
    case 6:
      state = 7;
      break; // packet type
    case 7:
      pktLen = (uint16_t)c << 8;
      state = 8;
      break;
    case 8:
      pktLen |= c;
      idx = 0;
      state = 9;
      break;
    case 9:
      if (idx < sizeof(rawBuf))
        rawBuf[idx] = c;
      idx++;
      if (idx >= pktLen)
      {
        *len = pktLen - 2;
        for (uint16_t i = 0; i < *len && i < 32; i++)
          buf[i] = rawBuf[i];
        return (*len > 0) ? buf[0] : FP_PACKETRECIEVEERR;
      }
      break;
    }
  }
  return FP_TIMEOUT_ERR;
}

bool fpInit()
{
  fpSerial.begin(FP_BAUD_RATE, SERIAL_8N1, PIN_FP_RX, PIN_FP_TX);
  delay(200);
  while (fpSerial.available())
    fpSerial.read();
  uint8_t cmd[] = {FP_VERIFYPASSWORD, 0x00, 0x00, 0x00, 0x00};
  fpSendPacket(FP_CMD_TYPE, cmd, sizeof(cmd));
  uint8_t buf[8];
  uint16_t len;
  return (fpReceivePacket(buf, &len, 2000) == FP_OK);
}

uint8_t fpGetImage()
{
  uint8_t cmd[] = {FP_GETIMAGE};
  fpSendPacket(FP_CMD_TYPE, cmd, 1);
  uint8_t buf[8];
  uint16_t len;
  return fpReceivePacket(buf, &len, FP_TIMEOUT_MS);
}

uint8_t fpImage2Tz(uint8_t slot)
{
  uint8_t cmd[] = {FP_IMAGE2TZ, slot};
  fpSendPacket(FP_CMD_TYPE, cmd, 2);
  uint8_t buf[8];
  uint16_t len;
  return fpReceivePacket(buf, &len, FP_TIMEOUT_MS);
}

uint8_t fpRegModel()
{
  uint8_t cmd[] = {FP_REGMODEL};
  fpSendPacket(FP_CMD_TYPE, cmd, 1);
  uint8_t buf[8];
  uint16_t len;
  return fpReceivePacket(buf, &len, FP_TIMEOUT_MS);
}

uint8_t fpStore(uint8_t slot, uint16_t id)
{
  uint8_t cmd[] = {FP_STORE, slot, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF)};
  fpSendPacket(FP_CMD_TYPE, cmd, 4);
  uint8_t buf[8];
  uint16_t len;
  return fpReceivePacket(buf, &len, FP_TIMEOUT_MS);
}

uint8_t fpSearch(uint16_t *foundID, uint16_t *confidence)
{
  uint8_t cmd[] = {
      FP_SEARCH, 0x01, 0x00, 0x00,
      (uint8_t)(FP_MAX_TEMPLATES >> 8), (uint8_t)(FP_MAX_TEMPLATES & 0xFF)};
  fpSendPacket(FP_CMD_TYPE, cmd, 6);
  uint8_t buf[16];
  uint16_t len;
  uint8_t ret = fpReceivePacket(buf, &len, FP_TIMEOUT_MS);
  if (ret == FP_OK && len >= 5)
  {
    *foundID = ((uint16_t)buf[1] << 8) | buf[2];
    *confidence = ((uint16_t)buf[3] << 8) | buf[4];
  }
  return ret;
}

uint8_t fpDeleteModel(uint16_t id)
{
  uint8_t cmd[] = {FP_DELETCHAR, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF), 0x00, 0x01};
  fpSendPacket(FP_CMD_TYPE, cmd, 5);
  uint8_t buf[8];
  uint16_t len;
  return fpReceivePacket(buf, &len, FP_TIMEOUT_MS);
}

uint8_t fpEmptyDatabase()
{
  uint8_t cmd[] = {FP_EMPTYLIBRARY};
  fpSendPacket(FP_CMD_TYPE, cmd, 1);
  uint8_t buf[8];
  uint16_t len;
  return fpReceivePacket(buf, &len, FP_TIMEOUT_MS);
}

uint8_t fpGetTemplateCount(uint16_t *count)
{
  uint8_t cmd[] = {FP_TEMPLETENUM};
  fpSendPacket(FP_CMD_TYPE, cmd, 1);
  uint8_t buf[16];
  uint16_t len;
  uint8_t ret = fpReceivePacket(buf, &len, FP_TIMEOUT_MS);
  if (ret == FP_OK && len >= 3)
    *count = ((uint16_t)buf[1] << 8) | buf[2];
  return ret;
}

// FP ID integer → string "FP_00001" (kompatibel field rfid di DB)
void fpIDtoString(uint16_t id, char *output)
{
  snprintf(output, 16, "FP_%05u", id);
}

// ========================================================================
//  FINGERPRINT SCAN — alur identik dengan RFID, tidak ada bedanya
// ========================================================================
void handleFingerprintScan()
{
  uint8_t ret = fpGetImage();
  if (ret == FP_NOFINGER)
    return; // Tidak ada jari, lanjut loop

  // Ada jari — nyalakan OLED sementara jika sedang dim
  bool wasOff = !oledIsOn;
  if (wasOff)
    turnOnOLED();

  if (ret != FP_OK)
  {
    // Gagal ambil gambar (jari lepas terlalu cepat), abaikan
    if (wasOff)
      checkOLEDSchedule();
    return;
  }

  showOLED(F("SIDIK JARI"), "MEMPROSES...");

  // Ekstrak fitur ke buffer 1
  ret = fpImage2Tz(1);
  if (ret != FP_OK)
  {
    showOLED(F("SIDIK JARI"), "GAMBAR BURUK");
    playToneError();
    delay(1200);
    if (wasOff)
      checkOLEDSchedule();
    return;
  }

  // Cari di library ZW101
  uint16_t foundID = 0, confidence = 0;
  ret = fpSearch(&foundID, &confidence);

  if (ret == FP_NOTFOUND || ret == FP_NOMATCH)
  {
    showOLED(F("SIDIK JARI"), "TIDAK DIKENAL");
    playToneError();
    delay(1500);
    if (wasOff)
      checkOLEDSchedule();
    return;
  }

  if (ret != FP_OK)
  {
    showOLED(F("SIDIK JARI"), "ERROR");
    playToneError();
    delay(1000);
    if (wasOff)
      checkOLEDSchedule();
    return;
  }

  // Cocok — buat UID string
  char fpUID[16];
  fpIDtoString(foundID, fpUID);

  // Debounce: cegah scan ganda saat jari masih menempel
  if (strcmp(fpUID, lastUID) == 0 && millis() - lastScanTime < (DEBOUNCE_TIME * 20))
  {
    if (wasOff)
      checkOLEDSchedule();
    return;
  }

  strcpy(lastUID, fpUID);
  lastScanTime = millis();

  // Tampilkan ID yang terdeteksi (sama seperti RFID menampilkan UID)
  showOLED(F("SIDIK JARI"), fpUID);
  playToneNotify();

  // Kirim ke queue / API — IDENTIK dengan alur RFID
  char message[32];
  bool success = kirimPresensi(fpUID, message);

  showOLED(success ? F("BERHASIL") : F("INFO"), message);
  success ? playToneSuccess() : playToneError();
  delay(200);

  if (wasOff)
    checkOLEDSchedule();
}

// ========================================================================
//  ENROLL VIA SERIAL MONITOR (hanya untuk setup awal template ke ZW101)
// ========================================================================
void handleFingerprintEnroll()
{
  if (fpEnrollStep == 0)
  {
    showOLED(F("ENROLL FP"), "TEMPEL JARI 1");
    fpEnrollStep = 1;
    return;
  }

  if (fpEnrollStep == 1)
  {
    uint8_t ret = fpGetImage();
    if (ret == FP_NOFINGER)
      return;
    if (ret != FP_OK)
    {
      showOLED(F("ENROLL"), "SCAN 1 GAGAL");
      playToneError();
      delay(800);
      showOLED(F("ENROLL FP"), "TEMPEL JARI 1");
      return;
    }
    ret = fpImage2Tz(1);
    if (ret != FP_OK)
    {
      showOLED(F("ENROLL"), "GAMBAR BURUK");
      playToneError();
      delay(800);
      showOLED(F("ENROLL FP"), "TEMPEL JARI 1");
      return;
    }
    playToneNotify();
    showOLED(F("ENROLL FP"), "ANGKAT JARI...");
    delay(1500);
    unsigned long t = millis();
    while (fpGetImage() != FP_NOFINGER && millis() - t < 5000)
      delay(100);
    showOLED(F("ENROLL FP"), "TEMPEL JARI 2");
    fpEnrollStep = 2;
    return;
  }

  if (fpEnrollStep == 2)
  {
    uint8_t ret = fpGetImage();
    if (ret == FP_NOFINGER)
      return;
    if (ret != FP_OK)
    {
      showOLED(F("ENROLL"), "SCAN 2 GAGAL");
      playToneError();
      delay(800);
      showOLED(F("ENROLL FP"), "TEMPEL JARI 2");
      return;
    }
    ret = fpImage2Tz(2);
    if (ret != FP_OK)
    {
      showOLED(F("ENROLL"), "GAMBAR BURUK");
      playToneError();
      delay(800);
      showOLED(F("ENROLL FP"), "TEMPEL JARI 2");
      return;
    }

    ret = fpRegModel();
    if (ret == FP_ENROLLMISMATCH)
    {
      showOLED(F("ENROLL GAGAL"), "JARI BERBEDA!");
      playToneError();
      delay(2000);
      fpEnrollStep = 0;
      showOLED(F("ENROLL FP"), "TEMPEL JARI 1");
      return;
    }

    ret = fpStore(1, (uint16_t)fpEnrollID);
    if (ret == FP_OK)
    {
      snprintf(messageBuffer, sizeof(messageBuffer), "ID %d TERSIMPAN", fpEnrollID);
      showOLED(F("ENROLL OK!"), messageBuffer);
      Serial.printf("[FP] Enroll berhasil! ID %d → dikirim sebagai FP_%05d\n", fpEnrollID, fpEnrollID);
      playToneSuccess();
      delay(2000);
    }
    else
    {
      showOLED(F("ENROLL GAGAL"), "SIMPAN ERROR");
      playToneError();
      delay(1500);
    }
    fpEnrollMode = false;
    fpEnrollStep = 0;
    currentDisplay.needsUpdate = true;
  }
}

void checkSerialCommands()
{
  if (!Serial.available())
    return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.startsWith("ENROLL "))
  {
    int id = cmd.substring(7).toInt();
    if (id < 1 || id > FP_MAX_TEMPLATES)
    {
      Serial.printf("[CMD] ID harus 1-%d\n", FP_MAX_TEMPLATES);
      return;
    }
    if (!fpAvailable)
    {
      Serial.println(F("[CMD] ZW101 tidak terdeteksi!"));
      return;
    }
    fpEnrollID = id;
    fpEnrollMode = true;
    fpEnrollStep = 0;
    Serial.printf("[CMD] Enroll ID %d → akan dikirim sebagai FP_%05d\n", id, id);
    showOLED(F("ENROLL FP"), "SIAPKAN JARI");
    delay(1000);
  }
  else if (cmd == "FPCOUNT")
  {
    if (!fpAvailable)
    {
      Serial.println(F("[CMD] ZW101 tidak terdeteksi!"));
      return;
    }
    uint16_t count = 0;
    if (fpGetTemplateCount(&count) == FP_OK)
    {
      Serial.printf("[FP] Template: %u / %d\n", count, FP_MAX_TEMPLATES);
      snprintf(messageBuffer, sizeof(messageBuffer), "%u/%d TEMPLATE", count, FP_MAX_TEMPLATES);
      showOLED(F("FP COUNT"), messageBuffer);
      delay(2000);
    }
  }
  else if (cmd.startsWith("FPDEL "))
  {
    int id = cmd.substring(6).toInt();
    if (id < 1 || id > FP_MAX_TEMPLATES)
    {
      Serial.printf("[CMD] ID harus 1-%d\n", FP_MAX_TEMPLATES);
      return;
    }
    if (!fpAvailable)
    {
      Serial.println(F("[CMD] ZW101 tidak terdeteksi!"));
      return;
    }
    if (fpDeleteModel((uint16_t)id) == FP_OK)
    {
      snprintf(messageBuffer, sizeof(messageBuffer), "ID %d DIHAPUS", id);
      showOLED(F("FP DELETE"), messageBuffer);
      Serial.printf("[FP] Hapus ID %d OK\n", id);
    }
    else
    {
      showOLED(F("FP DELETE"), "GAGAL");
      Serial.printf("[FP] Hapus ID %d GAGAL\n", id);
    }
    delay(1500);
  }
  else if (cmd == "FPEMPTY")
  {
    if (!fpAvailable)
    {
      Serial.println(F("[CMD] ZW101 tidak terdeteksi!"));
      return;
    }
    if (fpEmptyDatabase() == FP_OK)
    {
      showOLED(F("FP EMPTY"), "SEMUA DIHAPUS");
      Serial.println(F("[FP] Semua template dihapus."));
    }
    else
    {
      showOLED(F("FP EMPTY"), "GAGAL");
    }
    delay(1500);
  }
  else if (cmd.length() > 0)
  {
    Serial.println(F("[CMD] Perintah: ENROLL <id> | FPCOUNT | FPDEL <id> | FPEMPTY"));
  }
}

// ========================================================================
//                    EXISTING FUNCTIONS (TIDAK BERUBAH)
// ========================================================================

void turnOffOLED()
{
  if (oledIsOn)
  {
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    oledIsOn = false;
  }
}
void turnOnOLED()
{
  if (!oledIsOn)
  {
    display.ssd1306_command(SSD1306_DISPLAYON);
    oledIsOn = true;
    currentDisplay.needsUpdate = true;
  }
}
void checkOLEDSchedule()
{
  struct tm timeInfo;
  if (!getTimeWithFallback(&timeInfo))
    return;
  int h = timeInfo.tm_hour;
  if (h >= OLED_DIM_START_HOUR && h < OLED_DIM_END_HOUR)
    turnOffOLED();
  else
    turnOnOLED();
}

inline void selectSD()
{
  digitalWrite(PIN_RFID_SS, HIGH);
  digitalWrite(PIN_SD_CS, LOW);
}
inline void deselectSD() { digitalWrite(PIN_SD_CS, HIGH); }

String getQueueFileName(int index)
{
  char filename[20];
  snprintf(filename, sizeof(filename), "/queue_%d.csv", index);
  return String(filename);
}

int countRecordsInFile(const String &filename)
{
  if (!sdCardAvailable)
    return 0;
  selectSD();
  if (!file.open(filename.c_str(), O_RDONLY))
  {
    deselectSD();
    return 0;
  }
  int count = 0;
  char line[128];
  if (file.available())
    file.fgets(line, sizeof(line));
  while (file.fgets(line, sizeof(line)) > 0)
  {
    if (strlen(line) > 10)
      count++;
  }
  file.close();
  deselectSD();
  return count;
}

int countAllOfflineRecords()
{
  if (!sdCardAvailable)
    return 0;
  int total = 0;
  selectSD();
  for (int i = 0; i < MAX_QUEUE_FILES; i++)
  {
    String fn = getQueueFileName(i);
    if (sd.exists(fn.c_str()) && file.open(fn.c_str(), O_RDONLY))
    {
      char line[128];
      if (file.available())
        file.fgets(line, sizeof(line));
      while (file.fgets(line, sizeof(line)) > 0)
      {
        if (strlen(line) > 10)
          total++;
      }
      file.close();
    }
  }
  deselectSD();
  return total;
}

bool initSDCard()
{
  pinMode(PIN_SD_CS, OUTPUT);
  pinMode(PIN_RFID_SS, OUTPUT);
  deselectSD();
  digitalWrite(PIN_RFID_SS, HIGH);
  selectSD();
  delay(10);
  if (!sd.begin(PIN_SD_CS, SD_SCK_MHZ(10)))
  {
    deselectSD();
    return false;
  }
  currentQueueFile = -1;
  for (int i = 0; i < MAX_QUEUE_FILES; i++)
  {
    String fn = getQueueFileName(i);
    if (!sd.exists(fn.c_str()))
    {
      if (file.open(fn.c_str(), O_WRONLY | O_CREAT))
      {
        file.println("rfid,timestamp,device_id,unix_time");
        file.close();
        currentQueueFile = i;
        break;
      }
    }
    else
    {
      int count = 0;
      if (file.open(fn.c_str(), O_RDONLY))
      {
        char line[128];
        if (file.available())
          file.fgets(line, sizeof(line));
        while (file.fgets(line, sizeof(line)) > 0)
        {
          if (strlen(line) > 10)
            count++;
        }
        file.close();
      }
      if (count < MAX_RECORDS_PER_FILE)
      {
        currentQueueFile = i;
        break;
      }
    }
  }
  deselectSD();
  if (currentQueueFile == -1)
    currentQueueFile = 0;
  return true;
}

bool isDuplicate(const char *rfid, unsigned long currentUnixTime)
{
  if (!sdCardAvailable)
    return false;
  while (isReadingQueue)
    delay(10);
  isReadingQueue = true;
  selectSD();
  bool found = false;
  for (int offset = 0; offset <= 1; offset++)
  {
    int fileIdx = (currentQueueFile - offset + MAX_QUEUE_FILES) % MAX_QUEUE_FILES;
    String fn = getQueueFileName(fileIdx);
    if (!sd.exists(fn.c_str()) || !file.open(fn.c_str(), O_RDONLY))
      continue;
    char line[128];
    if (file.available())
      file.fgets(line, sizeof(line));
    int linesRead = 0;
    while (file.fgets(line, sizeof(line)) > 0 && linesRead < MAX_DUPLICATE_CHECK_LINES)
    {
      String s = String(line);
      s.trim();
      int c1 = s.indexOf(','), c3 = s.lastIndexOf(',');
      if (c1 > 0 && c3 > 0 && s.substring(0, c1).equals(rfid) && currentUnixTime - s.substring(c3 + 1).toInt() < MIN_REPEAT_INTERVAL)
      {
        found = true;
        break;
      }
      linesRead++;
    }
    file.close();
    if (found)
      break;
  }
  deselectSD();
  isReadingQueue = false;
  return found;
}

bool saveToQueue(const char *rfid, const char *timestamp, unsigned long unixTime)
{
  if (!sdCardAvailable)
    return false;
  while (isWritingToQueue || isReadingQueue)
    delay(10);
  isWritingToQueue = true;
  if (isDuplicate(rfid, unixTime))
  {
    isWritingToQueue = false;
    return false;
  }
  selectSD();
  if (currentQueueFile < 0 || currentQueueFile >= MAX_QUEUE_FILES)
    currentQueueFile = 0;
  String cf = getQueueFileName(currentQueueFile);
  if (!sd.exists(cf.c_str()))
  {
    if (file.open(cf.c_str(), O_WRONLY | O_CREAT))
    {
      file.println("rfid,timestamp,device_id,unix_time");
      file.close();
    }
  }
  int cnt = 0;
  if (file.open(cf.c_str(), O_RDONLY))
  {
    char line[128];
    if (file.available())
      file.fgets(line, sizeof(line));
    while (file.fgets(line, sizeof(line)) > 0)
    {
      if (strlen(line) > 10)
        cnt++;
    }
    file.close();
  }
  if (cnt >= MAX_RECORDS_PER_FILE)
  {
    currentQueueFile = (currentQueueFile + 1) % MAX_QUEUE_FILES;
    cf = getQueueFileName(currentQueueFile);
    if (sd.exists(cf.c_str()))
      sd.remove(cf.c_str());
    if (!file.open(cf.c_str(), O_WRONLY | O_CREAT))
    {
      deselectSD();
      isWritingToQueue = false;
      return false;
    }
    file.println("rfid,timestamp,device_id,unix_time");
    file.close();
  }
  if (!file.open(cf.c_str(), O_WRONLY | O_APPEND))
  {
    deselectSD();
    isWritingToQueue = false;
    return false;
  }
  file.print(rfid);
  file.print(",");
  file.print(timestamp);
  file.print(",");
  file.print(deviceId);
  file.print(",");
  file.println(unixTime);
  file.sync();
  file.close();
  deselectSD();
  isWritingToQueue = false;
  return true;
}

bool readQueueFile(const String &filename, OfflineRecord *records, int *count, int maxCount)
{
  if (!sdCardAvailable)
    return false;
  selectSD();
  if (!file.open(filename.c_str(), O_RDONLY))
  {
    deselectSD();
    return false;
  }
  *count = 0;
  time_t now = time(nullptr);
  char line[128];
  if (file.available())
    file.fgets(line, sizeof(line));
  while (file.available() && *count < maxCount)
  {
    if (file.fgets(line, sizeof(line)) > 0)
    {
      String s = String(line);
      s.trim();
      if (s.length() < 10)
        continue;
      int c1 = s.indexOf(','), c2 = s.indexOf(',', c1 + 1), c3 = s.indexOf(',', c2 + 1);
      if (c1 > 0 && c2 > 0 && c3 > 0)
      {
        records[*count].rfid = s.substring(0, c1);
        records[*count].timestamp = s.substring(c1 + 1, c2);
        records[*count].deviceId = s.substring(c2 + 1, c3);
        records[*count].unixTime = s.substring(c3 + 1).toInt();
        if (now - records[*count].unixTime <= MAX_OFFLINE_AGE)
          (*count)++;
      }
    }
  }
  file.close();
  deselectSD();
  return *count > 0;
}

bool syncQueueFile(const String &filename)
{
  if (!sdCardAvailable || WiFi.status() != WL_CONNECTED)
    return false;
  OfflineRecord records[MAX_RECORDS_PER_FILE];
  int validCount = 0;
  if (!readQueueFile(filename, records, &validCount, MAX_RECORDS_PER_FILE) || validCount == 0)
  {
    selectSD();
    sd.remove(filename.c_str());
    deselectSD();
    return true;
  }
  HTTPClient http;
  http.setTimeout(30000);
  http.setConnectTimeout(10000);
  char url[80];
  strcpy_P(url, API_BASE_URL);
  strcat_P(url, PSTR("/api/presensi/sync-bulk"));
  if (!http.begin(url))
    return false;
  http.addHeader(F("Content-Type"), F("application/json"));
  char apiKey[32];
  strcpy_P(apiKey, API_SECRET_KEY);
  http.addHeader(F("X-API-KEY"), apiKey);
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("data");
  for (int i = 0; i < validCount; i++)
  {
    JsonObject obj = arr.createNestedObject();
    obj["rfid"] = records[i].rfid;
    obj["timestamp"] = records[i].timestamp;
    obj["device_id"] = records[i].deviceId;
    obj["sync_mode"] = true;
  }
  String payload;
  serializeJson(doc, payload);
  int code = http.POST(payload);
  http.end();
  if (code == 200)
  {
    selectSD();
    sd.remove(filename.c_str());
    deselectSD();
    return true;
  }
  return false;
}

void chunkedSync()
{
  if (!sdCardAvailable || WiFi.status() != WL_CONNECTED)
  {
    syncInProgress = false;
    return;
  }
  if (!syncInProgress)
  {
    syncInProgress = true;
    syncCurrentFile = 0;
    syncStartTime = millis();
  }
  int synced = 0;
  while (syncCurrentFile < MAX_QUEUE_FILES && synced < MAX_SYNC_FILES_PER_CYCLE && millis() - syncStartTime < MAX_SYNC_TIME)
  {
    String fn = getQueueFileName(syncCurrentFile);
    selectSD();
    bool ex = sd.exists(fn.c_str());
    deselectSD();
    if (ex)
    {
      if (countRecordsInFile(fn) > 0)
      {
        if (syncQueueFile(fn))
          synced++;
        else if (WiFi.status() != WL_CONNECTED)
        {
          syncInProgress = false;
          return;
        }
      }
      else
      {
        selectSD();
        sd.remove(fn.c_str());
        deselectSD();
      }
    }
    syncCurrentFile++;
    yield();
  }
  if (syncCurrentFile >= MAX_QUEUE_FILES)
  {
    syncInProgress = false;
    syncCurrentFile = 0;
    if (synced > 0 && countAllOfflineRecords() == 0)
    {
      showOLED(F("SYNC"), "SELESAI!");
      playToneSuccess();
      delay(500);
    }
  }
}

bool syncTimeWithFallback()
{
  const char *servers[] = {NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3};
  for (int i = 0; i < 3; i++)
  {
    char s[32];
    strcpy_P(s, servers[i]);
    configTime(GMT_OFFSET_SEC, 0, s);
    struct tm ti;
    unsigned long t = millis();
    while (millis() - t < 2500)
    {
      if (getLocalTime(&ti) && ti.tm_year >= 120)
      {
        lastValidTime = mktime(&ti);
        timeWasSynced = true;
        bootTime = millis();
        snprintf(messageBuffer, sizeof(messageBuffer), "%02d:%02d", ti.tm_hour, ti.tm_min);
        showOLED(F("WAKTU TERSYNC"), messageBuffer);
        delay(1000);
        return true;
      }
      delay(100);
    }
  }
  return false;
}

bool getTimeWithFallback(struct tm *timeInfo)
{
  if (getLocalTime(timeInfo) && timeInfo->tm_year >= 120)
    return true;
  if (timeWasSynced && lastValidTime > 0)
  {
    time_t est = lastValidTime + ((millis() - bootTime) / 1000);
    *timeInfo = *localtime(&est);
    return true;
  }
  return false;
}

void periodicTimeSync()
{
  if (millis() - lastTimeSyncAttempt < TIME_SYNC_INTERVAL)
    return;
  lastTimeSyncAttempt = millis();
  if (WiFi.status() == WL_CONNECTED)
    syncTimeWithFallback();
}

String getFormattedTimestamp()
{
  struct tm ti;
  if (getTimeWithFallback(&ti))
  {
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
    return String(buf);
  }
  return "";
}

bool connectToWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setSleep(WIFI_PS_MAX_MODEM);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  for (int attempt = 0; attempt < 2; attempt++)
  {
    char ssid[16], password[16];
    strcpy_P(ssid, attempt == 0 ? WIFI_SSID_1 : WIFI_SSID_2);
    strcpy_P(password, attempt == 0 ? WIFI_PASSWORD_1 : WIFI_PASSWORD_2);
    WiFi.begin(ssid, password);
    for (int retry = 0; retry < 20 && WiFi.status() != WL_CONNECTED; retry++)
    {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor((SCREEN_WIDTH - strlen(ssid) * 6) / 2, 10);
      display.println(ssid);
      display.setCursor(35, 30);
      display.print(F("CONNECTING"));
      for (int j = 0; j < (retry % 4); j++)
        display.print('.');
      display.display();
      delay(300);
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      snprintf(messageBuffer, sizeof(messageBuffer), "RSSI: %ld dBm", WiFi.RSSI());
      showOLED(F("WIFI OK"), messageBuffer);
      isOnline = true;
      delay(1500);
      return true;
    }
  }
  isOnline = false;
  return false;
}

bool pingAPI()
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  HTTPClient http;
  char url[80];
  strcpy_P(url, API_BASE_URL);
  strcat_P(url, PSTR("/api/presensi/ping"));
  http.begin(url);
  char apiKey[32];
  strcpy_P(apiKey, API_SECRET_KEY);
  http.addHeader(F("X-API-KEY"), apiKey);
  int code = http.GET();
  http.end();
  isOnline = (code == 200);
  return isOnline;
}

void processReconnect()
{
  switch (reconnectState)
  {
  case RECONNECT_IDLE:
    if (WiFi.status() != WL_CONNECTED && millis() - lastReconnectAttempt >= RECONNECT_INTERVAL)
    {
      lastReconnectAttempt = millis();
      reconnectState = RECONNECT_INIT_SSID1;
    }
    break;
  case RECONNECT_INIT_SSID1:
    WiFi.disconnect(true);
    delay(100);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setSleep(WIFI_PS_MAX_MODEM);
    {
      char s[16], p[16];
      strcpy_P(s, WIFI_SSID_1);
      strcpy_P(p, WIFI_PASSWORD_1);
      WiFi.begin(s, p);
    }
    reconnectStartTime = millis();
    reconnectState = RECONNECT_TRYING_SSID1;
    break;
  case RECONNECT_TRYING_SSID1:
    if (WiFi.status() == WL_CONNECTED)
      reconnectState = RECONNECT_SUCCESS;
    else if (millis() - reconnectStartTime >= RECONNECT_TIMEOUT)
      reconnectState = RECONNECT_INIT_SSID2;
    break;
  case RECONNECT_INIT_SSID2:
    WiFi.disconnect(true);
    delay(100);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setSleep(WIFI_PS_MAX_MODEM);
    {
      char s[16], p[16];
      strcpy_P(s, WIFI_SSID_2);
      strcpy_P(p, WIFI_PASSWORD_2);
      WiFi.begin(s, p);
    }
    reconnectStartTime = millis();
    reconnectState = RECONNECT_TRYING_SSID2;
    break;
  case RECONNECT_TRYING_SSID2:
    if (WiFi.status() == WL_CONNECTED)
      reconnectState = RECONNECT_SUCCESS;
    else if (millis() - reconnectStartTime >= RECONNECT_TIMEOUT)
      reconnectState = RECONNECT_FAILED;
    break;
  case RECONNECT_SUCCESS:
    isOnline = true;
    syncTimeWithFallback();
    if (sdCardAvailable)
    {
      int p = countAllOfflineRecords();
      if (p > 0)
      {
        snprintf(messageBuffer, sizeof(messageBuffer), "%d RECORDS", p);
        showOLED(F("SYNCING"), messageBuffer);
        syncInProgress = false;
        syncCurrentFile = 0;
        lastSyncTime = millis();
        chunkedSync();
      }
    }
    reconnectState = RECONNECT_IDLE;
    break;
  case RECONNECT_FAILED:
    isOnline = false;
    reconnectState = RECONNECT_IDLE;
    break;
  }
}

void uidToString(uint8_t *uid, uint8_t length, char *output)
{
  if (length >= 4)
  {
    uint32_t v = ((uint32_t)uid[3] << 24) | ((uint32_t)uid[2] << 16) | ((uint32_t)uid[1] << 8) | uid[0];
    sprintf(output, "%010lu", v);
  }
  else
  {
    sprintf(output, "%02X%02X", uid[0], uid[1]);
  }
}

bool kirimPresensi(const char *rfidUID, char *message)
{
  String timestamp = getFormattedTimestamp();
  time_t now = time(nullptr);
  if (sdCardAvailable)
  {
    if (isDuplicate(rfidUID, now))
    {
      strcpy(message, "CUKUP SEKALI!");
      return false;
    }
    if (saveToQueue(rfidUID, timestamp.c_str(), now))
    {
      strcpy(message, "DATA TERSIMPAN");
      return true;
    }
    strcpy(message, "SD CARD ERROR");
    return false;
  }
  strcpy(message, "NO WIFI & SD");
  return false;
}

void showOLED(const __FlashStringHelper *line1, const char *line2)
{
  if (!oledIsOn)
    return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  int16_t x, y;
  uint16_t w, h;
  display.getTextBounds(line1, 0, 0, &x, &y, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 10);
  display.println(line1);
  display.getTextBounds(line2, 0, 0, &x, &y, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 30);
  display.println(line2);
  display.display();
}
void showOLED(const __FlashStringHelper *line1, const __FlashStringHelper *line2)
{
  char buf[32];
  strncpy_P(buf, (const char *)line2, 31);
  showOLED(line1, buf);
}
void showProgress(const __FlashStringHelper *message, int durationMs)
{
  if (!oledIsOn)
    return;
  const int step = 8, pw = 80;
  int d = durationMs / (pw / step);
  int sx = (SCREEN_WIDTH - pw) / 2;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  int16_t x, y;
  uint16_t tw, h;
  display.getTextBounds(message, 0, 0, &x, &y, &tw, &h);
  display.setCursor((SCREEN_WIDTH - tw) / 2, 20);
  display.println(message);
  display.display();
  for (int i = 0; i <= pw; i += step)
  {
    display.fillRect(sx, 40, i, 4, WHITE);
    display.display();
    delay(d);
  }
  delay(300);
}

void showStartupAnimation()
{
  display.clearDisplay();
  display.setTextColor(WHITE);
  const char title[] PROGMEM = "ZEDLABS";
  const char subtitle1[] PROGMEM = "INNOVATE BEYOND";
  const char subtitle2[] PROGMEM = "LIMITS";
  const char version[] PROGMEM = "v3.0.0";
  int titleX = (SCREEN_WIDTH - 7 * 12) / 2, sub1X = (SCREEN_WIDTH - 15 * 6) / 2, sub2X = (SCREEN_WIDTH - 6 * 6) / 2, verX = (SCREEN_WIDTH - 6 * 6) / 2;
  for (int x = -80; x <= titleX; x += 4)
  {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(x, 5);
    display.println((__FlashStringHelper *)title);
    display.setTextSize(1);
    display.setCursor(sub1X, 30);
    display.println((__FlashStringHelper *)subtitle1);
    display.setCursor(sub2X, 40);
    display.println((__FlashStringHelper *)subtitle2);
    display.display();
    delay(30);
  }
  delay(300);
  display.setTextSize(1);
  display.setCursor(verX, 55);
  display.print((__FlashStringHelper *)version);
  display.display();
  for (int i = 0; i < 3; i++)
  {
    delay(300);
    display.print('.');
    display.display();
  }
  delay(500);
}

void updateCurrentDisplayState()
{
  currentDisplay.isOnline = (WiFi.status() == WL_CONNECTED);
  struct tm ti;
  if (getTimeWithFallback(&ti))
    snprintf(currentDisplay.time, sizeof(currentDisplay.time), "%02d:%02d", ti.tm_hour, ti.tm_min);
  currentDisplay.pendingRecords = sdCardAvailable ? countAllOfflineRecords() : 0;
  if (WiFi.status() == WL_CONNECTED)
  {
    long r = WiFi.RSSI();
    currentDisplay.wifiSignal = (r > -67) ? 4 : (r > -70) ? 3
                                            : (r > -80)   ? 2
                                            : (r > -90)   ? 1
                                                          : 0;
  }
  else
    currentDisplay.wifiSignal = 0;
}

void updateStandbySignal()
{
  if (!oledIsOn)
    return;
  if (memcmp(&currentDisplay, &previousDisplay, sizeof(DisplayState)) != 0)
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(2, 2);
    if (fpEnrollMode)
    {
      display.print(F("ENROLL FP"));
    }
    else if (reconnectState == RECONNECT_INIT_SSID1 || reconnectState == RECONNECT_TRYING_SSID1)
    {
      display.print(F("CONN SSID1"));
      for (int i = 0; i < (millis() / 500) % 4; i++)
        display.print('.');
    }
    else if (reconnectState == RECONNECT_INIT_SSID2 || reconnectState == RECONNECT_TRYING_SSID2)
    {
      display.print(F("CONN SSID2"));
      for (int i = 0; i < (millis() / 500) % 4; i++)
        display.print('.');
    }
    else if (syncInProgress)
    {
      display.print(F("SYNCING"));
      for (int i = 0; i < (millis() / 500) % 4; i++)
        display.print('.');
    }
    else
    {
      display.print(currentDisplay.isOnline ? F("ONLINE") : F("OFFLINE"));
    }

    // Petunjuk penggunaan (otomatis sesuai hardware yang terdeteksi)
    const char *hint = fpAvailable ? "KARTU / JARI" : "TAP KARTU";
    int16_t x1, y1;
    uint16_t w1, h1;
    display.getTextBounds(hint, 0, 0, &x1, &y1, &w1, &h1);
    display.setCursor((SCREEN_WIDTH - w1) / 2, 20);
    display.print(hint);
    display.getTextBounds(currentDisplay.time, 0, 0, &x1, &y1, &w1, &h1);
    display.setCursor((SCREEN_WIDTH - w1) / 2, 35);
    display.print(currentDisplay.time);
    if (currentDisplay.pendingRecords > 0)
    {
      snprintf(messageBuffer, sizeof(messageBuffer), "Q:%d", currentDisplay.pendingRecords);
      display.getTextBounds(messageBuffer, 0, 0, &x1, &y1, &w1, &h1);
      display.setCursor((SCREEN_WIDTH - w1) / 2, 50);
      display.print(messageBuffer);
    }
    if (currentDisplay.wifiSignal > 0)
    {
      for (int i = 0; i < 4; i++)
      {
        int h = 2 + i * 2, x = SCREEN_WIDTH - 18 + i * 5;
        if (i < currentDisplay.wifiSignal)
          display.fillRect(x, 10 - h, 3, h, WHITE);
        else
          display.drawRect(x, 10 - h, 3, h, WHITE);
      }
    }
    display.display();
    memcpy(&previousDisplay, &currentDisplay, sizeof(DisplayState));
  }
}

void playToneSuccess()
{
  for (int i = 0; i < 2; i++)
  {
    tone(PIN_BUZZER, 3000, 100);
    delay(150);
  }
  noTone(PIN_BUZZER);
}
void playToneError()
{
  for (int i = 0; i < 3; i++)
  {
    tone(PIN_BUZZER, 3000, 150);
    delay(200);
  }
  noTone(PIN_BUZZER);
}
void playToneNotify()
{
  tone(PIN_BUZZER, 3000, 100);
  delay(120);
  noTone(PIN_BUZZER);
}
void playStartupMelody()
{
  const int melody[] PROGMEM = {2500, 3000, 2500, 3000};
  for (int i = 0; i < 4; i++)
  {
    tone(PIN_BUZZER, pgm_read_word_near(melody + i), 100);
    delay(150);
  }
  noTone(PIN_BUZZER);
}
void fatalError(const __FlashStringHelper *errorMessage)
{
  char buf[32];
  strncpy_P(buf, (const char *)errorMessage, 31);
  buf[31] = '\0';
  showOLED((const __FlashStringHelper *)buf, "RESTART...");
  playToneError();
  delay(3000);
  ESP.restart();
}

void handleRFIDScan()
{
  deselectSD();
  digitalWrite(PIN_RFID_SS, LOW);
  char rfidBuffer[11];
  uidToString(rfidReader.uid.uidByte, rfidReader.uid.size, rfidBuffer);
  if (strcmp(rfidBuffer, lastUID) == 0 && millis() - lastScanTime < DEBOUNCE_TIME)
  {
    rfidReader.PICC_HaltA();
    rfidReader.PCD_StopCrypto1();
    digitalWrite(PIN_RFID_SS, HIGH);
    return;
  }
  strcpy(lastUID, rfidBuffer);
  lastScanTime = millis();
  bool wasOff = !oledIsOn;
  if (wasOff)
    turnOnOLED();
  showOLED(F("RFID"), rfidBuffer);
  playToneNotify();
  char message[32];
  bool success = kirimPresensi(rfidBuffer, message);
  showOLED(success ? F("BERHASIL") : F("INFO"), message);
  success ? playToneSuccess() : playToneError();
  delay(200);
  if (wasOff)
    checkOLEDSchedule();
  rfidReader.PICC_HaltA();
  rfidReader.PCD_StopCrypto1();
  digitalWrite(PIN_RFID_SS, HIGH);
}

// ========================================================================
//                              SETUP
// ========================================================================
void setup()
{
  Serial.begin(115200);
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  pinMode(PIN_BUZZER, OUTPUT);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  showStartupAnimation();
  playStartupMelody();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  deviceId = "ESP32_" + String(mac[4], HEX) + String(mac[5], HEX);
  deviceId.toUpperCase();

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  // ---- SD Card ----
  showProgress(F("INIT SD CARD"), 1500);
  sdCardAvailable = initSDCard();
  if (sdCardAvailable)
  {
    showOLED(F("SD CARD"), "TERSEDIA");
    playToneSuccess();
    delay(800);
    int p = countAllOfflineRecords();
    if (p > 0)
    {
      snprintf(messageBuffer, sizeof(messageBuffer), "%d TERSISA", p);
      showOLED(F("DATA OFFLINE"), messageBuffer);
      delay(1000);
    }
  }
  else
  {
    showOLED(F("SD CARD"), "TIDAK ADA");
    playToneError();
    delay(1000);
  }

  // ---- HLK-ZW101 ----
  showProgress(F("INIT FINGERPRINT"), 1500);
  if (PIN_FP_TOUCH >= 0)
    pinMode(PIN_FP_TOUCH, INPUT);
  fpAvailable = fpInit();
  if (fpAvailable)
  {
    uint16_t fpCount = 0;
    fpGetTemplateCount(&fpCount);
    snprintf(messageBuffer, sizeof(messageBuffer), "%u TEMPLATE", fpCount);
    showOLED(F("FP ZW101 OK"), messageBuffer);
    playToneSuccess();
    Serial.printf("[FP] ZW101 OK — %u template tersimpan\n", fpCount);
  }
  else
  {
    showOLED(F("FP ZW101"), "TIDAK ADA");
    playToneError();
    Serial.println(F("[FP] ZW101 tidak terdeteksi. Cek wiring!"));
  }
  delay(1000);

  // ---- WiFi ----
  showProgress(F("CONNECTING WIFI"), 1500);
  if (!connectToWiFi())
  {
    fatalError(F("NO WIFI"));
  }
  else
  {
    showProgress(F("PING API"), 1000);
    int retry = 0;
    while (!pingAPI() && retry < 3)
    {
      retry++;
      snprintf(messageBuffer, sizeof(messageBuffer), "Retry %d/3", retry);
      showOLED(F("API GAGAL"), messageBuffer);
      playToneError();
      delay(1000);
    }
    if (isOnline)
    {
      showOLED(F("API OK"), "SYNCING TIME");
      playToneSuccess();
      delay(500);
      syncTimeWithFallback();
      if (sdCardAvailable)
      {
        int p = countAllOfflineRecords();
        if (p > 0)
        {
          snprintf(messageBuffer, sizeof(messageBuffer), "%d records", p);
          showOLED(F("SYNC DATA"), messageBuffer);
          delay(1000);
          chunkedSync();
        }
      }
    }
    else
    {
      fatalError(F("API GAGAL"));
    }
  }

  // ---- RFID RC522 ----
  showProgress(F("INIT RFID"), 1000);
  rfidReader.PCD_Init();
  delay(100);
  digitalWrite(PIN_RFID_SS, HIGH);
  byte ver = rfidReader.PCD_ReadRegister(rfidReader.VersionReg);
  if (ver == 0x00 || ver == 0xFF)
  {
    fatalError(F("RC522 GAGAL"));
  }

  showOLED(F("SISTEM SIAP"), isOnline ? "ONLINE" : "OFFLINE");
  playToneSuccess();

  Serial.println(F("\n=== SISTEM PRESENSI v3.0.0 ==="));
  Serial.printf("RFID RC522 : OK\n");
  Serial.printf("FP ZW101   : %s\n", fpAvailable ? "OK" : "Tidak Terdeteksi");
  Serial.printf("WiFi       : %s | SD Card: %s\n", isOnline ? "Online" : "Offline", sdCardAvailable ? "OK" : "Tidak Ada");
  Serial.println(F("Enroll template: ketik ENROLL <id> di Serial Monitor"));
  Serial.println(F("===============================\n"));

  bootTime = lastSyncTime = lastTimeSyncAttempt = lastReconnectAttempt = lastDisplayUpdate = lastPeriodicCheck = millis();
  delay(1000);
  checkOLEDSchedule();
}

// ========================================================================
//                              MAIN LOOP
// ========================================================================
void loop()
{
  unsigned long now = millis();

  checkOLEDSchedule();
  checkSerialCommands();

  // ── ENROLL MODE (tidak proses scan biasa) ─────────────────────────────
  if (fpEnrollMode && fpAvailable)
  {
    handleFingerprintEnroll();
    return;
  }

  // ── RFID SCAN ─────────────────────────────────────────────────────────
  if (rfidReader.PICC_IsNewCardPresent() && rfidReader.PICC_ReadCardSerial())
  {
    handleRFIDScan();
    return;
  }

  // ── FINGERPRINT SCAN ──────────────────────────────────────────────────
  // Jika pakai TOUCH pin: hanya scan saat pin HIGH (efisien).
  // Jika tidak pakai TOUCH pin (PIN_FP_TOUCH = -1): polling terus.
  if (fpAvailable)
  {
    bool tryFP = (PIN_FP_TOUCH < 0) ? true : (digitalRead(PIN_FP_TOUCH) == HIGH);
    if (tryFP)
      handleFingerprintScan();
  }

  // ── RECONNECT & SYNC ──────────────────────────────────────────────────
  processReconnect();

  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL)
  {
    lastDisplayUpdate = now;
    updateCurrentDisplayState();
    updateStandbySignal();
  }

  if (now - lastPeriodicCheck >= PERIODIC_CHECK_INTERVAL)
  {
    lastPeriodicCheck = now;
    if (WiFi.status() == WL_CONNECTED && sdCardAvailable)
    {
      if (syncInProgress)
        chunkedSync();
      else if (now - lastSyncTime >= SYNC_INTERVAL)
      {
        lastSyncTime = now;
        if (countAllOfflineRecords() > 0)
          chunkedSync();
      }
    }
    periodicTimeSync();
  }

  // ── DEEP SLEEP ────────────────────────────────────────────────────────
  struct tm ti;
  if (getTimeWithFallback(&ti))
  {
    int h = ti.tm_hour;
    if (h >= SLEEP_START_HOUR || h < SLEEP_END_HOUR)
    {
      showOLED(F("SLEEP MODE"), "...");
      delay(1000);
      int sleepSeconds;
      if (h >= SLEEP_START_HOUR)
        sleepSeconds = ((24 - h) * 3600) + (SLEEP_END_HOUR * 3600) - (ti.tm_min * 60 + ti.tm_sec);
      else
        sleepSeconds = ((SLEEP_END_HOUR - h) * 3600) - (ti.tm_min * 60 + ti.tm_sec);
      if (sleepSeconds < 60)
        sleepSeconds = 60;
      if (sleepSeconds > 43200)
        sleepSeconds = 43200;
      snprintf(messageBuffer, sizeof(messageBuffer), "%d Jam %d Menit", sleepSeconds / 3600, (sleepSeconds % 3600) / 60);
      showOLED(F("SLEEP FOR"), messageBuffer);
      delay(2000);
      display.clearDisplay();
      display.display();
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
      esp_deep_sleep_start();
    }
  }
  yield();
}
