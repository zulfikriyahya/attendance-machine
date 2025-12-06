/*
 * ======================================================================================
 * IDENTITAS PROYEK: SISTEM PRESENSI PINTAR (RFID) - QUEUE SYSTEM
 * ======================================================================================
 * Perangkat    : ESP32-C3 Super Mini
 * Penulis      : Yahya Zulfikri
 * Dibuat       : Juli 2025
 * Diperbarui   : Desember 2025
 * Versi        : 2.1.0 (Queue System - 50 Records per Batch)
 *
 * FITUR BARU v2.1.0:
 * - Queue System: Multi-file CSV (queue_0.csv, queue_1.csv, dst)
 * - Maksimal 50 records per file untuk mencegah kehabisan memori
 * - Auto-rotate ke file baru setelah 50 records
 * - Sync per-file untuk efisiensi memori
 * - Hapus file otomatis setelah sync berhasil
 * - Total capacity: 5000+ records (100 files x 50 records)
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
#include <SD.h>
#include <FS.h>

// ========================================
// DEFINISI PIN
// ========================================
// SPI Bus (Shared antara RFID & SD Card)
#define PIN_SPI_SCK         4
#define PIN_SPI_MOSI        6
#define PIN_SPI_MISO        5

// RFID RC522
#define PIN_RFID_SS         7   // Chip Select RFID
#define PIN_RFID_RST        3   // Reset RFID

// SD Card
#define PIN_SD_CS           1   // Chip Select SD Card (BERBEDA dari RFID!)

// OLED SSD1306
#define PIN_OLED_SDA        8
#define PIN_OLED_SCL        9

// BUZZER
#define PIN_BUZZER          10

#define SCREEN_WIDTH        128
#define SCREEN_HEIGHT       64
#define DEBOUNCE_TIME       300

// ========================================
// KONFIGURASI JARINGAN
// ========================================
const char WIFI_SSID_1[] PROGMEM        = "PRESENSI";
const char WIFI_SSID_2[] PROGMEM        = "ZEDLABS";
const char WIFI_PASSWORD_1[] PROGMEM    = "P@ssw0rd";
const char WIFI_PASSWORD_2[] PROGMEM    = "P@ssw0rd";
const char API_BASE_URL[] PROGMEM       = "http://192.168.250.72:8000";
const char API_SECRET_KEY[] PROGMEM     = "P@ndegl@ng_14012000*";

const char NTP_SERVER_1[] PROGMEM       = "pool.ntp.org";
const char NTP_SERVER_2[] PROGMEM       = "time.google.com";
const char NTP_SERVER_3[] PROGMEM       = "id.pool.ntp.org";
const char NTP_SERVER_4[] PROGMEM       = "time.nist.gov";
const char NTP_SERVER_5[] PROGMEM       = "time.cloudflare.com";

// ========================================
// KONSTANTA SISTEM QUEUE
// ========================================
const int WIFI_RETRY_COUNT          = 2;
const int NTP_SERVER_COUNT          = 5;
const int NTP_TIMEOUT_MS            = 8000;
const int NTP_MAX_RETRIES           = 2;

// QUEUE SYSTEM CONFIGURATION
const int MAX_RECORDS_PER_FILE      = 50;   // 50 records per file
const int MAX_QUEUE_FILES           = 100;   // Maksimal 100 files (total 5000 records)
const unsigned long SYNC_INTERVAL   = 60000; // 60 detik
const unsigned long MAX_OFFLINE_AGE = 3600;  // 1 jam

const int SLEEP_START_HOUR          = 18;
const int SLEEP_END_HOUR            = 1;
const long GMT_OFFSET_SEC           = 25200;
const int DAYLIGHT_OFFSET_SEC       = 0;

// ========================================
// VARIABEL MEMORI RTC
// ========================================
RTC_DATA_ATTR time_t lastValidTime = 0;
RTC_DATA_ATTR bool timeWasSynced = false;
RTC_DATA_ATTR unsigned long bootTime = 0;
RTC_DATA_ATTR int currentQueueFile = 0; // File queue aktif

// ========================================
// OBJEK PERANGKAT KERAS
// ========================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 rfidReader(PIN_RFID_SS, PIN_RFID_RST);

// ========================================
// VARIABEL GLOBAL
// ========================================
char lastUID[11] = "";
unsigned long lastScanTime = 0;
unsigned long lastSyncTime = 0;
char messageBuffer[64];
bool isOnline = false;
bool sdCardAvailable = false;
String deviceId = "ESP32_";

// ========================================
// STRUKTUR DATA OFFLINE
// ========================================
struct OfflineRecord {
  String rfid;
  String timestamp;
  String deviceId;
  unsigned long unixTime;
};

// ========================================
// DEKLARASI FUNGSI
// ========================================
void showOLED(const __FlashStringHelper *line1, const char *line2);
void showOLED(const __FlashStringHelper *line1, const __FlashStringHelper *line2);
void showProgress(const __FlashStringHelper *message, int durationMs);
void showStandbySignal();

// ========================================
// FUNGSI QUEUE SYSTEM
// ========================================

/**
 * Generate nama file queue berdasarkan index
 */
String getQueueFileName(int index) {
  char filename[20];
  snprintf(filename, sizeof(filename), "/queue_%d.csv", index);
  return String(filename);
}

/**
 * Inisialisasi SD Card dan queue system
 */
bool initSDCard() {
  // Set CS pins
  pinMode(PIN_SD_CS, OUTPUT);
  pinMode(PIN_RFID_SS, OUTPUT);
  
  // Deselect both devices
  digitalWrite(PIN_SD_CS, HIGH);
  digitalWrite(PIN_RFID_SS, HIGH);
  
  // Initialize SPI bus
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
  
  // Select SD Card
  digitalWrite(PIN_SD_CS, LOW);
  delay(10);
  
  if (!SD.begin(PIN_SD_CS, SPI, 4000000)) {
    digitalWrite(PIN_SD_CS, HIGH);
    return false;
  }
  
  digitalWrite(PIN_SD_CS, HIGH);
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    return false;
  }
  
  // Cari file queue aktif yang belum penuh
  currentQueueFile = -1;
  for (int i = 0; i < MAX_QUEUE_FILES; i++) {
    String filename = getQueueFileName(i);
    if (!SD.exists(filename)) {
      // Buat file baru
      File file = SD.open(filename, FILE_WRITE);
      if (file) {
        file.println("rfid,timestamp,device_id,unix_time");
        file.close();
        currentQueueFile = i;
        break;
      }
    } else {
      // Cek apakah file masih ada space
      int count = countRecordsInFile(filename);
      if (count < MAX_RECORDS_PER_FILE) {
        currentQueueFile = i;
        break;
      }
    }
  }
  
  // Jika semua file penuh, gunakan file 0 (overwrite)
  if (currentQueueFile == -1) {
    currentQueueFile = 0;
  }
  
  return true;
}

/**
 * Hitung jumlah record dalam satu file
 */
int countRecordsInFile(const String& filename) {
  if (!sdCardAvailable) return 0;
  
  digitalWrite(PIN_RFID_SS, HIGH); // Deselect RFID
  digitalWrite(PIN_SD_CS, LOW);    // Select SD Card
  
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    digitalWrite(PIN_SD_CS, HIGH);
    return 0;
  }
  
  int count = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() > 10) count++;
  }
  file.close();
  
  digitalWrite(PIN_SD_CS, HIGH); // Deselect SD Card
  
  return count > 0 ? count - 1 : 0; // -1 untuk header
}

/**
 * Hitung total semua records di semua queue files
 */
int countAllOfflineRecords() {
  if (!sdCardAvailable) return 0;
  
  int total = 0;
  for (int i = 0; i < MAX_QUEUE_FILES; i++) {
    String filename = getQueueFileName(i);
    if (SD.exists(filename)) {
      total += countRecordsInFile(filename);
    }
  }
  return total;
}

/**
 * Cek duplikat RFID dalam semua queue files
 */
bool isDuplicateInAllQueues(const char* rfid, unsigned long currentUnixTime) {
  if (!sdCardAvailable) return false;
  
  digitalWrite(PIN_RFID_SS, HIGH); // Deselect RFID
  
  for (int i = 0; i < MAX_QUEUE_FILES; i++) {
    String filename = getQueueFileName(i);
    if (!SD.exists(filename)) continue;
    
    digitalWrite(PIN_SD_CS, LOW); // Select SD Card
    File file = SD.open(filename, FILE_READ);
    if (!file) {
      digitalWrite(PIN_SD_CS, HIGH);
      continue;
    }
    
    // Skip header
    if (file.available()) {
      file.readStringUntil('\n');
    }
    
    // Cek setiap baris
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      
      if (line.length() < 10) continue;
      
      int firstComma = line.indexOf(',');
      int thirdComma = line.lastIndexOf(',');
      
      if (firstComma > 0 && thirdComma > 0) {
        String fileRfid = line.substring(0, firstComma);
        unsigned long fileUnixTime = line.substring(thirdComma + 1).toInt();
        
        if (fileRfid.equals(rfid)) {
          unsigned long timeDiff = currentUnixTime - fileUnixTime;
          if (timeDiff < MAX_OFFLINE_AGE) {
            file.close();
            digitalWrite(PIN_SD_CS, HIGH);
            return true; // Duplikat ditemukan
          }
        }
      }
    }
    
    file.close();
    digitalWrite(PIN_SD_CS, HIGH);
  }
  
  return false;
}

/**
 * Simpan data ke queue file aktif
 */
bool saveToQueue(const char* rfid, const char* timestamp, unsigned long unixTime) {
  if (!sdCardAvailable) return false;
  
  // Validasi duplikat
  if (isDuplicateInAllQueues(rfid, unixTime)) {
    return false;
  }
  
  digitalWrite(PIN_RFID_SS, HIGH); // Deselect RFID
  digitalWrite(PIN_SD_CS, LOW);    // Select SD Card
  
  // Cek apakah file saat ini sudah penuh
  String currentFile = getQueueFileName(currentQueueFile);
  int currentCount = countRecordsInFile(currentFile);
  
  if (currentCount >= MAX_RECORDS_PER_FILE) {
    // Cari file baru
    currentQueueFile = (currentQueueFile + 1) % MAX_QUEUE_FILES;
    currentFile = getQueueFileName(currentQueueFile);
    
    // Hapus file lama jika ada (untuk overwrite)
    if (SD.exists(currentFile)) {
      SD.remove(currentFile);
    }
    
    // Buat file baru
    File file = SD.open(currentFile, FILE_WRITE);
    if (!file) {
      digitalWrite(PIN_SD_CS, HIGH);
      return false;
    }
    file.println("rfid,timestamp,device_id,unix_time");
    file.close();
  }
  
  // Append data
  File file = SD.open(currentFile, FILE_APPEND);
  if (!file) {
    digitalWrite(PIN_SD_CS, HIGH);
    return false;
  }
  
  file.print(rfid);
  file.print(",");
  file.print(timestamp);
  file.print(",");
  file.print(deviceId);
  file.print(",");
  file.println(unixTime);
  
  file.close();
  digitalWrite(PIN_SD_CS, HIGH); // Deselect SD Card
  
  return true;
}

/**
 * Baca records dari satu file queue
 */
bool readQueueFile(const String& filename, OfflineRecord* records, int* count, int maxCount) {
  if (!sdCardAvailable) return false;
  
  digitalWrite(PIN_RFID_SS, HIGH); // Deselect RFID
  digitalWrite(PIN_SD_CS, LOW);    // Select SD Card
  
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    digitalWrite(PIN_SD_CS, HIGH);
    return false;
  }
  
  *count = 0;
  time_t currentTime = time(nullptr);
  
  // Skip header
  if (file.available()) {
    file.readStringUntil('\n');
  }
  
  while (file.available() && *count < maxCount) {
    String line = file.readStringUntil('\n');
    line.trim();
    
    if (line.length() < 10) continue;
    
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);
    int thirdComma = line.indexOf(',', secondComma + 1);
    
    if (firstComma > 0 && secondComma > 0 && thirdComma > 0) {
      records[*count].rfid = line.substring(0, firstComma);
      records[*count].timestamp = line.substring(firstComma + 1, secondComma);
      records[*count].deviceId = line.substring(secondComma + 1, thirdComma);
      records[*count].unixTime = line.substring(thirdComma + 1).toInt();
      
      // Hanya ambil data yang masih valid (< 1 jam)
      if (currentTime - records[*count].unixTime <= MAX_OFFLINE_AGE) {
        (*count)++;
      }
    }
  }
  
  file.close();
  digitalWrite(PIN_SD_CS, HIGH); // Deselect SD Card
  
  return *count > 0;
}

/**
 * Sync satu file queue ke server
 */
bool syncQueueFile(const String& filename) {
  if (!sdCardAvailable) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  
  OfflineRecord records[MAX_RECORDS_PER_FILE];
  int validCount = 0;
  
  if (!readQueueFile(filename, records, &validCount, MAX_RECORDS_PER_FILE)) {
    // File kosong atau error, hapus saja
    digitalWrite(PIN_RFID_SS, HIGH);
    digitalWrite(PIN_SD_CS, LOW);
    SD.remove(filename);
    digitalWrite(PIN_SD_CS, HIGH);
    return true;
  }
  
  if (validCount == 0) {
    digitalWrite(PIN_RFID_SS, HIGH);
    digitalWrite(PIN_SD_CS, LOW);
    SD.remove(filename);
    digitalWrite(PIN_SD_CS, HIGH);
    return true;
  }
  
  HTTPClient http;
  http.setTimeout(30000);
  
  char url[80];
  strcpy_P(url, API_BASE_URL);
  strcat_P(url, PSTR("/api/presensi/sync-bulk"));
  
  http.begin(url);
  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("Accept"), F("application/json"));
  
  char apiKey[32];
  strcpy_P(apiKey, API_SECRET_KEY);
  http.addHeader(F("X-API-KEY"), apiKey);
  
  DynamicJsonDocument doc(4096);
  JsonArray dataArray = doc.createNestedArray("data");
  
  for (int i = 0; i < validCount; i++) {
    JsonObject obj = dataArray.createNestedObject();
    obj["rfid"] = records[i].rfid;
    obj["timestamp"] = records[i].timestamp;
    obj["device_id"] = records[i].deviceId;
    obj["sync_mode"] = true;
  }
  
  String payload;
  serializeJson(doc, payload);
  
  int responseCode = http.POST(payload);
  http.end();
  
  if (responseCode == 200) {
    // Sync berhasil, hapus file
    digitalWrite(PIN_RFID_SS, HIGH);
    digitalWrite(PIN_SD_CS, LOW);
    SD.remove(filename);
    digitalWrite(PIN_SD_CS, HIGH);
    return true;
  }
  
  return false;
}

/**
 * Sync semua queue files (satu per satu)
 */
bool syncAllQueues() {
  if (!sdCardAvailable) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  
  int totalSynced = 0;
  int totalFailed = 0;
  
  for (int i = 0; i < MAX_QUEUE_FILES; i++) {
    String filename = getQueueFileName(i);
    if (!SD.exists(filename)) continue;
    
    int fileRecords = countRecordsInFile(filename);
    if (fileRecords == 0) {
      SD.remove(filename);
      continue;
    }
    
    snprintf(messageBuffer, sizeof(messageBuffer), "File %d (%d rec)", i, fileRecords);
    showOLED(F("SYNCING"), messageBuffer);
    
    if (syncQueueFile(filename)) {
      totalSynced += fileRecords;
      playToneSuccess();
    } else {
      totalFailed++;
      playToneError();
    }
    
    delay(500); // Delay antar file untuk stabilitas
  }
  
  if (totalSynced > 0) {
    snprintf(messageBuffer, sizeof(messageBuffer), "%d records", totalSynced);
    showOLED(F("SYNC SUCCESS"), messageBuffer);
    delay(1500);
  }
  
  return totalSynced > 0;
}

// ========================================
// FUNGSI PERIODIC SYNC
// ========================================

void periodicSync() {
  if (!sdCardAvailable) return;
  
  unsigned long currentTime = millis();
  if (currentTime - lastSyncTime < SYNC_INTERVAL) {
    return;
  }
  
  lastSyncTime = currentTime;
  
  if (WiFi.status() == WL_CONNECTED) {
    int pendingCount = countAllOfflineRecords();
    if (pendingCount > 0) {
      syncAllQueues();
    }
  }
}

// ========================================
// FUNGSI MANAJEMEN WAKTU
// ========================================

bool syncTimeWithFallback() {
  for (int serverIndex = 0; serverIndex < NTP_SERVER_COUNT; serverIndex++) {
    char ntpServer[32];

    switch (serverIndex) {
      case 0: strcpy_P(ntpServer, NTP_SERVER_1); break;
      case 1: strcpy_P(ntpServer, NTP_SERVER_2); break;
      case 2: strcpy_P(ntpServer, NTP_SERVER_3); break;
      case 3: strcpy_P(ntpServer, NTP_SERVER_4); break;
      case 4: strcpy_P(ntpServer, NTP_SERVER_5); break;
    }

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, ntpServer);

    for (int retry = 0; retry < NTP_MAX_RETRIES; retry++) {
      snprintf_P(messageBuffer, sizeof(messageBuffer), PSTR("Server %d/%d"),
                 serverIndex + 1, NTP_SERVER_COUNT);
      showOLED(F("SYNC WAKTU"), messageBuffer);

      struct tm timeInfo;
      unsigned long startTime = millis();
      bool timeReceived = false;

      while ((millis() - startTime) < NTP_TIMEOUT_MS) {
        if (getLocalTime(&timeInfo)) {
          if (timeInfo.tm_year >= 120) {
            timeReceived = true;
            break;
          }
        }
        delay(100);
      }

      if (timeReceived) {
        lastValidTime = mktime(&timeInfo);
        timeWasSynced = true;
        bootTime = millis();

        snprintf(messageBuffer, sizeof(messageBuffer), "%02d:%02d",
                 timeInfo.tm_hour, timeInfo.tm_min);
        showOLED(F("WAKTU TERSYNC"), messageBuffer);
        delay(1500);
        return true;
      }

      delay(500);
    }
  }

  return false;
}

bool getTimeWithFallback(struct tm *timeInfo) {
  if (getLocalTime(timeInfo)) {
    if (timeInfo->tm_year >= 120) {
      return true;
    }
  }

  if (timeWasSynced && lastValidTime > 0) {
    unsigned long elapsedSeconds = (millis() - bootTime) / 1000;
    time_t estimatedTime = lastValidTime + elapsedSeconds;
    *timeInfo = *localtime(&estimatedTime);
    return true;
  }

  return false;
}

inline void periodicTimeSync() {
  static unsigned long lastSyncAttempt = 0;
  const unsigned long SYNC_INTERVAL_MS = 3600000UL;

  if (millis() - lastSyncAttempt > SYNC_INTERVAL_MS) {
    lastSyncAttempt = millis();
    if (WiFi.status() == WL_CONNECTED) {
      syncTimeWithFallback();
    }
  }
}

String getFormattedTimestamp() {
  struct tm timeInfo;
  if (getTimeWithFallback(&timeInfo)) {
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return String(buffer);
  }
  return "";
}

// ========================================
// FUNGSI JARINGAN
// ========================================

bool connectToWiFi() {
  WiFi.mode(WIFI_STA);

  for (int attempt = 0; attempt < WIFI_RETRY_COUNT; attempt++) {
    char ssid[16], password[16];

    if (attempt == 0) {
      strcpy_P(ssid, WIFI_SSID_1);
      strcpy_P(password, WIFI_PASSWORD_1);
    } else {
      strcpy_P(ssid, WIFI_SSID_2);
      strcpy_P(password, WIFI_PASSWORD_2);
    }

    WiFi.begin(ssid, password);

    for (int retry = 0; retry < 20 && WiFi.status() != WL_CONNECTED; retry++) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);

      display.setCursor((SCREEN_WIDTH - strlen(ssid) * 6) / 2, 10);
      display.println(ssid);

      const char loadingText[] = "MENGHUBUNGKAN";
      int dotCount = retry % 4;
      display.setCursor((SCREEN_WIDTH - strlen(loadingText) * 6) / 2, 30);
      display.print(loadingText);
      for (int j = 0; j < dotCount; j++) {
        display.print('.');
      }

      display.display();
      delay(300);
    }

    if (WiFi.status() == WL_CONNECTED) {
      WiFi.localIP().toString().toCharArray(messageBuffer, sizeof(messageBuffer));
      showOLED(F("WIFI TERHUBUNG"), messageBuffer);
      isOnline = true;
      delay(2000);
      return true;
    }
  }

  isOnline = false;
  return false;
}

bool pingAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    isOnline = false;
    return false;
  }

  HTTPClient http;
  http.setTimeout(10000);

  char url[80];
  strcpy_P(url, API_BASE_URL);
  strcat_P(url, PSTR("/api/presensi/ping"));

  http.begin(url);

  char apiKey[32];
  strcpy_P(apiKey, API_SECRET_KEY);
  http.addHeader(F("X-API-KEY"), apiKey);

  int responseCode = http.GET();
  http.end();

  isOnline = (responseCode == 200);
  return isOnline;
}

// ========================================
// FUNGSI RFID
// ========================================

void uidToString(uint8_t *uid, uint8_t length, char *output) {
  if (length >= 4) {
    uint32_t value = ((uint32_t)uid[3] << 24) |
                     ((uint32_t)uid[2] << 16) |
                     ((uint32_t)uid[1] << 8) |
                     uid[0];
    sprintf(output, "%010lu", value);
  } else {
    sprintf(output, "%02X%02X", uid[0], uid[1]);
  }
}

bool kirimPresensi(const char *rfidUID, char *message, char *name,
                   char *status, char *timeStr) {
  String timestamp = getFormattedTimestamp();
  time_t currentUnixTime = time(nullptr);
  
  if (WiFi.status() == WL_CONNECTED && isOnline) {
    HTTPClient http;
    http.setTimeout(30000);

    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/rfid"));

    http.begin(url);
    http.addHeader(F("Content-Type"), F("application/json"));
    http.addHeader(F("Accept"), F("application/json"));

    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    char payload[128];
    snprintf_P(payload, sizeof(payload), 
               PSTR("{\"rfid\":\"%s\",\"timestamp\":\"%s\",\"device_id\":\"%s\"}"),
               rfidUID, timestamp.c_str(), deviceId.c_str());

    int responseCode = http.POST(payload);
    String response = http.getString();
    http.end();

    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      goto save_offline;
    }

    const char *msg = doc["message"] | "TERJADI KESALAHAN";
    strncpy(message, msg, 31);
    message[31] = '\0';

    if (responseCode == 200 && doc.containsKey("data")) {
      JsonObject data = doc["data"];
      const char *n = data["nama"] | "-";
      const char *w = data["waktu"] | "-";
      const char *s = data["statusPulang"] | data["status"] | "-";

      strncpy(name, n, 31);
      name[31] = '\0';
      strncpy(timeStr, w, 15);
      timeStr[15] = '\0';
      strncpy(status, s, 15);
      status[15] = '\0';

      return true;
    } else {
      goto save_offline;
    }
  }
  
save_offline:
  if (sdCardAvailable) {
    if (saveToQueue(rfidUID, timestamp.c_str(), currentUnixTime)) {
      strcpy(message, "DATA TERSIMPAN OFFLINE");
      strcpy(name, "Mode Offline");
      strcpy(timeStr, timestamp.substring(11).c_str());
      strcpy(status, "Pending");
      return true;
    } else {
      if (isDuplicateInAllQueues(rfidUID, currentUnixTime)) {
        strcpy(message, "DUPLIKAT! TAP < 1 JAM");
        strcpy(name, "Data Ditolak");
        strcpy(timeStr, timestamp.substring(11).c_str());
        strcpy(status, "Rejected");
      } else {
        strcpy(message, "QUEUE PENUH!");
      }
      return false;
    }
  }
  
  strcpy_P(message, PSTR("TIDAK ADA WIFI & SD"));
  return false;
}

// ========================================
// FUNGSI TAMPILAN
// ========================================

void showOLED(const __FlashStringHelper *line1, const char *line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  int16_t x1, y1;
  uint16_t w1, h1;

  display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 10);
  display.println(line1);

  display.getTextBounds(line2, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 30);
  display.println(line2);

  display.display();
}

void showOLED(const __FlashStringHelper *line1, const __FlashStringHelper *line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  int16_t x1, y1;
  uint16_t w1, h1;

  display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 10);
  display.println(line1);

  display.getTextBounds(line2, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 30);
  display.println(line2);

  display.display();
}

void showStandbySignal() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(2, 2);
  if (isOnline) {
    display.print(F("ONLINE"));
  } else {
    display.print(F("OFFLINE"));
  }
  
  if (sdCardAvailable) {
    int pendingCount = countAllOfflineRecords();
    if (pendingCount > 0) {
      display.setCursor(SCREEN_WIDTH - 42, 2);
      display.print(F("Q:"));
      display.print(pendingCount);
    }
  }

  const __FlashStringHelper *title = F("TEMPELKAN KARTU");
  int16_t x1, y1;
  uint16_t w1, h1;

  display.getTextBounds(title, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 20);
  display.println(title);

  struct tm timeInfo;
  if (getTimeWithFallback(&timeInfo)) {
    snprintf(messageBuffer, sizeof(messageBuffer), "%02d:%02d",
             timeInfo.tm_hour, timeInfo.tm_min);
    display.getTextBounds(messageBuffer, 0, 0, &x1, &y1, &w1, &h1);
    display.setCursor((SCREEN_WIDTH - w1) / 2, 35);
    display.println(messageBuffer);
  }

  if (WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    int bars = (rssi > -67) ? 4 :
               (rssi > -70) ? 3 :
               (rssi > -80) ? 2 :
               (rssi > -90) ? 1 : 0;

    const int barX = SCREEN_WIDTH - 18;
    const int barWidth = 3;
    const int barSpacing = 2;

    for (int i = 0; i < 4; i++) {
      int barHeight = 2 + i * 2;
      int x = barX + i * (barWidth + barSpacing);
      int y = 10 - barHeight;

      if (i < bars) {
        display.fillRect(x, y, barWidth, barHeight, WHITE);
      } else {
        display.drawRect(x, y, barWidth, barHeight, WHITE);
      }
    }
  } else {
    display.setCursor(SCREEN_WIDTH - 20, 2);
    display.println(F("X"));
  }

  display.display();
}

void showProgress(const __FlashStringHelper *message, int durationMs) {
  const int progressStep = 8;
  const int progressWidth = 80;
  int delayPerStep = durationMs / (progressWidth / progressStep);
  int startX = (SCREEN_WIDTH - progressWidth) / 2;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  int16_t x1, y1;
  uint16_t w1, h1;
  display.getTextBounds(message, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 20);
  display.println(message);
  display.display();

  for (int i = 0; i <= progressWidth; i += progressStep) {
    display.fillRect(startX, 40, i, 4, WHITE);
    display.display();
    delay(delayPerStep);
  }

  delay(500);
}

void showStartupAnimation() {
  display.clearDisplay();
  display.setTextColor(WHITE);

  const char title[] PROGMEM = "ZEDLABS";
  const char subtitle1[] PROGMEM = "INNOVATE BEYOND";
  const char subtitle2[] PROGMEM = "LIMITS";
  const char loading[] PROGMEM = "STARTING v2.1.0";

  const int titleLength = 7;
  const int titleX = (SCREEN_WIDTH - (titleLength * 12)) / 2;
  const int sub1X = (SCREEN_WIDTH - 15 * 6) / 2;
  const int sub2X = (SCREEN_WIDTH - 6 * 6) / 2;
  const int loadX = (SCREEN_WIDTH - 15 * 6) / 2;

  for (int x = -80; x <= titleX; x += 4) {
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
  display.setCursor(loadX, 55);
  display.print((__FlashStringHelper *)loading);
  display.display();

  for (int i = 0; i < 3; i++) {
    delay(300);
    display.print('.');
    display.display();
  }

  showProgress(F("MENYIAPKAN"), 2000);
  delay(1000);

  display.clearDisplay();
  display.display();
}

// ========================================
// FUNGSI BUZZER
// ========================================

inline void playToneSuccess() {
  for (int i = 0; i < 3; i++) {
    tone(PIN_BUZZER, 3000, 100);
    delay(150);
  }
  noTone(PIN_BUZZER);
}

inline void playToneError() {
  for (int i = 0; i < 5; i++) {
    tone(PIN_BUZZER, 3000, 150);
    delay(200);
  }
  noTone(PIN_BUZZER);
}

inline void playToneNotify() {
  tone(PIN_BUZZER, 3000, 100);
  delay(120);
  noTone(PIN_BUZZER);
}

void playStartupMelody() {
  const int melody[] PROGMEM = { 2500, 3000, 2500, 3000 };
  for (int i = 0; i < 4; i++) {
    tone(PIN_BUZZER, pgm_read_word_near(melody + i), 100);
    delay(150);
  }
  noTone(PIN_BUZZER);
}

// ========================================
// PENANGANAN ERROR
// ========================================

void fatalError(const __FlashStringHelper *errorMessage) {
  char errMsg[32];
  strncpy_P(errMsg, (const char*)errorMessage, sizeof(errMsg) - 1);
  errMsg[31] = '\0';
  showOLED((const __FlashStringHelper*)errMsg, "RESTART...");
  playToneError();
  delay(3000);
  ESP.restart();
}

// ========================================
// FUNGSI UTAMA
// ========================================

void setup() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  pinMode(PIN_BUZZER, OUTPUT);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  showStartupAnimation();
  playStartupMelody();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  deviceId = "ESP32_" + String(mac[4], HEX) + String(mac[5], HEX);
  deviceId.toUpperCase();

  showProgress(F("INIT SD CARD"), 1500);
  sdCardAvailable = initSDCard();
  
  if (sdCardAvailable) {
    showOLED(F("SD CARD"), "TERSEDIA");
    playToneSuccess();
    delay(1500);
    
    int pendingCount = countAllOfflineRecords();
    if (pendingCount > 0) {
      snprintf(messageBuffer, sizeof(messageBuffer), "%d data pending", pendingCount);
      showOLED(F("DATA OFFLINE"), messageBuffer);
      delay(2000);
    }
  } else {
    showOLED(F("SD CARD"), "TIDAK TERSEDIA");
    playToneError();
    delay(2000);
    showOLED(F("MODE ONLINE"), "SAJA");
    delay(1500);
  }

  if (!connectToWiFi()) {
    if (sdCardAvailable) {
      showOLED(F("WIFI GAGAL"), "MODE OFFLINE");
      playToneError();
      delay(2000);
      isOnline = false;
    } else {
      fatalError(F("WIFI & SD GAGAL"));
    }
  } else {
    showProgress(F("PING API"), 2000);
    int apiRetryCount = 0;
    while (!pingAPI() && apiRetryCount < 3) {
      apiRetryCount++;
      snprintf_P(messageBuffer, sizeof(messageBuffer), PSTR("PERCOBAAN %d"), apiRetryCount);
      showOLED(F("API GAGAL"), messageBuffer);
      playToneError();
      delay(2000);
    }

    if (isOnline) {
      showOLED(F("API TERHUBUNG"), "SINKRONISASI WAKTU");
      playToneSuccess();
      
      if (!syncTimeWithFallback()) {
        struct tm timeInfo;
        if (!getTimeWithFallback(&timeInfo)) {
          showOLED(F("PERINGATAN"), "WAKTU TIDAK TERSEDIA");
          playToneError();
          delay(2000);
        }
      }
      
      if (sdCardAvailable) {
        int pendingCount = countAllOfflineRecords();
        if (pendingCount > 0) {
          snprintf(messageBuffer, sizeof(messageBuffer), "%d records", pendingCount);
          showOLED(F("SYNC DATA"), messageBuffer);
          delay(1000);
          syncAllQueues();
        }
      }
    } else {
      showOLED(F("API OFFLINE"), "MODE OFFLINE");
      playToneError();
      delay(2000);
    }
  }

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_RFID_SS);
  rfidReader.PCD_Init();
  delay(100);

  // Deselect RFID setelah init
  digitalWrite(PIN_RFID_SS, HIGH);
  
  byte version = rfidReader.PCD_ReadRegister(rfidReader.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    fatalError(F("RC522 TIDAK TERDETEKSI"));
  }

  showOLED(F("SISTEM SIAP"), isOnline ? "ONLINE" : "OFFLINE");
  playToneSuccess();
  bootTime = millis();
  lastSyncTime = millis();
}

void loop() {
  periodicTimeSync();
  periodicSync();

  struct tm timeInfo;
  if (getTimeWithFallback(&timeInfo)) {
    int currentHour = timeInfo.tm_hour;

    if (currentHour >= SLEEP_START_HOUR || currentHour < SLEEP_END_HOUR) {
      showOLED(F("SLEEP MODE"), "SAMPAI PAGI");
      delay(2000);

      int sleepSeconds;
      if (currentHour >= SLEEP_START_HOUR) {
        sleepSeconds = ((24 - currentHour + SLEEP_END_HOUR) * 3600) -
                       (timeInfo.tm_min * 60 + timeInfo.tm_sec);
      } else {
        sleepSeconds = ((SLEEP_END_HOUR - currentHour) * 3600) -
                       (timeInfo.tm_min * 60 + timeInfo.tm_sec);
      }

      uint64_t sleepDuration = (uint64_t)sleepSeconds * 1000000ULL;
      esp_sleep_enable_timer_wakeup(sleepDuration);
      esp_deep_sleep_start();
    }
  }

  if (WiFi.status() != WL_CONNECTED && isOnline) {
    isOnline = false;
  } else if (WiFi.status() == WL_CONNECTED && !isOnline) {
    if (pingAPI()) {
      isOnline = true;
    }
  }

  if (rfidReader.PICC_IsNewCardPresent() && rfidReader.PICC_ReadCardSerial()) {
    digitalWrite(PIN_SD_CS, HIGH);   // Deselect SD Card
    digitalWrite(PIN_RFID_SS, LOW);  // Select RFID
    
    uidToString(rfidReader.uid.uidByte, rfidReader.uid.size, messageBuffer);

    if (strcmp(messageBuffer, lastUID) == 0 &&
        millis() - lastScanTime < DEBOUNCE_TIME) {
      rfidReader.PICC_HaltA();
      rfidReader.PCD_StopCrypto1();
      digitalWrite(PIN_RFID_SS, HIGH); // Deselect RFID
      return;
    }

    strcpy(lastUID, messageBuffer);
    lastScanTime = millis();

    showOLED(F("RFID TERDETEKSI"), messageBuffer);
    playToneNotify();
    delay(50);

    char message[32], name[32], status[16], timeStr[16];
    bool success = kirimPresensi(messageBuffer, message, name, status, timeStr);

    showOLED(success ? F("BERHASIL") : F("INFO"), message);

    if (success) {
      playToneSuccess();
    } else {
      playToneError();
    }

    delay(150);

    rfidReader.PICC_HaltA();
    rfidReader.PCD_StopCrypto1();
    digitalWrite(PIN_RFID_SS, HIGH); // Deselect RFID
  }

  showStandbySignal();
  delay(80);
}