/*
 * ======================================================================================
 * SISTEM PRESENSI PINTAR (RFID) - QUEUE SYSTEM v2.2.2 ULTIMATE
 * ======================================================================================
 * Device  : ESP32-C3 Super Mini
 * Author  : Yahya Zulfikri
 * Created : Juli 2025
 * Updated : Desember 2025
 * Version : 2.2.2
 *
 * CHANGELOG v2.2.2:
 * - Cached record counter untuk performa
 * - Display buffer untuk reduce redraw
 * - Chunked sync dengan state machine
 * - Limited duplicate check
 * - Async cache initialization
 * - Shorter HTTP timeout
 * - Task scheduling di main loop
 * - Fast RFID handler
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

// PIN DEFINITIONS
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

// NETWORK CONFIG
const char WIFI_SSID_1[] PROGMEM = "SSID_WIFI_1";
const char WIFI_SSID_2[] PROGMEM = "SSID_WIFI_2";
const char WIFI_PASSWORD_1[] PROGMEM = "PasswordWifi1";
const char WIFI_PASSWORD_2[] PROGMEM = "PasswordWifi1";
const char API_BASE_URL[] PROGMEM = "https://zedlabs.id";
const char API_SECRET_KEY[] PROGMEM = "SecretAPIToken";
const char NTP_SERVER_1[] PROGMEM = "pool.ntp.org";
const char NTP_SERVER_2[] PROGMEM = "time.google.com";
const char NTP_SERVER_3[] PROGMEM = "id.pool.ntp.org";

// QUEUE SYSTEM CONFIG
const int MAX_RECORDS_PER_FILE = 50;
const int MAX_QUEUE_FILES = 1000;
const unsigned long SYNC_INTERVAL = 30000;        // 10 detik
const unsigned long MAX_OFFLINE_AGE = 2592000;    // 1 bulan
const unsigned long MIN_REPEAT_INTERVAL = 1800;   // 30 detik
const unsigned long TIME_SYNC_INTERVAL = 3600000; // 1 jam
const unsigned long RECONNECT_INTERVAL = 30000;   // 30 detik
const int SLEEP_START_HOUR = 18;
const int SLEEP_END_HOUR = 5;
const long GMT_OFFSET_SEC = 25200;

// OPTIMASI CONFIG
const unsigned long COUNT_CACHE_DURATION = 30000; // 30 detik
const int MAX_SYNC_FILES_PER_CYCLE = 2;
const unsigned long MAX_SYNC_TIME = 5000;           // 5 detik per cycle
const unsigned long DISPLAY_UPDATE_INTERVAL = 500;  // 500ms
const unsigned long PERIODIC_CHECK_INTERVAL = 1000; // 1 detik
const int MAX_DUPLICATE_CHECK_LINES = 100;          // Batasi pembacaan file

// RTC MEMORY
RTC_DATA_ATTR time_t lastValidTime = 0;
RTC_DATA_ATTR bool timeWasSynced = false;
RTC_DATA_ATTR unsigned long bootTime = 0;
RTC_DATA_ATTR int currentQueueFile = 0;

// HARDWARE OBJECTS
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 rfidReader(PIN_RFID_SS, PIN_RFID_RST);
SdFat sd;
FsFile file;

// GLOBAL VARIABLES
char lastUID[11] = "";
unsigned long lastScanTime = 0;
unsigned long lastSyncTime = 0;
unsigned long lastTimeSyncAttempt = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastPeriodicCheck = 0;
char messageBuffer[64];
bool isOnline = false;
bool sdCardAvailable = false;
String deviceId = "ESP32_";

// OPTIMASI 1: Cached Record Counter
int cachedRecordCount = -1;
unsigned long lastCountUpdate = 0;

// OPTIMASI 2: Display Buffer
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

// OPTIMASI 3: Chunked Sync State Machine
int syncCurrentFile = 0;
bool syncInProgress = false;
unsigned long syncStartTime = 0;

// OPTIMASI 5: Async Cache Init
bool cacheInitInProgress = false;
int cacheInitFileIndex = 0;

// NON-BLOCKING RECONNECT STATE MACHINE
enum ReconnectState
{
  RECONNECT_IDLE,
  RECONNECT_TRYING_SSID1,
  RECONNECT_TRYING_SSID2,
  RECONNECT_SUCCESS,
  RECONNECT_FAILED
};

ReconnectState reconnectState = RECONNECT_IDLE;
int reconnectRetryCount = 0;
unsigned long reconnectStartTime = 0;
const int MAX_RECONNECT_RETRY = 20;
const int RECONNECT_RETRY_DELAY = 300;

// DUPLICATE CHECK CACHE
struct DuplicateCache
{
  char rfid[11];
  unsigned long unixTime;
};

const int CACHE_SIZE = 20;
DuplicateCache duplicateCache[CACHE_SIZE];
int cacheIndex = 0;
bool cacheInitialized = false;

// RACE CONDITION PROTECTION
volatile bool isWritingToQueue = false;
volatile bool isReadingQueue = false;

// BACKGROUND PROCESS FLAGS
bool isSyncing = false;
bool isReconnecting = false;

struct OfflineRecord
{
  String rfid;
  String timestamp;
  String deviceId;
  unsigned long unixTime;
};

// FUNCTION DECLARATIONS
void showOLED(const __FlashStringHelper *line1, const char *line2);
void showOLED(const __FlashStringHelper *line1, const __FlashStringHelper *line2);
void showProgress(const __FlashStringHelper *message, int durationMs);
void playToneSuccess();
void playToneError();
void playToneNotify();
void playStartupMelody();
void fatalError(const __FlashStringHelper *errorMessage);
void handleRFIDScan();
void updateCurrentDisplayState();
void updateStandbySignal();

// ========================================
// OPTIMASI 1: Cached Record Counter
// ========================================
int getCachedRecordCount();
void invalidateRecordCountCache();

// ========================================
// SD CARD HELPERS
// ========================================
inline void selectSD()
{
  digitalWrite(PIN_RFID_SS, HIGH);
  digitalWrite(PIN_SD_CS, LOW);
}

inline void deselectSD()
{
  digitalWrite(PIN_SD_CS, HIGH);
}

String getQueueFileName(int index)
{
  char filename[20];
  snprintf(filename, sizeof(filename), "/queue_%d.csv", index);
  return String(filename);
}

int _countRecordsInternal(const String &filename)
{
  if (!file.open(filename.c_str(), O_RDONLY))
    return 0;

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
  return count;
}

int countRecordsInFile(const String &filename)
{
  if (!sdCardAvailable)
    return 0;
  selectSD();
  int count = _countRecordsInternal(filename);
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
    String filename = getQueueFileName(i);
    if (sd.exists(filename.c_str()))
    {
      total += _countRecordsInternal(filename);
    }
  }

  deselectSD();
  return total;
}

// OPTIMASI 1: Implementasi Cached Counter
int getCachedRecordCount()
{
  if (cachedRecordCount < 0 ||
      millis() - lastCountUpdate > COUNT_CACHE_DURATION)
  {
    cachedRecordCount = countAllOfflineRecords();
    lastCountUpdate = millis();
  }
  return cachedRecordCount;
}

void invalidateRecordCountCache()
{
  cachedRecordCount = -1;
}

// ========================================
// SD CARD INITIALIZATION
// ========================================
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
    String filename = getQueueFileName(i);

    if (!sd.exists(filename.c_str()))
    {
      if (file.open(filename.c_str(), O_WRONLY | O_CREAT))
      {
        file.println("rfid,timestamp,device_id,unix_time");
        file.close();
        currentQueueFile = i;
        break;
      }
    }
    else
    {
      int count = _countRecordsInternal(filename);
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

// ========================================
// DUPLICATE CHECK
// ========================================
bool isDuplicateInCache(const char *rfid, unsigned long currentUnixTime)
{
  for (int i = 0; i < CACHE_SIZE; i++)
  {
    if (duplicateCache[i].rfid[0] != '\0' &&
        strcmp(duplicateCache[i].rfid, rfid) == 0)
    {
      if (currentUnixTime - duplicateCache[i].unixTime < MIN_REPEAT_INTERVAL)
      {
        return true;
      }
    }
  }
  return false;
}

void addToCache(const char *rfid, unsigned long unixTime)
{
  strncpy(duplicateCache[cacheIndex].rfid, rfid, 10);
  duplicateCache[cacheIndex].rfid[10] = '\0';
  duplicateCache[cacheIndex].unixTime = unixTime;
  cacheIndex = (cacheIndex + 1) % CACHE_SIZE;
}

// OPTIMASI 5: Async Cache Initialization
void asyncInitializeCache()
{
  if (!sdCardAvailable || cacheInitialized)
    return;

  if (!cacheInitInProgress)
  {
    cacheInitInProgress = true;
    cacheInitFileIndex = 0;
    for (int i = 0; i < CACHE_SIZE; i++)
    {
      duplicateCache[i].rfid[0] = '\0';
      duplicateCache[i].unixTime = 0;
    }
    cacheIndex = 0;
  }

  if (cacheInitFileIndex < 2)
  {
    selectSD();

    int fileNum = (cacheInitFileIndex == 0) ? currentQueueFile : (currentQueueFile == 0 ? MAX_QUEUE_FILES - 1 : currentQueueFile - 1);

    String filename = getQueueFileName(fileNum);

    if (sd.exists(filename.c_str()) && file.open(filename.c_str(), O_RDONLY))
    {
      char line[128];
      if (file.available())
        file.fgets(line, sizeof(line));

      struct TempRecord
      {
        char rfid[11];
        unsigned long unixTime;
      };
      TempRecord tempRecords[MAX_RECORDS_PER_FILE];
      int tempCount = 0;

      while (file.fgets(line, sizeof(line)) > 0 && tempCount < MAX_RECORDS_PER_FILE)
      {
        String lineStr = String(line);
        lineStr.trim();
        if (lineStr.length() < 10)
          continue;

        int firstComma = lineStr.indexOf(',');
        int thirdComma = lineStr.lastIndexOf(',');

        if (firstComma > 0 && thirdComma > 0)
        {
          String fileRfid = lineStr.substring(0, firstComma);
          unsigned long fileUnixTime = lineStr.substring(thirdComma + 1).toInt();

          fileRfid.toCharArray(tempRecords[tempCount].rfid, 11);
          tempRecords[tempCount].unixTime = fileUnixTime;
          tempCount++;
        }
      }
      file.close();

      int cachePopulated = cacheIndex;
      for (int i = tempCount - 1; i >= 0 && cachePopulated < CACHE_SIZE; i--)
      {
        strncpy(duplicateCache[cachePopulated].rfid, tempRecords[i].rfid, 10);
        duplicateCache[cachePopulated].rfid[10] = '\0';
        duplicateCache[cachePopulated].unixTime = tempRecords[i].unixTime;
        cachePopulated++;
      }
      cacheIndex = cachePopulated;
    }

    deselectSD();
    cacheInitFileIndex++;
  }
  else
  {
    cacheInitialized = true;
    cacheInitInProgress = false;
  }
}

// OPTIMASI 4: Limited Duplicate Check (hanya 2 file terakhir)
bool isDuplicateLimited(const char *rfid, unsigned long currentUnixTime)
{
  // 1. Cek cache dulu (super cepat)
  if (isDuplicateInCache(rfid, currentUnixTime))
  {
    return true;
  }

  // 2. Hanya cek 2 file terakhir
  if (!sdCardAvailable)
    return false;

  while (isReadingQueue)
  {
    delay(10);
  }
  isReadingQueue = true;

  selectSD();
  bool found = false;

  for (int offset = 0; offset <= 1; offset++)
  {
    int fileIdx = (currentQueueFile - offset + MAX_QUEUE_FILES) % MAX_QUEUE_FILES;
    String filename = getQueueFileName(fileIdx);

    if (!sd.exists(filename.c_str()))
      continue;
    if (!file.open(filename.c_str(), O_RDONLY))
      continue;

    char line[128];
    if (file.available())
      file.fgets(line, sizeof(line));

    int linesRead = 0;
    while (file.fgets(line, sizeof(line)) > 0 && linesRead < MAX_DUPLICATE_CHECK_LINES)
    {
      String lineStr = String(line);
      lineStr.trim();

      int firstComma = lineStr.indexOf(',');
      int thirdComma = lineStr.lastIndexOf(',');

      if (firstComma > 0 && thirdComma > 0)
      {
        String fileRfid = lineStr.substring(0, firstComma);
        unsigned long fileUnixTime = lineStr.substring(thirdComma + 1).toInt();

        if (fileRfid.equals(rfid))
        {
          if (currentUnixTime - fileUnixTime < MIN_REPEAT_INTERVAL)
          {
            found = true;
            break;
          }
        }
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

// ========================================
// SAVE TO QUEUE
// ========================================
bool saveToQueue(const char *rfid, const char *timestamp, unsigned long unixTime)
{
  if (!sdCardAvailable)
    return false;

  while (isWritingToQueue || isReadingQueue)
  {
    delay(10);
  }
  isWritingToQueue = true;

  if (isDuplicateLimited(rfid, unixTime))
  {
    isWritingToQueue = false;
    return false;
  }

  selectSD();

  if (currentQueueFile < 0 || currentQueueFile >= MAX_QUEUE_FILES)
  {
    currentQueueFile = 0;
  }

  String currentFile = getQueueFileName(currentQueueFile);

  if (!sd.exists(currentFile.c_str()))
  {
    if (file.open(currentFile.c_str(), O_WRONLY | O_CREAT))
    {
      file.println("rfid,timestamp,device_id,unix_time");
      file.close();
    }
  }

  int currentCount = _countRecordsInternal(currentFile);

  if (currentCount >= MAX_RECORDS_PER_FILE)
  {
    currentQueueFile = (currentQueueFile + 1) % MAX_QUEUE_FILES;
    currentFile = getQueueFileName(currentQueueFile);

    if (sd.exists(currentFile.c_str()))
    {
      sd.remove(currentFile.c_str());
    }

    if (!file.open(currentFile.c_str(), O_WRONLY | O_CREAT))
    {
      deselectSD();
      isWritingToQueue = false;
      return false;
    }
    file.println("rfid,timestamp,device_id,unix_time");
    file.close();

    deselectSD();
    cacheInitialized = false;
    asyncInitializeCache();
    selectSD();
  }

  if (!file.open(currentFile.c_str(), O_WRONLY | O_APPEND))
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

  addToCache(rfid, unixTime);

  isWritingToQueue = false;
  return true;
}

// ========================================
// SYNC FUNCTIONS
// ========================================
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
  time_t currentTime = time(nullptr);
  char line[128];

  if (file.available())
    file.fgets(line, sizeof(line));

  while (file.available() && *count < maxCount)
  {
    if (file.fgets(line, sizeof(line)) > 0)
    {
      String lineStr = String(line);
      lineStr.trim();
      if (lineStr.length() < 10)
        continue;

      int firstComma = lineStr.indexOf(',');
      int secondComma = lineStr.indexOf(',', firstComma + 1);
      int thirdComma = lineStr.indexOf(',', secondComma + 1);

      if (firstComma > 0 && secondComma > 0 && thirdComma > 0)
      {
        records[*count].rfid = lineStr.substring(0, firstComma);
        records[*count].timestamp = lineStr.substring(firstComma + 1, secondComma);
        records[*count].deviceId = lineStr.substring(secondComma + 1, thirdComma);
        records[*count].unixTime = lineStr.substring(thirdComma + 1).toInt();

        if (currentTime - records[*count].unixTime <= MAX_OFFLINE_AGE)
        {
          (*count)++;
        }
      }
    }
  }

  file.close();
  deselectSD();
  return *count > 0;
}

// OPTIMASI 6: Shorter HTTP Timeout
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
  http.setTimeout(10000);       // 10 detik (lebih pendek dari 30)
  http.setConnectTimeout(3000); // 3 detik untuk connect

  char url[80];
  strcpy_P(url, API_BASE_URL);
  strcat_P(url, PSTR("/api/presensi/sync-bulk"));

  http.begin(url);
  http.addHeader(F("Content-Type"), F("application/json"));

  char apiKey[32];
  strcpy_P(apiKey, API_SECRET_KEY);
  http.addHeader(F("X-API-KEY"), apiKey);

  DynamicJsonDocument doc(4096);
  JsonArray dataArray = doc.createNestedArray("data");

  for (int i = 0; i < validCount; i++)
  {
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

  if (responseCode == 200)
  {
    selectSD();
    sd.remove(filename.c_str());
    deselectSD();
    return true;
  }
  return false;
}

// OPTIMASI 3: Chunked Sync dengan State Machine
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

  int filesSynced = 0;

  while (syncCurrentFile < MAX_QUEUE_FILES &&
         filesSynced < MAX_SYNC_FILES_PER_CYCLE &&
         millis() - syncStartTime < MAX_SYNC_TIME)
  {

    String filename = getQueueFileName(syncCurrentFile);

    selectSD();
    bool exists = sd.exists(filename.c_str());
    deselectSD();

    if (exists)
    {
      int records = countRecordsInFile(filename);
      if (records > 0)
      {
        if (syncQueueFile(filename))
        {
          filesSynced++;
          invalidateRecordCountCache();
        }
      }
    }

    syncCurrentFile++;
  }

  // Selesai sync semua file
  if (syncCurrentFile >= MAX_QUEUE_FILES)
  {
    syncInProgress = false;
    syncCurrentFile = 0;
  }
}

// ========================================
// TIME MANAGEMENT
// ========================================
bool syncTimeWithFallback()
{
  const char *servers[] = {NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3};

  for (int i = 0; i < 3; i++)
  {
    char ntpServer[32];
    strcpy_P(ntpServer, servers[i]);
    configTime(GMT_OFFSET_SEC, 0, ntpServer);

    struct tm timeInfo;
    unsigned long start = millis();

    while (millis() - start < 2500)
    {
      if (getLocalTime(&timeInfo) && timeInfo.tm_year >= 120)
      {
        lastValidTime = mktime(&timeInfo);
        timeWasSynced = true;
        bootTime = millis();

        snprintf(messageBuffer, sizeof(messageBuffer), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
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
    time_t estimatedTime = lastValidTime + ((millis() - bootTime) / 1000);
    *timeInfo = *localtime(&estimatedTime);
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
  {
    syncTimeWithFallback();
  }
}

String getFormattedTimestamp()
{
  struct tm timeInfo;
  if (getTimeWithFallback(&timeInfo))
  {
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return String(buffer);
  }
  return "";
}

// ========================================
// NETWORK FUNCTIONS
// ========================================
bool connectToWiFi()
{
  WiFi.mode(WIFI_STA);

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
      WiFi.localIP().toString().toCharArray(messageBuffer, sizeof(messageBuffer));
      showOLED(F("WIFI OK"), messageBuffer);
      isOnline = true;
      delay(800);
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

// ========================================
// NON-BLOCKING WIFI RECONNECT
// ========================================
void processReconnect()
{
  switch (reconnectState)
  {
  case RECONNECT_IDLE:
    if (WiFi.status() != WL_CONNECTED &&
        millis() - lastReconnectAttempt >= RECONNECT_INTERVAL)
    {
      lastReconnectAttempt = millis();
      reconnectState = RECONNECT_TRYING_SSID1;
      reconnectRetryCount = 0;
      reconnectStartTime = millis();

      char ssid[16], password[16];
      strcpy_P(ssid, WIFI_SSID_1);
      strcpy_P(password, WIFI_PASSWORD_1);
      WiFi.begin(ssid, password);
    }
    break;

  case RECONNECT_TRYING_SSID1:
    if (millis() - reconnectStartTime >= RECONNECT_RETRY_DELAY)
    {
      reconnectStartTime = millis();

      if (WiFi.status() == WL_CONNECTED)
      {
        reconnectState = RECONNECT_SUCCESS;
      }
      else
      {
        reconnectRetryCount++;

        if (reconnectRetryCount >= MAX_RECONNECT_RETRY)
        {
          reconnectState = RECONNECT_TRYING_SSID2;
          reconnectRetryCount = 0;

          char ssid[16], password[16];
          strcpy_P(ssid, WIFI_SSID_2);
          strcpy_P(password, WIFI_PASSWORD_2);
          WiFi.begin(ssid, password);
        }
      }
    }
    break;

  case RECONNECT_TRYING_SSID2:
    if (millis() - reconnectStartTime >= RECONNECT_RETRY_DELAY)
    {
      reconnectStartTime = millis();

      if (WiFi.status() == WL_CONNECTED)
      {
        reconnectState = RECONNECT_SUCCESS;
      }
      else
      {
        reconnectRetryCount++;

        if (reconnectRetryCount >= MAX_RECONNECT_RETRY)
        {
          reconnectState = RECONNECT_FAILED;
        }
      }
    }
    break;

  case RECONNECT_SUCCESS:
    isOnline = true;

    if (pingAPI())
    {
      if (sdCardAvailable && !syncInProgress)
      {
        int pending = getCachedRecordCount();
        if (pending > 0)
        {
          chunkedSync();
        }
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

// ========================================
// RFID FUNCTIONS
// ========================================
void uidToString(uint8_t *uid, uint8_t length, char *output)
{
  if (length >= 4)
  {
    uint32_t value = ((uint32_t)uid[3] << 24) | ((uint32_t)uid[2] << 16) |
                     ((uint32_t)uid[1] << 8) | uid[0];
    sprintf(output, "%010lu", value);
  }
  else
  {
    sprintf(output, "%02X%02X", uid[0], uid[1]);
  }
}

// OPTIMASI 9: Kirim Presensi dengan Limited Check
bool kirimPresensi(const char *rfidUID, char *message)
{
  String timestamp = getFormattedTimestamp();
  time_t currentUnixTime = time(nullptr);

  if (sdCardAvailable)
  {
    // Gunakan isDuplicateLimited (hanya cek 2 file terakhir)
    if (isDuplicateLimited(rfidUID, currentUnixTime))
    {
      strcpy(message, "CUKUP SEKALI!");
      return false;
    }

    if (saveToQueue(rfidUID, timestamp.c_str(), currentUnixTime))
    {
      strcpy(message, "DATA TERSIMPAN");
      invalidateRecordCountCache();
      return true;
    }
    else
    {
      strcpy(message, "SD CARD ERROR");
      return false;
    }
  }

  strcpy(message, "NO WIFI & SD");
  return false;
}

// ========================================
// DISPLAY FUNCTIONS
// ========================================
void showOLED(const __FlashStringHelper *line1, const char *line2)
{
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
  const int progressStep = 8;
  const int progressWidth = 80;
  int delayPerStep = durationMs / (progressWidth / progressStep);
  int startX = (SCREEN_WIDTH - progressWidth) / 2;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  int16_t x, y;
  uint16_t w, h;
  display.getTextBounds(message, 0, 0, &x, &y, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 20);
  display.println(message);
  display.display();

  for (int i = 0; i <= progressWidth; i += progressStep)
  {
    display.fillRect(startX, 40, i, 4, WHITE);
    display.display();
    delay(delayPerStep);
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
  const char version[] PROGMEM = "v2.2.2 ULTIMATE";

  const int titleLength = 7;
  const int titleX = (SCREEN_WIDTH - (titleLength * 12)) / 2;
  const int sub1X = (SCREEN_WIDTH - 15 * 6) / 2;
  const int sub2X = (SCREEN_WIDTH - 6 * 6) / 2;
  const int verX = (SCREEN_WIDTH - 16 * 6) / 2;

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

// OPTIMASI 10: Update Display State
void updateCurrentDisplayState()
{
  currentDisplay.isOnline = (WiFi.status() == WL_CONNECTED);

  struct tm timeInfo;
  if (getTimeWithFallback(&timeInfo))
  {
    snprintf(currentDisplay.time, sizeof(currentDisplay.time),
             "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
  }

  if (sdCardAvailable)
  {
    currentDisplay.pendingRecords = getCachedRecordCount();
  }
  else
  {
    currentDisplay.pendingRecords = 0;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    long rssi = WiFi.RSSI();
    currentDisplay.wifiSignal = (rssi > -67) ? 4 : (rssi > -70) ? 3
                                               : (rssi > -80)   ? 2
                                               : (rssi > -90)   ? 1
                                                                : 0;
  }
  else
  {
    currentDisplay.wifiSignal = 0;
  }
}

// OPTIMASI 2: Display Buffer untuk Reduce Redraw
void updateStandbySignal()
{
  // Hanya update jika ada perubahan
  if (memcmp(&currentDisplay, &previousDisplay, sizeof(DisplayState)) != 0)
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);

    display.setCursor(2, 2);
    if (reconnectState == RECONNECT_TRYING_SSID1 ||
        reconnectState == RECONNECT_TRYING_SSID2)
    {
      display.print(F("RECONN"));
      for (int i = 0; i < (millis() / 500) % 4; i++)
        display.print('.');
    }
    else
    {
      display.print(currentDisplay.isOnline ? F("ONLINE") : F("OFFLINE"));
    }

    const char *tapText = "TAP KARTU";
    int16_t x1, y1;
    uint16_t w1, h1;
    display.getTextBounds(tapText, 0, 0, &x1, &y1, &w1, &h1);
    display.setCursor((SCREEN_WIDTH - w1) / 2, 20);
    display.print(tapText);

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
        int h = 2 + i * 2;
        int x = SCREEN_WIDTH - 18 + i * 5;
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

// ========================================
// BUZZER FUNCTIONS
// ========================================
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

// ========================================
// ERROR HANDLER
// ========================================
void fatalError(const __FlashStringHelper *errorMessage)
{
  char errMsg[32];
  strncpy_P(errMsg, (const char *)errorMessage, sizeof(errMsg) - 1);
  errMsg[31] = '\0';
  showOLED((const __FlashStringHelper *)errMsg, "RESTART...");
  playToneError();
  delay(3000);
  ESP.restart();
}

// OPTIMASI 8: Fast RFID Handler
void handleRFIDScan()
{
  deselectSD();
  digitalWrite(PIN_RFID_SS, LOW);

  char rfidBuffer[11];
  uidToString(rfidReader.uid.uidByte, rfidReader.uid.size, rfidBuffer);

  // Debounce check
  if (strcmp(rfidBuffer, lastUID) == 0 &&
      millis() - lastScanTime < DEBOUNCE_TIME)
  {
    rfidReader.PICC_HaltA();
    rfidReader.PCD_StopCrypto1();
    digitalWrite(PIN_RFID_SS, HIGH);
    return;
  }

  strcpy(lastUID, rfidBuffer);
  lastScanTime = millis();

  // Quick feedback
  showOLED(F("RFID"), rfidBuffer);
  playToneNotify();

  // Process attendance
  char message[32];
  bool success = kirimPresensi(rfidBuffer, message);

  showOLED(success ? F("BERHASIL") : F("INFO"), message);
  success ? playToneSuccess() : playToneError();
  delay(500);

  rfidReader.PICC_HaltA();
  rfidReader.PCD_StopCrypto1();
  digitalWrite(PIN_RFID_SS, HIGH);

  invalidateRecordCountCache();
}

// ========================================
// SETUP
// ========================================
void setup()
{
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

  showProgress(F("INIT SD CARD"), 1500);
  sdCardAvailable = initSDCard();

  if (sdCardAvailable)
  {
    showOLED(F("SD CARD"), "TERSEDIA");
    playToneSuccess();
    delay(800);

    showProgress(F("LOAD CACHE"), 1000);
    asyncInitializeCache(); // Mulai async initialization

    int pending = getCachedRecordCount();
    if (pending > 0)
    {
      snprintf(messageBuffer, sizeof(messageBuffer), "%d TERSISA", pending);
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

  showProgress(F("CONNECTING WIFI"), 1500);
  if (!connectToWiFi())
  {
    fatalError(F("NO WIFI"));
  }
  else
  {
    showProgress(F("PING API"), 1000);
    int apiRetryCount = 0;
    while (!pingAPI() && apiRetryCount < 3)
    {
      apiRetryCount++;
      snprintf(messageBuffer, sizeof(messageBuffer), "Retry %d/3", apiRetryCount);
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
        int pending = getCachedRecordCount();
        if (pending > 0)
        {
          snprintf(messageBuffer, sizeof(messageBuffer), "%d records", pending);
          showOLED(F("SYNC DATA"), messageBuffer);
          delay(1000);
          chunkedSync(); // Gunakan chunked sync
        }
      }
    }
    else
    {
      fatalError(F("API GAGAL"));
    }
  }

  showProgress(F("INIT RFID"), 1000);
  rfidReader.PCD_Init();
  delay(100);
  digitalWrite(PIN_RFID_SS, HIGH);

  byte version = rfidReader.PCD_ReadRegister(rfidReader.VersionReg);
  if (version == 0x00 || version == 0xFF)
  {
    fatalError(F("RC522 GAGAL"));
  }

  showOLED(F("SISTEM SIAP"), isOnline ? "ONLINE" : "OFFLINE");
  playToneSuccess();

  bootTime = millis();
  lastSyncTime = millis();
  lastTimeSyncAttempt = millis();
  lastReconnectAttempt = millis();
  lastDisplayUpdate = millis();
  lastPeriodicCheck = millis();
}

// ========================================
// OPTIMASI 7: LOOP dengan Task Scheduling
// ========================================
void loop()
{
  unsigned long currentMillis = millis();

  // Task 1: RFID Reading (Highest Priority - Always Check)
  if (rfidReader.PICC_IsNewCardPresent() && rfidReader.PICC_ReadCardSerial())
  {
    handleRFIDScan();
    return; // Skip tugas lain saat ada scan
  }

  // Task 2: Display Update (Medium Priority - 500ms interval)
  if (currentMillis - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL)
  {
    lastDisplayUpdate = currentMillis;
    updateCurrentDisplayState();
    updateStandbySignal(); // Hanya update jika ada perubahan
  }

  // Task 3: Periodic Background Tasks (Low Priority - 1s interval)
  if (currentMillis - lastPeriodicCheck >= PERIODIC_CHECK_INTERVAL)
  {
    lastPeriodicCheck = currentMillis;

    // Async cache init jika belum selesai
    if (!cacheInitialized)
    {
      asyncInitializeCache();
    }

    // Chunked sync
    if (syncInProgress || (currentMillis - lastSyncTime >= SYNC_INTERVAL))
    {
      if (!syncInProgress)
        lastSyncTime = currentMillis;
      chunkedSync();
    }

    // WiFi reconnect
    processReconnect();

    // Time sync
    periodicTimeSync();
  }

  // Sleep mode check
  struct tm timeInfo;
  if (getTimeWithFallback(&timeInfo))
  {
    int h = timeInfo.tm_hour;

    if (h >= SLEEP_START_HOUR || h < SLEEP_END_HOUR)
    {
      showOLED(F("SLEEP MODE"), "...");
      delay(1000);

      int targetHour = SLEEP_END_HOUR;
      int currentHour = timeInfo.tm_hour;
      int currentMinute = timeInfo.tm_min;
      int currentSecond = timeInfo.tm_sec;

      int sleepSeconds;

      if (currentHour >= SLEEP_START_HOUR)
      {
        sleepSeconds = ((24 - currentHour) * 3600) +
                       (targetHour * 3600) -
                       (currentMinute * 60 + currentSecond);
      }
      else
      {
        sleepSeconds = ((targetHour - currentHour) * 3600) -
                       (currentMinute * 60 + currentSecond);
      }

      if (sleepSeconds < 60)
        sleepSeconds = 60;
      if (sleepSeconds > 43200)
        sleepSeconds = 43200;

      snprintf(messageBuffer, sizeof(messageBuffer), "%dh %dm",
               sleepSeconds / 3600, (sleepSeconds % 3600) / 60);
      showOLED(F("SLEEP FOR"), messageBuffer);
      delay(2000);

      esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
      esp_deep_sleep_start();
    }
  }
}