#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <MFRC522.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>
#include <EEPROM.h>
#include <esp_wifi.h>
#include <esp_bt.h>

#define RST_PIN 3
#define SS_PIN 7
#define SD_CS 1
#define SDA_PIN 8
#define SCL_PIN 9
#define BUZZER_PIN 10
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DEBOUNCE_TIME 500
#define SCK 4
#define MISO 5
#define MOSI 6

const char *WIFI_SSIDS[] = {"SSID1", "SSID2"};
const char *WIFI_PASSWORDS[] = {"pass1", "pass2"};
const int WIFI_COUNT = 2;
const String API_BASE_URL = "https://example.com/api";
const String API_SECRET = "password";
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200;
const int daylightOffset_sec = 0;
const int MAX_RETRY_ATTEMPTS = 3;
const int WIFI_TIMEOUT = 20000;
const int HTTP_TIMEOUT = 10000;
const int MAX_OFFLINE_RECORDS = 100;
const int WATCHDOG_TIMEOUT = 30;
const int SLEEP_HOUR_START = 18;
const int SLEEP_HOUR_END = 3;

bool sdSiap = false;
String lastUid = "";
unsigned long lastScanTime = 0, lastWifiCheck = 0, lastSyncAttempt = 0, systemUptime = 0;
int wifiReconnectCount = 0, apiErrorCount = 0;
bool isDeepSleepMode = false;

struct TapHistory {
  String rfid;
  unsigned long tapTime;
  String timestamp;
};

const int MAX_TAP_HISTORY = 50;
TapHistory tapHistoryBuffer[MAX_TAP_HISTORY];
int tapHistoryIndex = 0, tapHistoryCount = 0;
const unsigned long MIN_TAP_INTERVAL = 1800000;

enum SystemStatus { STATUS_INIT, STATUS_READY, STATUS_PROCESSING, STATUS_ERROR, STATUS_OFFLINE, STATUS_MAINTENANCE };
SystemStatus currentStatus = STATUS_INIT;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 rfid(SS_PIN, RST_PIN);
HTTPClient http;

struct SystemStats {
  unsigned long totalScans = 0, successfulSyncs = 0, failedSyncs = 0, wifiReconnects = 0, systemReboots = 0, uptime = 0;
};
SystemStats stats;

void setup() {
  Serial.begin(115200);
  Serial.println("=== ZEDLABS Presensi System v0.1.2 ===");
  EEPROM.begin(512);
  loadSystemStats();
  stats.systemReboots++;
  btStop();
  esp_bt_controller_disable();
  initializeHardware();
  showStartupAnimation();
  playStartupMelody();
  initializeStorage();
  if (!initializeNetwork()) handleCriticalError("NETWORK INIT FAILED");
  initializeTimeSync();
  initializeRFID();
  performInitialSync();
  currentStatus = STATUS_READY;
  showSystemReady();
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  systemUptime = millis();
}

void loop() {
  unsigned long currentTime = millis();
  if (currentTime - systemUptime > 60000) {
    stats.uptime++;
    systemUptime = currentTime;
    saveSystemStats();
  }
  if (shouldEnterSleepMode()) { enterSleepMode(); return; }
  performPeriodicMaintenance(currentTime);
  processRFIDCard();
  updateDisplay();
  delay(50);
}

void initializeHardware() {
  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C) && !display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) 
    handleCriticalError("OLED INITIALIZATION FAILED");
  display.clearDisplay();
  display.display();
}

void initializeStorage() {
  int attempts = 0;
  while (attempts < MAX_RETRY_ATTEMPTS && !SD.begin(SD_CS)) { attempts++; delay(1000); }
  if (attempts >= MAX_RETRY_ATTEMPTS) {
    sdSiap = false;
    showOLED("WARNING", "NO SD CARD");
    delay(2000);
  } else {
    sdSiap = true;
    createSystemDirectories();
    checkSDCardHealth();
  }
}

bool initializeNetwork() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  for (int attempt = 0; attempt < MAX_RETRY_ATTEMPTS; attempt++) {
    if (connectToWiFi()) {
      if (performAPIHealthCheck()) return true;
      apiErrorCount++;
      return true;
    }
    delay(5000);
  }
  return false;
}

bool connectToWiFi() {
  for (int i = 0; i < WIFI_COUNT; i++) {
    WiFi.begin(WIFI_SSIDS[i], WIFI_PASSWORDS[i]);
    unsigned long startTime = millis();
    int dots = 0;
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_TIMEOUT) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      String ssid = String(WIFI_SSIDS[i]);
      if (ssid.length() > 16) ssid = ssid.substring(0, 16);
      display.setCursor((SCREEN_WIDTH - ssid.length() * 6) / 2, 10);
      display.println(ssid);
      String status = "CONNECTING";
      for (int j = 0; j < (dots % 4); j++) status += ".";
      display.setCursor((SCREEN_WIDTH - status.length() * 6) / 2, 30);
      display.println(status);
      drawSignalBars();
      display.display();
      delay(500);
      dots++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      showOLED("WIFI CONNECTED", WiFi.localIP().toString());
      playToneSuccess();
      delay(2000);
      return true;
    }
  }
  return false;
}

bool performAPIHealthCheck() {
  if (WiFi.status() != WL_CONNECTED) return false;
  showProgress("API HEALTH CHECK", 2000);
  http.setTimeout(HTTP_TIMEOUT);
  for (int attempt = 0; attempt < MAX_RETRY_ATTEMPTS; attempt++) {
    http.begin(API_BASE_URL + "/presensi/ping");
    http.addHeader("X-API-KEY", API_SECRET);
    http.addHeader("User-Agent", "ZEDLABS-Presensi/0.1.2");
    int httpCode = http.GET();
    http.end();
    if (httpCode == 200) {
      showOLED("API ONLINE", "SYSTEM READY");
      playToneSuccess();
      delay(1000);
      return true;
    }
    delay(2000);
  }
  showOLED("API WARNING", "OFFLINE MODE");
  playToneError();
  delay(2000);
  return false;
}

void initializeTimeSync() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  int attempts = 0;
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo) && attempts < 10) { delay(1000); attempts++; }
  if (attempts >= 10) { showOLED("TIME SYNC", "FAILED"); delay(2000); }
}

void initializeRFID() {
  SPI.begin(SCK, MISO, MOSI, SS_PIN);
  rfid.PCD_Init();
  delay(100);
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  if (version == 0x00 || version == 0xFF) handleCriticalError("RFID READER NOT DETECTED");
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);
}

void performInitialSync() {
  if (!sdSiap || WiFi.status() != WL_CONNECTED) return;
  showProgress("SYNCING OFFLINE DATA", 3000);
  int syncedRecords = kirimDataOffline();
  if (syncedRecords > 0) { showOLED("SYNC COMPLETE", String(syncedRecords) + " RECORDS"); delay(2000); }
}

void performPeriodicMaintenance(unsigned long currentTime) {
  if (currentTime - lastWifiCheck > 30000) { checkWiFiConnection(); lastWifiCheck = currentTime; }
  if (currentTime - lastSyncAttempt > 300000) {
    if (sdSiap && WiFi.status() == WL_CONNECTED) {
      int synced = kirimDataOffline();
      if (synced > 0) stats.successfulSyncs++;
    }
    lastSyncAttempt = currentTime;
  }
  static unsigned long lastCleanup = 0;
  if (currentTime - lastCleanup > 600000) {
    performMemoryCleanup();
    cleanupOldTapHistory(currentTime);
    lastCleanup = currentTime;
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiReconnectCount++;
    stats.wifiReconnects++;
    connectToWiFi();
  }
}

bool isRecentTap(String rfid, unsigned long currentTime) {
  for (int i = 0; i < tapHistoryCount; i++) {
    if (tapHistoryBuffer[i].rfid == rfid) {
      unsigned long timeDiff = currentTime - tapHistoryBuffer[i].tapTime;
      if (timeDiff < MIN_TAP_INTERVAL) return true;
    }
  }
  if (sdSiap) return checkRecentTapInSD(rfid, currentTime);
  return false;
}

bool checkRecentTapInSD(String rfid, unsigned long currentTime) {
  File file = SD.open("/offline.json", FILE_READ);
  if (file) {
    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (!error && doc.is<JsonArray>()) {
      JsonArray dataArray = doc.as<JsonArray>();
      for (JsonObject record : dataArray) {
        String recordRfid = record["uid"];
        String originalTapTime = record["original_tap_time"];
        if (recordRfid == rfid) {
          unsigned long recordTime = parseTimestampToMillis(originalTapTime);
          if (recordTime > 0 && currentTime - recordTime < MIN_TAP_INTERVAL) return true;
        }
      }
    }
  }
  return checkRecentTapInTransactionLog(rfid, currentTime);
}

bool checkRecentTapInTransactionLog(String rfid, unsigned long currentTime) {
  File logFile = SD.open("/transaction.log", FILE_READ);
  if (!logFile) return false;
  String lines[100];
  int lineCount = 0;
  if (logFile.available()) logFile.readStringUntil('\n');
  while (logFile.available() && lineCount < 100) {
    String line = logFile.readStringUntil('\n');
    if (line.length() > 0) { lines[lineCount] = line; lineCount++; }
  }
  logFile.close();
  for (int i = lineCount - 1; i >= 0; i--) {
    if (lines[i].indexOf(rfid) != -1) {
      int firstComma = lines[i].indexOf(',');
      int secondComma = lines[i].indexOf(',', firstComma + 1);
      if (firstComma != -1 && secondComma != -1) {
        String logRfid = lines[i].substring(firstComma + 1, secondComma);
        String logTimestamp = lines[i].substring(0, firstComma);
        if (logRfid == rfid) {
          unsigned long logTime = parseTimestampToMillis(logTimestamp);
          if (logTime > 0 && currentTime - logTime < MIN_TAP_INTERVAL) return true;
        }
      }
    }
  }
  return false;
}

unsigned long parseTimestampToMillis(String timestamp) {
  if (timestamp.startsWith("OFFLINE_")) return timestamp.substring(8).toInt();
  struct tm tm_time;
  memset(&tm_time, 0, sizeof(tm_time));
  if (timestamp.length() >= 19) {
    tm_time.tm_year = timestamp.substring(0, 4).toInt() - 1900;
    tm_time.tm_mon = timestamp.substring(5, 7).toInt() - 1;
    tm_time.tm_mday = timestamp.substring(8, 10).toInt();
    tm_time.tm_hour = timestamp.substring(11, 13).toInt();
    tm_time.tm_min = timestamp.substring(14, 16).toInt();
    tm_time.tm_sec = timestamp.substring(17, 19).toInt();
    time_t epoch = mktime(&tm_time);
    if (epoch != -1) return (unsigned long)(epoch - gmtOffset_sec) * 1000;
  }
  return 0;
}

void addTapToHistory(String rfid, unsigned long tapTime, String timestamp) {
  tapHistoryBuffer[tapHistoryIndex].rfid = rfid;
  tapHistoryBuffer[tapHistoryIndex].tapTime = tapTime;
  tapHistoryBuffer[tapHistoryIndex].timestamp = timestamp;
  tapHistoryIndex = (tapHistoryIndex + 1) % MAX_TAP_HISTORY;
  if (tapHistoryCount < MAX_TAP_HISTORY) tapHistoryCount++;
}

void cleanupOldTapHistory(unsigned long currentTime) {
  for (int i = 0; i < tapHistoryCount; i++) {
    if (currentTime - tapHistoryBuffer[i].tapTime > MIN_TAP_INTERVAL) {
      for (int j = i; j < tapHistoryCount - 1; j++) tapHistoryBuffer[j] = tapHistoryBuffer[j + 1];
      tapHistoryCount--;
      if (tapHistoryIndex > 0) tapHistoryIndex--;
      i--;
    }
  }
}

void processRFIDCard() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  String rfidStr = uidToString(rfid.uid.uidByte, rfid.uid.size);
  unsigned long currentTime = millis();
  if (rfidStr == lastUid && (currentTime - lastScanTime) < DEBOUNCE_TIME) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }
  lastUid = rfidStr;
  lastScanTime = currentTime;
  stats.totalScans++;
  currentStatus = STATUS_PROCESSING;
  showOLED("RFID DETECTED", rfidStr);
  playToneNotify();
  processAttendance(rfidStr);
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  currentStatus = STATUS_READY;
}

void processAttendance(String rfidStr) {
  String pesan, nama, status, waktu;
  bool success = false;
  unsigned long currentTime = millis();
  if (isRecentTap(rfidStr, currentTime)) {
    showOLED("TAP DITOLAK", "TUNGGU 30 MENIT");
    playToneError();
    if (sdSiap) logRejectedTap(rfidStr, getCurrentTimestamp(), "DUPLICATE TAP < 30MIN");
    delay(3000);
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    success = kirimPresensi(rfidStr, pesan, nama, status, waktu);
    if (success) { stats.successfulSyncs++; apiErrorCount = 0; }
    else { apiErrorCount++; stats.failedSyncs++; }
  }
  if (success) {
    showSuccessMessage(nama, status, waktu);
    playToneSuccess();
    logTransaction(rfidStr, nama, status, waktu, "ONLINE");
    addTapToHistory(rfidStr, currentTime, getCurrentTimestamp());
  } else {
    handleOfflineAttendance(rfidStr, pesan);
    addTapToHistory(rfidStr, currentTime, getCurrentTimestamp());
  }
  delay(1500);
}

void logRejectedTap(String rfid, String timestamp, String reason) {
  if (!sdSiap) return;
  String logEntry = timestamp + "," + rfid + ",REJECTED,REJECTED," + timestamp.substring(11, 19) + "," + reason + "," + String(WiFi.RSSI()) + "," + String(ESP.getFreeHeap()) + "," + WiFi.macAddress() + "\n";
  File logFile = SD.open("/rejected.log", FILE_APPEND);
  if (logFile) {
    if (logFile.size() == 0) logFile.println("timestamp,rfid,nama,status,waktu,reason,signal_strength,free_heap,device_mac");
    logFile.print(logEntry);
    logFile.close();
  }
}

void handleOfflineAttendance(String rfidStr, String errorMessage) {
  String originalTapTime = getCurrentTimestamp();
  bool saved = simpanOfflineLog(rfidStr, originalTapTime);
  if (saved) {
    showOLED("OFFLINE MODE", "DATA SAVED");
    playToneNotify();
    logTransaction(rfidStr, "Unknown", "OFFLINE", originalTapTime, "OFFLINE");
  } else {
    showOLED("ERROR", "STORAGE FAILED");
    playToneError();
  }
}

void showSuccessMessage(String nama, String status, String waktu) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor((SCREEN_WIDTH - 7 * 6) / 2, 5);
  display.println("SUCCESS");
  String displayName = nama;
  if (displayName.length() > 16) displayName = displayName.substring(0, 13) + "...";
  display.setCursor((SCREEN_WIDTH - displayName.length() * 6) / 2, 20);
  display.println(displayName);
  display.setCursor((SCREEN_WIDTH - status.length() * 6) / 2, 35);
  display.println(status);
  display.setCursor((SCREEN_WIDTH - waktu.length() * 6) / 2, 50);
  display.println(waktu);
  display.display();
}

bool shouldEnterSleepMode() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  int currentHour = timeinfo.tm_hour;
  return (currentHour >= SLEEP_HOUR_START || currentHour < SLEEP_HOUR_END);
}

void enterSleepMode() {
  showOLED("SLEEP MODE", "UNTIL MORNING");
  delay(2000);
  saveSystemStats();
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  int hoursToSleep = (timeinfo.tm_hour >= SLEEP_HOUR_START) ? (24 - timeinfo.tm_hour + SLEEP_HOUR_END) : (SLEEP_HOUR_END - timeinfo.tm_hour);
  uint64_t sleepTime = (uint64_t)hoursToSleep * 3600 * 1000000ULL;
  display.clearDisplay();
  display.display();
  WiFi.disconnect(true);
  esp_sleep_enable_timer_wakeup(sleepTime);
  esp_deep_sleep_start();
}

void updateDisplay() {
  static unsigned long lastUpdate = 0;
  unsigned long currentTime = millis();
  if (currentStatus == STATUS_READY && (currentTime - lastUpdate) > 2000) {
    showStandbySignal();
    lastUpdate = currentTime;
  }
}

void showStandbySignal() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  String title = "TEMPELKAN KARTU";
  display.setCursor((SCREEN_WIDTH - title.length() * 6) / 2, 25);
  display.println(title);
  drawStatusIndicators();
  drawSignalBars();
  String statsLine = "S:" + String(stats.totalScans) + " E:" + String(apiErrorCount);
  display.setCursor(0, 55);
  display.println(statsLine);
  display.display();
}

void drawStatusIndicators() {
  display.setCursor(0, 0);
  display.print(WiFi.status() == WL_CONNECTED ? "WiFi" : "----");
  display.setCursor(40, 0);
  display.print(sdSiap ? "SD" : "--");
  display.setCursor(60, 0);
  display.print((apiErrorCount == 0 && WiFi.status() == WL_CONNECTED) ? "API" : "---");
}

void drawSignalBars() {
  if (WiFi.status() != WL_CONNECTED) return;
  long rssi = WiFi.RSSI();
  int bars = (rssi > -50) ? 4 : (rssi > -60) ? 3 : (rssi > -70) ? 2 : (rssi > -80) ? 1 : 0;
  int baseX = SCREEN_WIDTH - 20, barWidth = 3, spacing = 1;
  for (int i = 0; i < 4; i++) {
    int barHeight = 2 + i * 2, x = baseX + i * (barWidth + spacing), y = 8 - barHeight;
    if (i < bars) display.fillRect(x, y, barWidth, barHeight, WHITE);
    else display.drawRect(x, y, barWidth, barHeight, WHITE);
  }
}

String uidToString(uint8_t *uid, uint8_t len) {
  String result = "";
  if (len >= 4) {
    uint32_t value = ((uint32_t)uid[3] << 24) | ((uint32_t)uid[2] << 16) | ((uint32_t)uid[1] << 8) | uid[0];
    result = String(value);
  } else {
    for (int i = 0; i < len; i++) {
      if (uid[i] < 0x10) result += "0";
      result += String(uid[i], HEX);
    }
  }
  while (result.length() < 10) result = "0" + result;
  return result;
}

bool kirimPresensi(String rfid, String &pesan, String &nama, String &status, String &waktu) {
  if (WiFi.status() != WL_CONNECTED) { pesan = "NO WIFI CONNECTION"; return false; }
  http.setTimeout(HTTP_TIMEOUT);
  http.begin(API_BASE_URL + "/presensi/rfid");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("X-API-KEY", API_SECRET);
  http.addHeader("User-Agent", "ZEDLABS-Presensi/0.1.2");
  StaticJsonDocument<200> requestDoc;
  requestDoc["rfid"] = rfid;
  requestDoc["device_id"] = WiFi.macAddress();
  requestDoc["timestamp"] = getCurrentTimestamp();
  String payload;
  serializeJson(requestDoc, payload);
  int httpCode = http.POST(payload);
  String response = http.getString();
  http.end();
  StaticJsonDocument<1024> responseDoc;
  DeserializationError error = deserializeJson(responseDoc, response);
  if (error) { pesan = "INVALID RESPONSE FORMAT"; return false; }
  pesan = responseDoc["message"] | "UNKNOWN ERROR";
  if (httpCode == 200 && responseDoc.containsKey("data")) {
    JsonObject data = responseDoc["data"];
    nama = data["nama"] | "Unknown";
    waktu = data["waktu"] | getCurrentTime();
    status = data.containsKey("statusPulang") ? (data["statusPulang"] | "PULANG") : data.containsKey("status") ? (data["status"] | "MASUK") : "SUCCESS";
    return true;
  }
  return false;
}

bool simpanOfflineLog(String rfid, String originalTapTime) {
  if (!sdSiap) return false;
  StaticJsonDocument<4096> doc;
  File file = SD.open("/offline.json", FILE_READ);
  if (file) {
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) doc.clear();
  }
  JsonArray dataArray = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();
  if (dataArray.size() >= MAX_OFFLINE_RECORDS) dataArray.remove(0);
  JsonObject newRecord = dataArray.createNestedObject();
  newRecord["uid"] = rfid;
  newRecord["original_tap_time"] = originalTapTime;
  newRecord["created_at"] = getCurrentTimestamp();
  newRecord["device_id"] = WiFi.macAddress();
  newRecord["retry_count"] = 0;
  newRecord["signal_strength"] = WiFi.RSSI();
  newRecord["free_heap"] = ESP.getFreeHeap();
  file = SD.open("/offline.json", FILE_WRITE);
  if (!file) return false;
  serializeJsonPretty(doc, file);
  file.close();
  return true;
}

int kirimDataOffline() {
  if (!sdSiap || WiFi.status() != WL_CONNECTED) return 0;
  File file = SD.open("/offline.json", FILE_READ);
  if (!file) return 0;
  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) return 0;
  JsonArray dataArray = doc.as<JsonArray>();
  JsonArray failedRecords;
  int syncedCount = 0;
  for (JsonObject record : dataArray) {
    String rfid = record["uid"];
    String originalTapTime = record["original_tap_time"];
    String createdAt = record["created_at"] | "unknown";
    int retryCount = record["retry_count"] | 0;
    if (kirimPresensiWithTimestamp(rfid, originalTapTime)) {
      syncedCount++;
      logTransaction(rfid, "Synced User", "SYNCED", originalTapTime, "SYNCED");
    } else {
      if (retryCount < 5) {
        JsonObject failedRecord = failedRecords.createNestedObject();
        failedRecord["uid"] = rfid;
        failedRecord["original_tap_time"] = originalTapTime;
        failedRecord["created_at"] = createdAt;
        failedRecord["device_id"] = record["device_id"];
        failedRecord["retry_count"] = retryCount + 1;
        failedRecord["signal_strength"] = record["signal_strength"];
        failedRecord["free_heap"] = record["free_heap"];
        failedRecord["last_retry"] = getCurrentTimestamp();
      } else {
        logTransaction(rfid, "Dropped User", "DROPPED", originalTapTime, "DROPPED");
      }
    }
    delay(100);
  }
  file = SD.open("/offline.json", FILE_WRITE);
  if (file) {
    if (failedRecords.size() > 0) serializeJsonPretty(failedRecords, file);
    else file.print("[]");
    file.close();
  }
  return syncedCount;
}

bool kirimPresensiWithTimestamp(String rfid, String originalTimestamp) {
  if (WiFi.status() != WL_CONNECTED) return false;
  http.setTimeout(HTTP_TIMEOUT);
  http.begin(API_BASE_URL + "/presensi/rfid");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("X-API-KEY", API_SECRET);
  http.addHeader("User-Agent", "ZEDLABS-Presensi/0.1.2");
  StaticJsonDocument<300> requestDoc;
  requestDoc["rfid"] = rfid;
  requestDoc["device_id"] = WiFi.macAddress();
  requestDoc["timestamp"] = originalTimestamp;
  requestDoc["sync_mode"] = true;
  requestDoc["sync_timestamp"] = getCurrentTimestamp();
  requestDoc["device_info"]["signal_strength"] = WiFi.RSSI();
  requestDoc["device_info"]["free_heap"] = ESP.getFreeHeap();
  String payload;
  serializeJson(requestDoc, payload);
  int httpCode = http.POST(payload);
  String response = http.getString();
  http.end();
  StaticJsonDocument<1024> responseDoc;
  DeserializationError error = deserializeJson(responseDoc, response);
  if (error) return false;
  return (httpCode == 200 || httpCode == 409);
}

void logTransaction(String rfid, String nama, String status, String waktu, String syncType) {
  if (!sdSiap) return;
  String logEntry = getCurrentTimestamp() + "," + rfid + "," + nama + "," + status + "," + waktu + "," + syncType + "," + String(WiFi.RSSI()) + "," + String(ESP.getFreeHeap()) + "," + WiFi.macAddress() + "\n";
  File logFile = SD.open("/transaction.log", FILE_APPEND);
  if (logFile) { logFile.print(logEntry); logFile.close(); }
}

String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "OFFLINE_" + String(millis());
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00";
  char buffer[16];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
  return String(buffer);
}

void createSystemDirectories() {
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  if (!SD.exists("/backup")) SD.mkdir("/backup");
  if (!SD.exists("/system_info.txt")) {
    File sysInfo = SD.open("/system_info.txt", FILE_WRITE);
    if (sysInfo) {
      sysInfo.println("ZEDLABS Presensi System v0.1.2");
      sysInfo.println("Device MAC: " + WiFi.macAddress());
      sysInfo.println("Initialized: " + getCurrentTimestamp());
      sysInfo.close();
    }
  }
  if (!SD.exists("/transaction.log")) {
    File logFile = SD.open("/transaction.log", FILE_WRITE);
    if (logFile) {
      logFile.println("timestamp,rfid,nama,status,waktu,sync_type,signal_strength,free_heap,device_mac");
      logFile.close();
    }
  }
}

void checkSDCardHealth() {
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
  if (usedBytes > (cardSize * 0.9)) {
    showOLED("SD WARNING", "STORAGE FULL");
    delay(3000);
    cleanupOldLogs();
  }
}

void cleanupOldLogs() {
  if (SD.exists("/transaction.log")) {
    String backupName = "/backup/transaction_" + String(millis()) + ".log";
    File sourceFile = SD.open("/transaction.log", FILE_READ);
    File backupFile = SD.open(backupName, FILE_WRITE);
    if (sourceFile && backupFile) {
      while (sourceFile.available()) backupFile.write(sourceFile.read());
      sourceFile.close();
      backupFile.close();
    }
    File logFile = SD.open("/transaction.log", FILE_READ);
    if (logFile && logFile.size() > 50000) {
      String lastLines = "";
      String line;
      int lineCount = 0;
      while (logFile.available() && lineCount < 2000) {
        line = logFile.readStringUntil('\n');
        if (line.length() > 0) { lastLines = line + "\n" + lastLines; lineCount++; }
      }
      logFile.close();
      int keepLines = 1000;
      String finalLines = "";
      int currentLines = 0;
      int startPos = 0;
      while (currentLines < keepLines && startPos < lastLines.length()) {
        int newlinePos = lastLines.indexOf('\n', startPos);
        if (newlinePos == -1) break;
        finalLines = lastLines.substring(startPos, newlinePos + 1) + finalLines;
        startPos = newlinePos + 1;
        currentLines++;
      }
      logFile = SD.open("/transaction.log", FILE_WRITE);
      if (logFile) {
        logFile.println("timestamp,rfid,nama,status,waktu,sync_type,signal_strength,free_heap,device_mac");
        logFile.print(finalLines);
        logFile.close();
      }
    }
  }
}

void performMemoryCleanup() {
  lastUid = "";
  String tempStr;
  tempStr.reserve(1000);
  tempStr = "";
  if (ESP.getFreeHeap() < 10000) handleCriticalError("LOW MEMORY");
}

void loadSystemStats() {
  EEPROM.get(0, stats);
  if (stats.totalScans > 1000000 || stats.systemReboots > 10000) {
    memset(&stats, 0, sizeof(stats));
    saveSystemStats();
  }
}

void saveSystemStats() {
  EEPROM.put(0, stats);
  EEPROM.commit();
}

void showSystemReady() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor((SCREEN_WIDTH - 11 * 6) / 2, 5);
  display.println("SYSTEM READY");
  display.setCursor(0, 20);
  display.printf("Scans: %lu", stats.totalScans);
  display.setCursor(0, 30);
  display.printf("Reboots: %lu", stats.systemReboots);
  display.setCursor(0, 40);
  display.printf("WiFi: %s", (WiFi.status() == WL_CONNECTED) ? "OK" : "FAIL");
  display.setCursor(0, 50);
  display.printf("SD: %s API: %s", sdSiap ? "OK" : "--", (apiErrorCount == 0) ? "OK" : "ERR");
  display.display();
  playToneSuccess();
  delay(3000);
}

void handleCriticalError(String errorMessage) {
  if (sdSiap) {
    File errorLog = SD.open("/error.log", FILE_APPEND);
    if (errorLog) {
      errorLog.println(getCurrentTimestamp() + " - CRITICAL: " + errorMessage);
      errorLog.close();
    }
  }
  showOLED("CRITICAL ERROR", errorMessage);
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, 2000, 200);
    delay(300);
    noTone(BUZZER_PIN);
    delay(200);
  }
  delay(5000);
  saveSystemStats();
  ESP.restart();
}

void showOLED(String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  line1.toUpperCase();
  line2.toUpperCase();
  int16_t x1, y1;
  uint16_t w1, h1;
  display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 15);
  display.println(line1);
  display.getTextBounds(line2, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 35);
  display.println(line2);
  display.display();
}

void showProgress(String message, int duration) {
  int steps = 40;
  int stepDelay = duration / steps;
  int barWidth = 80;
  int barHeight = 4;
  int barX = (SCREEN_WIDTH - barWidth) / 2;
  int barY = 40;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor((SCREEN_WIDTH - message.length() * 6) / 2, 20);
  display.println(message);
  display.drawRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2, WHITE);
  display.display();
  for (int i = 0; i <= steps; i++) {
    int fillWidth = (barWidth * i) / steps;
    display.fillRect(barX, barY, fillWidth, barHeight, WHITE);
    display.display();
    delay(stepDelay);
  }
  delay(500);
}

void showStartupAnimation() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  String company = "ZEDLABS";
  String tagline1 = "INNOVATE BEYOND";
  String tagline2 = "LIMITS";
  String version = "Presensi v0.1.2";
  for (int x = -60; x <= (SCREEN_WIDTH - company.length() * 12) / 2; x += 3) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(x, 5);
    display.println(company);
    display.display();
    delay(20);
  }
  delay(500);
  display.setTextSize(1);
  display.setCursor((SCREEN_WIDTH - tagline1.length() * 6) / 2, 30);
  display.println(tagline1);
  display.setCursor((SCREEN_WIDTH - tagline2.length() * 6) / 2, 40);
  display.println(tagline2);
  display.setCursor((SCREEN_WIDTH - version.length() * 6) / 2, 55);
  display.println(version);
  display.display();
  delay(2000);
  for (int i = 0; i < 3; i++) {
    display.clearDisplay();
    display.display();
    delay(200);
    display.setTextSize(2);
    display.setCursor((SCREEN_WIDTH - company.length() * 12) / 2, 5);
    display.println(company);
    display.setTextSize(1);
    display.setCursor((SCREEN_WIDTH - tagline1.length() * 6) / 2, 30);
    display.println(tagline1);
    display.setCursor((SCREEN_WIDTH - tagline2.length() * 6) / 2, 40);
    display.println(tagline2);
    display.setCursor((SCREEN_WIDTH - version.length() * 6) / 2, 55);
    display.println(version);
    display.display();
    delay(200);
  }
  display.clearDisplay();
  display.display();
}

void playToneSuccess() {
  int melody[] = {1000, 1200, 1500};
  int durations[] = {150, 150, 200};
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, melody[i], durations[i]);
    delay(durations[i] + 50);
    noTone(BUZZER_PIN);
  }
}

void playToneError() {
  int melody[] = {800, 600, 400};
  int durations[] = {200, 200, 300};
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, melody[i], durations[i]);
    delay(durations[i] + 50);
    noTone(BUZZER_PIN);
  }
}

void playToneNotify() {
  tone(BUZZER_PIN, 1200, 100);
  delay(150);
  noTone(BUZZER_PIN);
}

void playStartupMelody() {
  int melody[] = {523, 659, 784, 1047};
  int durations[] = {200, 200, 200, 400};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, melody[i], durations[i]);
    delay(durations[i] + 50);
    noTone(BUZZER_PIN);
  }
}
