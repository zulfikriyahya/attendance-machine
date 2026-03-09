/*
 * ======================================================================================
 * SISTEM PRESENSI PINTAR (RFID) - QUEUE SYSTEM v2.2.5
 * ======================================================================================
 * Device  : ESP32-C3 Super Mini
 * Author  : Yahya Zulfikri
 * Created : Juli 2025
 * Updated : Maret 2026
 * Version : 2.2.5
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

// TIMING CONSTANTS
#define DEBOUNCE_TIME 150UL
#define SYNC_INTERVAL 300000UL
#define MAX_OFFLINE_AGE 2592000UL
#define MIN_REPEAT_INTERVAL 1800UL
#define TIME_SYNC_INTERVAL 3600000UL
#define RECONNECT_INTERVAL 300000UL
#define RECONNECT_TIMEOUT 15000UL
#define MAX_SYNC_TIME 15000UL
#define DISPLAY_UPDATE_INTERVAL 1000UL
#define PERIODIC_CHECK_INTERVAL 1000UL
#define OLED_SCHEDULE_CHECK_INTERVAL 60000UL
#define RFID_FEEDBACK_DISPLAY_MS 1800UL
#define SD_REDETECT_INTERVAL 30000UL
#define MAX_TIME_ESTIMATE_AGE 43200UL

// QUEUE CONFIG
#define MAX_RECORDS_PER_FILE 25
#define MAX_QUEUE_FILES 2000
#define MAX_SYNC_FILES_PER_CYCLE 5
#define MAX_DUPLICATE_CHECK_LINES 100
#define QUEUE_WARN_THRESHOLD 1600

// SCHEDULE CONFIG
#define SLEEP_START_HOUR 18
#define SLEEP_END_HOUR 5
#define OLED_DIM_START_HOUR 8
#define OLED_DIM_END_HOUR 14
#define GMT_OFFSET_SEC 25200L

// NETWORK CONFIG
const char WIFI_SSID_1[] PROGMEM = "SSID_WIFI_1";
const char WIFI_SSID_2[] PROGMEM = "SSID_WIFI_2";
const char WIFI_PASSWORD_1[] PROGMEM = "PasswordWifi1";
const char WIFI_PASSWORD_2[] PROGMEM = "PasswordWifi2";
const char API_BASE_URL[] PROGMEM = "https://zedlabs.id";
const char API_SECRET_KEY[] PROGMEM = "SecretAPIToken";
const char NTP_SERVER_1[] PROGMEM = "pool.ntp.org";
const char NTP_SERVER_2[] PROGMEM = "time.google.com";
const char NTP_SERVER_3[] PROGMEM = "id.pool.ntp.org";

// RTC MEMORY
RTC_DATA_ATTR time_t lastValidTime = 0;
RTC_DATA_ATTR bool timeWasSynced = false;
RTC_DATA_ATTR unsigned long bootTime = 0;
RTC_DATA_ATTR bool bootTimeSet = false;
RTC_DATA_ATTR int currentQueueFile = 0;

// HARDWARE OBJECTS
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 rfidReader(PIN_RFID_SS, PIN_RFID_RST);
SdFat sd;
FsFile file;

// GLOBAL STATE
char lastUID[11] = "";
unsigned long lastScanTime = 0;
unsigned long lastSyncTime = 0;
unsigned long lastTimeSyncAttempt = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastPeriodicCheck = 0;
unsigned long lastOLEDScheduleCheck = 0;
unsigned long lastSDRedetect = 0;
char messageBuffer[64];
bool isOnline = false;
bool sdCardAvailable = false;
char deviceId[20] = "";
bool oledIsOn = true;

// CACHE
int cachedPendingRecords = 0;
bool pendingCacheDirty = true;
int cachedQueueFileCount = 0;

// SD MUTEX
volatile bool sdBusy = false;

// RFID VALIDATION RESULT
enum RfidValidResult
{
    RFID_VALID,
    RFID_INVALID,
    RFID_UNREACHABLE
};

// DISPLAY BUFFER
struct DisplayState
{
    bool isOnline;
    char time[6];
    int pendingRecords;
    int wifiSignal;
};

DisplayState currentDisplay = {false, "00:00", 0, 0};
DisplayState previousDisplay = {false, "--:--", -1, -1};

// SYNC STATE
int syncCurrentFile = 0;
bool syncInProgress = false;
unsigned long syncStartTime = 0;

// RECONNECT STATE MACHINE
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
void offlineBootFallback();
void handleRFIDScan();
void updateCurrentDisplayState();
void updateStandbySignal();
void checkOLEDSchedule();
bool displayStateChanged();
bool reinitSDCard();
bool isTimeValid();
bool getTimeWithFallback(struct tm *timeInfo);
bool syncTimeWithFallback();
void checkSDHealth();
void refreshPendingCache();
RfidValidResult validateRfidOnline(const char *rfid);

// ========================================
// SD MUTEX
// ========================================
inline void acquireSD()
{
    while (sdBusy)
        delay(5);
    sdBusy = true;
}
inline void releaseSD() { sdBusy = false; }

// ========================================
// SPI CS
// ========================================
inline void selectSD()
{
    digitalWrite(PIN_RFID_SS, HIGH);
    digitalWrite(PIN_SD_CS, LOW);
}
inline void deselectSD() { digitalWrite(PIN_SD_CS, HIGH); }

// ========================================
// OLED CONTROL
// ========================================
void turnOffOLED()
{
    if (!oledIsOn)
        return;
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    oledIsOn = false;
}

void turnOnOLED()
{
    if (oledIsOn)
        return;
    display.ssd1306_command(SSD1306_DISPLAYON);
    oledIsOn = true;
    memset(&previousDisplay, 0xFF, sizeof(previousDisplay));
}

void checkOLEDSchedule()
{
    unsigned long now = millis();
    if (now - lastOLEDScheduleCheck < OLED_SCHEDULE_CHECK_INTERVAL)
        return;
    lastOLEDScheduleCheck = now;
    struct tm timeInfo;
    if (!getTimeWithFallback(&timeInfo))
        return;
    int h = timeInfo.tm_hour;
    if (h >= OLED_DIM_START_HOUR && h < OLED_DIM_END_HOUR)
        turnOffOLED();
    else
        turnOnOLED();
}

// ========================================
// TIME
// ========================================
bool isTimeValid()
{
    struct tm timeInfo;
    if (getLocalTime(&timeInfo) && timeInfo.tm_year >= 120)
        return true;
    if (!timeWasSynced || lastValidTime == 0 || !bootTimeSet)
        return false;
    return ((millis() - bootTime) / 1000) <= MAX_TIME_ESTIMATE_AGE;
}

bool getTimeWithFallback(struct tm *timeInfo)
{
    if (getLocalTime(timeInfo) && timeInfo->tm_year >= 120)
        return true;
    if (!timeWasSynced || lastValidTime == 0 || !bootTimeSet)
        return false;
    unsigned long elapsed = (millis() - bootTime) / 1000;
    if (elapsed > MAX_TIME_ESTIMATE_AGE)
        return false;
    time_t est = lastValidTime + elapsed;
    *timeInfo = *localtime(&est);
    return true;
}

String getFormattedTimestamp()
{
    struct tm timeInfo;
    if (!getTimeWithFallback(&timeInfo))
        return "";
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return String(buffer);
}

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
                if (!bootTimeSet)
                {
                    bootTime = millis();
                    bootTimeSet = true;
                }
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

void periodicTimeSync()
{
    if (millis() - lastTimeSyncAttempt < TIME_SYNC_INTERVAL)
        return;
    lastTimeSyncAttempt = millis();
    if (WiFi.status() == WL_CONNECTED)
        syncTimeWithFallback();
}

// ========================================
// SD CARD
// ========================================
bool reinitSDCard()
{
    if (file.isOpen())
        file.close();
    sd.end();
    delay(100);
    selectSD();
    delay(10);
    bool ok = sd.begin(PIN_SD_CS, SD_SCK_MHZ(10));
    deselectSD();
    return ok;
}

void checkSDHealth()
{
    if (millis() - lastSDRedetect < SD_REDETECT_INTERVAL)
        return;
    lastSDRedetect = millis();

    if (!sdCardAvailable)
    {
        acquireSD();
        bool recovered = reinitSDCard();
        releaseSD();
        if (recovered)
        {
            sdCardAvailable = true;
            pendingCacheDirty = true;
            showOLED(F("SD CARD"), "TERBACA KEMBALI");
            playToneSuccess();
            delay(800);
        }
        return;
    }

    acquireSD();
    selectSD();
    bool healthy = sd.vol()->fatType() > 0;
    deselectSD();
    releaseSD();

    if (!healthy)
    {
        sdCardAvailable = false;
        showOLED(F("SD CARD"), "TERLEPAS!");
        playToneError();
        delay(800);
    }
}

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
    cachedQueueFileCount = 0;
    selectSD();
    for (int i = 0; i < MAX_QUEUE_FILES; i++)
    {
        String filename = getQueueFileName(i);
        if (!sd.exists(filename.c_str()))
            continue;
        cachedQueueFileCount++;
        if (!file.open(filename.c_str(), O_RDONLY))
            continue;
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
    deselectSD();
    return total;
}

void refreshPendingCache()
{
    if (!pendingCacheDirty)
        return;
    cachedPendingRecords = countAllOfflineRecords();
    pendingCacheDirty = false;
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
            int count = 0;
            if (file.open(filename.c_str(), O_RDONLY))
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

// ========================================
// DUPLICATE CHECK (SD already acquired)
// ========================================
bool isDuplicateInternal(const char *rfid, unsigned long currentUnixTime)
{
    bool found = false;
    for (int offset = 0; offset <= 1 && !found; offset++)
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
            int c1 = lineStr.indexOf(',');
            int c3 = lineStr.lastIndexOf(',');
            if (c1 > 0 && c3 > c1)
            {
                String fileRfid = lineStr.substring(0, c1);
                unsigned long fileUnixTime = lineStr.substring(c3 + 1).toInt();
                if (fileRfid.equals(rfid) && (currentUnixTime - fileUnixTime) < MIN_REPEAT_INTERVAL)
                {
                    found = true;
                    break;
                }
            }
            linesRead++;
        }
        file.close();
    }
    return found;
}

// ========================================
// SAVE TO QUEUE
// ========================================
bool saveToQueue(const char *rfid, const char *timestamp, unsigned long unixTime)
{
    if (!sdCardAvailable)
        return false;
    if (sdBusy)
        return false;

    acquireSD();
    selectSD();

    if (isDuplicateInternal(rfid, unixTime))
    {
        deselectSD();
        releaseSD();
        return false;
    }

    if (currentQueueFile < 0 || currentQueueFile >= MAX_QUEUE_FILES)
        currentQueueFile = 0;

    String currentFile = getQueueFileName(currentQueueFile);
    if (!sd.exists(currentFile.c_str()))
    {
        if (file.open(currentFile.c_str(), O_WRONLY | O_CREAT))
        {
            file.println("rfid,timestamp,device_id,unix_time");
            file.close();
        }
    }

    int currentCount = 0;
    if (file.open(currentFile.c_str(), O_RDONLY))
    {
        char line[128];
        if (file.available())
            file.fgets(line, sizeof(line));
        while (file.fgets(line, sizeof(line)) > 0)
        {
            if (strlen(line) > 10)
                currentCount++;
        }
        file.close();
    }

    if (currentCount >= MAX_RECORDS_PER_FILE)
    {
        currentQueueFile = (currentQueueFile + 1) % MAX_QUEUE_FILES;
        currentFile = getQueueFileName(currentQueueFile);
        if (sd.exists(currentFile.c_str()))
            sd.remove(currentFile.c_str());
        if (!file.open(currentFile.c_str(), O_WRONLY | O_CREAT))
        {
            deselectSD();
            releaseSD();
            return false;
        }
        file.println("rfid,timestamp,device_id,unix_time");
        file.close();
    }

    if (!file.open(currentFile.c_str(), O_WRONLY | O_APPEND))
    {
        deselectSD();
        releaseSD();
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
    releaseSD();
    pendingCacheDirty = true;
    return true;
}

// ========================================
// FAILED LOG
// ========================================
void appendFailedLog(const char *rfid, const char *timestamp, const char *reason)
{
    if (!sdCardAvailable)
        return;
    acquireSD();
    selectSD();
    if (file.open("/failed_log.csv", O_WRONLY | O_CREAT | O_APPEND))
    {
        if (file.size() == 0)
            file.println("rfid,timestamp,reason");
        file.print(rfid);
        file.print(",");
        file.print(timestamp);
        file.print(",");
        file.println(reason);
        file.sync();
        file.close();
    }
    deselectSD();
    releaseSD();
}

// ========================================
// RFID VALIDATION (online)
// ========================================
RfidValidResult validateRfidOnline(const char *rfid)
{
    if (WiFi.status() != WL_CONNECTED)
        return RFID_UNREACHABLE;

    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(5000);

    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/validate"));

    if (!http.begin(url))
        return RFID_UNREACHABLE;

    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    StaticJsonDocument<128> doc;
    doc["rfid"] = rfid;
    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    http.end();

    if (code == 200)
        return RFID_VALID;
    if (code == 404)
        return RFID_INVALID;
    return RFID_UNREACHABLE;
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
            int c1 = lineStr.indexOf(',');
            int c2 = lineStr.indexOf(',', c1 + 1);
            int c3 = lineStr.indexOf(',', c2 + 1);
            if (c1 > 0 && c2 > 0 && c3 > 0)
            {
                records[*count].rfid = lineStr.substring(0, c1);
                records[*count].timestamp = lineStr.substring(c1 + 1, c2);
                records[*count].deviceId = lineStr.substring(c2 + 1, c3);
                records[*count].unixTime = lineStr.substring(c3 + 1).toInt();
                if (records[*count].timestamp.length() == 0)
                {
                    appendFailedLog(records[*count].rfid.c_str(), "empty", "TIMESTAMP_KOSONG");
                }
                else if ((unsigned long)currentTime - records[*count].unixTime <= MAX_OFFLINE_AGE)
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

bool syncQueueFile(const String &filename)
{
    if (!sdCardAvailable || WiFi.status() != WL_CONNECTED)
        return false;

    OfflineRecord records[MAX_RECORDS_PER_FILE];
    int validCount = 0;

    if (!readQueueFile(filename, records, &validCount, MAX_RECORDS_PER_FILE) || validCount == 0)
    {
        acquireSD();
        selectSD();
        sd.remove(filename.c_str());
        deselectSD();
        releaseSD();
        pendingCacheDirty = true;
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

    StaticJsonDocument<4096> doc;
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

    if (responseCode == 200)
    {
        String body = http.getString();
        http.end();

        StaticJsonDocument<4096> res;
        if (deserializeJson(res, body) == DeserializationError::Ok)
        {
            JsonArray data = res["data"].as<JsonArray>();
            for (JsonObject item : data)
            {
                const char *status = item["status"] | "error";
                if (strcmp(status, "error") == 0)
                {
                    const char *rfid = item["rfid"] | "unknown";
                    const char *ts = item["timestamp"] | "unknown";
                    const char *msg = item["message"] | "UNKNOWN";
                    appendFailedLog(rfid, ts, msg);
                }
            }
        }

        acquireSD();
        selectSD();
        sd.remove(filename.c_str());
        deselectSD();
        releaseSD();
        pendingCacheDirty = true;
        return true;
    }

    http.end();
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

    int filesSynced = 0;
    while (syncCurrentFile < MAX_QUEUE_FILES &&
           filesSynced < MAX_SYNC_FILES_PER_CYCLE &&
           millis() - syncStartTime < MAX_SYNC_TIME)
    {
        String filename = getQueueFileName(syncCurrentFile);
        acquireSD();
        selectSD();
        bool exists = sd.exists(filename.c_str());
        deselectSD();
        releaseSD();

        if (exists)
        {
            int records = countRecordsInFile(filename);
            if (records > 0)
            {
                if (syncQueueFile(filename))
                    filesSynced++;
                else if (WiFi.status() != WL_CONNECTED)
                {
                    syncInProgress = false;
                    return;
                }
            }
            else
            {
                acquireSD();
                selectSD();
                sd.remove(filename.c_str());
                deselectSD();
                releaseSD();
                pendingCacheDirty = true;
            }
        }
        syncCurrentFile++;
        yield();
    }

    if (syncCurrentFile >= MAX_QUEUE_FILES)
    {
        syncInProgress = false;
        syncCurrentFile = 0;
        if (filesSynced > 0)
        {
            refreshPendingCache();
            if (cachedPendingRecords == 0)
            {
                showOLED(F("SYNC"), "SELESAI!");
                playToneSuccess();
                delay(500);
            }
        }
    }
}

// ========================================
// NETWORK
// ========================================
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
            display.setCursor((SCREEN_WIDTH - (int)strlen(ssid) * 6) / 2, 10);
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
            long rssi = WiFi.RSSI();
            snprintf(messageBuffer, sizeof(messageBuffer), "RSSI: %ld dBm", rssi);
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
    http.setTimeout(10000);
    http.setConnectTimeout(5000);
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

void offlineBootFallback()
{
    showOLED(F("NO WIFI"), "OFFLINE MODE");
    playToneError();
    delay(1500);
}

// ========================================
// RECONNECT STATE MACHINE
// ========================================
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
            refreshPendingCache();
            if (cachedPendingRecords > 0)
            {
                snprintf(messageBuffer, sizeof(messageBuffer), "%d RECORDS", cachedPendingRecords);
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

// ========================================
// RFID
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

bool kirimLangsung(const char *rfidUID, const char *timestamp, char *message)
{
    if (WiFi.status() != WL_CONNECTED)
        return false;
    HTTPClient http;
    http.setTimeout(10000);
    http.setConnectTimeout(5000);
    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi"));
    if (!http.begin(url))
        return false;
    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);
    StaticJsonDocument<256> doc;
    doc["rfid"] = rfidUID;
    doc["timestamp"] = timestamp;
    doc["device_id"] = deviceId;
    doc["sync_mode"] = false;
    String payload;
    serializeJson(doc, payload);
    int code = http.POST(payload);
    http.end();
    if (code == 200)
    {
        strcpy(message, "PRESENSI OK");
        return true;
    }
    if (code == 400)
    {
        strcpy(message, "CUKUP SEKALI!");
        return false;
    }
    if (code == 404)
    {
        strcpy(message, "RFID UNKNOWN");
        return false;
    }
    snprintf(message, 32, "SERVER ERR %d", code);
    return false;
}

bool kirimPresensi(const char *rfidUID, char *message)
{
    if (!isTimeValid())
    {
        strcpy(message, "WAKTU INVALID");
        return false;
    }

    String timestamp = getFormattedTimestamp();
    time_t currentUnixTime = time(nullptr);

    if (sdCardAvailable)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            RfidValidResult valid = validateRfidOnline(rfidUID);
            if (valid == RFID_INVALID)
            {
                strcpy(message, "RFID UNKNOWN");
                return false;
            }
        }

        if (saveToQueue(rfidUID, timestamp.c_str(), currentUnixTime))
        {
            strcpy(message, cachedQueueFileCount >= QUEUE_WARN_THRESHOLD ? "QUEUE HAMPIR PENUH!" : "DATA TERSIMPAN");
            return true;
        }

        acquireSD();
        selectSD();
        bool dup = isDuplicateInternal(rfidUID, currentUnixTime);
        deselectSD();
        releaseSD();

        strcpy(message, dup ? "CUKUP SEKALI!" : "SD CARD ERROR");
        return false;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        return kirimLangsung(rfidUID, timestamp.c_str(), message);
    }

    strcpy(message, "NO SD & WIFI");
    return false;
}

// ========================================
// DISPLAY
// ========================================
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
    buf[31] = '\0';
    showOLED(line1, buf);
}

void showProgress(const __FlashStringHelper *message, int durationMs)
{
    if (!oledIsOn)
        return;
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
    const char version[] PROGMEM = "v2.2.5";
    const int titleX = (SCREEN_WIDTH - (7 * 12)) / 2;
    const int sub1X = (SCREEN_WIDTH - 15 * 6) / 2;
    const int sub2X = (SCREEN_WIDTH - 6 * 6) / 2;
    const int verX = (SCREEN_WIDTH - 6 * 6) / 2;
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

bool displayStateChanged()
{
    return currentDisplay.isOnline != previousDisplay.isOnline ||
           currentDisplay.pendingRecords != previousDisplay.pendingRecords ||
           currentDisplay.wifiSignal != previousDisplay.wifiSignal ||
           strncmp(currentDisplay.time, previousDisplay.time, 6) != 0;
}

void updateCurrentDisplayState()
{
    currentDisplay.isOnline = (WiFi.status() == WL_CONNECTED);
    struct tm timeInfo;
    if (getTimeWithFallback(&timeInfo))
    {
        snprintf(currentDisplay.time, sizeof(currentDisplay.time), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
    }
    refreshPendingCache();
    currentDisplay.pendingRecords = cachedPendingRecords;
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

void updateStandbySignal()
{
    if (!oledIsOn)
        return;
    if (!displayStateChanged())
        return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(2, 2);
    if (reconnectState == RECONNECT_INIT_SSID1 || reconnectState == RECONNECT_TRYING_SSID1)
    {
        display.print(F("CONN SSID1"));
        for (int i = 0; i < (int)((millis() / 500) % 4); i++)
            display.print('.');
    }
    else if (reconnectState == RECONNECT_INIT_SSID2 || reconnectState == RECONNECT_TRYING_SSID2)
    {
        display.print(F("CONN SSID2"));
        for (int i = 0; i < (int)((millis() / 500) % 4); i++)
            display.print('.');
    }
    else if (syncInProgress)
    {
        display.print(F("SYNCING"));
        for (int i = 0; i < (int)((millis() / 500) % 4); i++)
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
            int bh = 2 + i * 2;
            int bx = SCREEN_WIDTH - 18 + i * 5;
            if (i < currentDisplay.wifiSignal)
                display.fillRect(bx, 10 - bh, 3, bh, WHITE);
            else
                display.drawRect(bx, 10 - bh, 3, bh, WHITE);
        }
    }
    display.display();
    memcpy(&previousDisplay, &currentDisplay, sizeof(DisplayState));
}

// ========================================
// BUZZER
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
    static const int melody[] = {2500, 3000, 2500, 3000};
    for (int i = 0; i < 4; i++)
    {
        tone(PIN_BUZZER, melody[i], 100);
        delay(150);
    }
    noTone(PIN_BUZZER);
}

// ========================================
// RFID SCAN HANDLER
// ========================================
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

    delay(RFID_FEEDBACK_DISPLAY_MS);
    if (wasOff)
        checkOLEDSchedule();

    rfidReader.PICC_HaltA();
    rfidReader.PCD_StopCrypto1();
    digitalWrite(PIN_RFID_SS, HIGH);
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
    snprintf(deviceId, sizeof(deviceId), "ESP32_%02X%02X", mac[4], mac[5]);
    for (int i = 0; deviceId[i]; i++)
        deviceId[i] = toupper(deviceId[i]);

    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

    showProgress(F("INIT SD CARD"), 1500);
    sdCardAvailable = initSDCard();

    if (sdCardAvailable)
    {
        showOLED(F("SD CARD"), "TERSEDIA");
        playToneSuccess();
        delay(800);
        refreshPendingCache();
        if (cachedPendingRecords > 0)
        {
            snprintf(messageBuffer, sizeof(messageBuffer), "%d TERSISA", cachedPendingRecords);
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
    bool wifiOk = connectToWiFi();

    if (!wifiOk)
    {
        offlineBootFallback();
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
                refreshPendingCache();
                if (cachedPendingRecords > 0)
                {
                    snprintf(messageBuffer, sizeof(messageBuffer), "%d records", cachedPendingRecords);
                    showOLED(F("SYNC DATA"), messageBuffer);
                    delay(1000);
                    chunkedSync();
                }
            }
        }
        else
        {
            showOLED(F("API GAGAL"), "OFFLINE MODE");
            playToneError();
            delay(1500);
        }
    }

    showProgress(F("INIT RFID"), 1000);
    rfidReader.PCD_Init();
    delay(100);
    digitalWrite(PIN_RFID_SS, HIGH);

    byte version = rfidReader.PCD_ReadRegister(rfidReader.VersionReg);
    if (version == 0x00 || version == 0xFF)
    {
        showOLED(F("RC522 GAGAL"), "RESTART...");
        playToneError();
        delay(3000);
        ESP.restart();
    }

    showOLED(F("SISTEM SIAP"), isOnline ? "ONLINE" : "OFFLINE");
    playToneSuccess();

    if (!bootTimeSet)
    {
        bootTime = millis();
        bootTimeSet = true;
    }

    lastSyncTime = millis();
    lastTimeSyncAttempt = millis();
    lastReconnectAttempt = millis();
    lastDisplayUpdate = millis();
    lastPeriodicCheck = millis();
    lastOLEDScheduleCheck = millis();
    lastSDRedetect = millis();

    delay(1000);
    checkOLEDSchedule();
}

// ========================================
// MAIN LOOP
// ========================================
void loop()
{
    unsigned long now = millis();

    checkOLEDSchedule();
    checkSDHealth();

    if (rfidReader.PICC_IsNewCardPresent() && rfidReader.PICC_ReadCardSerial())
    {
        handleRFIDScan();
        return;
    }

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
            {
                chunkedSync();
            }
            else if (now - lastSyncTime >= SYNC_INTERVAL)
            {
                refreshPendingCache();
                lastSyncTime = now;
                if (cachedPendingRecords > 0)
                    chunkedSync();
            }
        }
        periodicTimeSync();
    }

    struct tm timeInfo;
    if (getTimeWithFallback(&timeInfo))
    {
        int h = timeInfo.tm_hour;
        if (h >= SLEEP_START_HOUR || h < SLEEP_END_HOUR)
        {
            if (syncInProgress)
            {
                chunkedSync();
                return;
            }

            showOLED(F("SLEEP MODE"), "...");
            delay(1000);

            int sleepSeconds;
            if (h >= SLEEP_START_HOUR)
            {
                sleepSeconds = ((24 - h + SLEEP_END_HOUR) * 3600) - (timeInfo.tm_min * 60 + timeInfo.tm_sec);
            }
            else
            {
                sleepSeconds = ((SLEEP_END_HOUR - h) * 3600) - (timeInfo.tm_min * 60 + timeInfo.tm_sec);
            }
            if (sleepSeconds < 60)
                sleepSeconds = 60;
            if (sleepSeconds > 43200)
                sleepSeconds = 43200;

            snprintf(messageBuffer, sizeof(messageBuffer), "%d Jam %d Menit",
                     sleepSeconds / 3600, (sleepSeconds % 3600) / 60);
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
