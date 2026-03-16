/*
 * ======================================================================================
 * SISTEM PRESENSI PINTAR (RFID) - QUEUE SYSTEM v2.2.8 (With OTA)
 * ======================================================================================
 * Device  : ESP32-C3 Super Mini
 * Author  : Yahya Zulfikri
 * Created : Juli 2025
 * Updated : Maret 2026
 * Version : 2.2.8
 * ======================================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Wire.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>
#include <SdFat.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <Update.h>

// ========================================
// PIN DEFINITIONS
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

// ========================================
// TIMING CONSTANTS
// ========================================
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
#define WDT_TIMEOUT_SEC 60
#define OTA_CHECK_INTERVAL 10800000UL

// ========================================
// QUEUE CONFIG
// ========================================
#define MAX_RECORDS_PER_FILE 25
#define MAX_QUEUE_FILES 2000
#define MAX_SYNC_FILES_PER_CYCLE 5
#define MAX_DUPLICATE_CHECK_LINES 100
#define QUEUE_WARN_THRESHOLD 1600
#define METADATA_FILE "/queue_meta.txt"

// ========================================
// NVS BUFFER CONFIG
// ========================================
#define NVS_MAX_RECORDS 20
#define NVS_NAMESPACE "presensi"
#define NVS_KEY_COUNT "nvs_count"
#define NVS_KEY_PREFIX "rec_"

// ========================================
// OTA CONFIG
// ========================================
#define FIRMWARE_VERSION "2.2.8"

// ========================================
// SCHEDULE CONFIG
// ========================================
#define SLEEP_START_HOUR 18
#define SLEEP_END_HOUR 5
#define OLED_DIM_START_HOUR 8
#define OLED_DIM_END_HOUR 14
#define GMT_OFFSET_SEC 25200L

// ========================================
// NETWORK CONFIG
// ========================================
const char WIFI_SSID[] PROGMEM = "SSID_WIFI";
const char WIFI_PASSWORD[] PROGMEM = "PasswordWifi";
const char API_BASE_URL[] PROGMEM = "https://zedlabs.id";
const char API_SECRET_KEY[] PROGMEM = "SecretAPIToken";
const char NTP_SERVER_1[] PROGMEM = "pool.ntp.org";
const char NTP_SERVER_2[] PROGMEM = "time.google.com";
const char NTP_SERVER_3[] PROGMEM = "id.pool.ntp.org";

// ========================================
// RTC MEMORY
// ========================================
RTC_DATA_ATTR time_t lastValidTime = 0;
RTC_DATA_ATTR bool timeWasSynced = false;
RTC_DATA_ATTR unsigned long bootTime = 0;
RTC_DATA_ATTR bool bootTimeSet = false;
RTC_DATA_ATTR int currentQueueFile = 0;
RTC_DATA_ATTR bool rtcQueueFileValid = false;

// ========================================
// ENUMS
// ========================================
enum ReconnectState
{
    RECONNECT_IDLE,
    RECONNECT_INIT,
    RECONNECT_TRYING,
    RECONNECT_SUCCESS,
    RECONNECT_FAILED
};

// ========================================
// STRUCTS
// ========================================
struct Timers
{
    unsigned long lastScan;
    unsigned long lastSync;
    unsigned long lastTimeSync;
    unsigned long lastReconnect;
    unsigned long lastDisplayUpdate;
    unsigned long lastPeriodicCheck;
    unsigned long lastOLEDScheduleCheck;
    unsigned long lastSDRedetect;
    unsigned long lastNvsSync;
    unsigned long lastOtaCheck;
};

struct DisplayState
{
    bool isOnline;
    char time[6];
    int pendingRecords;
    int wifiSignal;
};

struct OfflineRecord
{
    char rfid[11];
    char timestamp[20];
    char deviceId[20];
    unsigned long unixTime;
};

struct SyncState
{
    int currentFile;
    bool inProgress;
    unsigned long startTime;
};

struct RfidFeedback
{
    bool active;
    unsigned long shownAt;
    bool wasOledOff;
};

struct OtaState
{
    bool updateAvailable;
    char version[16];
    char url[128];
};

// ========================================
// HARDWARE OBJECTS
// ========================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 rfidReader(PIN_RFID_SS, PIN_RFID_RST);
SdFat sd;
FsFile file;
Preferences prefs;

// ========================================
// GLOBAL STATE
// ========================================
Timers timers = {};
DisplayState currentDisplay = {false, "00:00", 0, 0};
DisplayState previousDisplay = {false, "--:--", -1, -1};
SyncState syncState = {0, false, 0};
RfidFeedback rfidFeedback = {false, 0, false};
OtaState otaState = {false, "", ""};

char lastUID[11] = "";
char deviceId[20] = "";
bool isOnline = false;
bool sdCardAvailable = false;
bool oledIsOn = true;

int cachedPendingRecords = 0;
bool pendingCacheDirty = true;
int cachedQueueFileCount = 0;

volatile bool sdBusy = false;
ReconnectState reconnectState = RECONNECT_IDLE;
unsigned long reconnectStartTime = 0;

// ========================================
// FUNCTION DECLARATIONS
// ========================================
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
void saveMetadata();
void loadMetadata();
void appendFailedLog(const char *rfid, const char *timestamp, const char *reason);
WiFiClientSecure &getSecureClient();
int nvsGetCount();
void nvsSetCount(int count);
bool nvsLoadRecord(int index, OfflineRecord &rec);
bool nvsSaveRecord(int index, const OfflineRecord &rec);
void nvsDeleteRecord(int index);
bool nvsSaveToBuffer(const char *rfid, const char *timestamp, unsigned long unixTime);
bool nvsSyncToServer();
bool nvsIsDuplicate(const char *rfid, unsigned long unixTime);
void checkOtaUpdate();
void performOtaUpdate();

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
// HTTPS CLIENT
// ========================================
WiFiClientSecure &getSecureClient()
{
    static WiFiClientSecure client;
    client.setInsecure();
    return client;
}

// ========================================
// OTA UPDATE
// ========================================
void checkOtaUpdate()
{
    if (WiFi.status() != WL_CONNECTED)
        return;
    if (millis() - timers.lastOtaCheck < OTA_CHECK_INTERVAL)
        return;
    timers.lastOtaCheck = millis();

    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(5000);

    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/firmware/check"));

    if (!http.begin(getSecureClient(), url))
        return;

    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"version\":\"%s\",\"device_id\":\"%s\"}",
             FIRMWARE_VERSION, deviceId);

    int code = http.POST(payload);
    if (code != 200)
    {
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body) != DeserializationError::Ok)
        return;

    bool hasUpdate = doc["update"] | false;
    const char *latestVer = doc["version"] | "";
    const char *binUrl = doc["url"] | "";

    if (!hasUpdate || strlen(latestVer) == 0 || strlen(binUrl) == 0)
        return;

    strncpy(otaState.version, latestVer, sizeof(otaState.version) - 1);
    otaState.version[sizeof(otaState.version) - 1] = '\0';
    strncpy(otaState.url, binUrl, sizeof(otaState.url) - 1);
    otaState.url[sizeof(otaState.url) - 1] = '\0';
    otaState.updateAvailable = true;

    char buf[20];
    snprintf(buf, sizeof(buf), "v%s TERSEDIA", otaState.version);
    showOLED(F("UPDATE"), buf);
    playToneNotify();
    delay(2000);
}

void performOtaUpdate()
{
    if (!otaState.updateAvailable)
        return;
    if (WiFi.status() != WL_CONNECTED)
        return;

    char buf[20];
    snprintf(buf, sizeof(buf), "v%s", otaState.version);
    showOLED(F("UPDATE OTA"), buf);
    delay(500);
    showOLED(F("MENGUNDUH"), "MOHON TUNGGU...");

    esp_task_wdt_delete(nullptr);
    esp_task_wdt_deinit();

    WiFiClientSecure otaClient;
    otaClient.setInsecure();

    HTTPClient http;
    http.begin(otaClient, otaState.url);
    http.addHeader(F("X-API-KEY"), API_SECRET_KEY);
    http.setTimeout(60000);

    int code = http.GET();
    if (code != 200)
    {
        snprintf(buf, sizeof(buf), "HTTP ERR %d", code);
        showOLED(F("UPDATE GAGAL"), buf);
        playToneError();
        http.end();
        otaState.updateAvailable = false;
        goto reinit_wdt;
    }

    {
        int totalSize = http.getSize();
        WiFiClient *stream = http.getStreamPtr();

        if (!Update.begin(totalSize))
        {
            showOLED(F("UPDATE GAGAL"), "NO SPACE");
            playToneError();
            http.end();
            otaState.updateAvailable = false;
            goto reinit_wdt;
        }

        uint8_t buff[1024];
        int written = 0;
        while (http.connected() && written < totalSize)
        {
            size_t available = stream->available();
            if (available)
            {
                int read = stream->readBytes(buff, min((size_t)sizeof(buff), available));
                Update.write(buff, read);
                written += read;
            }
            delay(1);
        }
        http.end();

        if (Update.end() && Update.isFinished())
        {
            showOLED(F("UPDATE OK"), "RESTART...");
            playToneSuccess();
            delay(2000);
            ESP.restart();
        }
        else
        {
            snprintf(buf, sizeof(buf), "ERR %d", Update.getError());
            showOLED(F("UPDATE GAGAL"), buf);
            playToneError();
            otaState.updateAvailable = false;
        }
    }

reinit_wdt:
    const esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true};
    esp_task_wdt_init(&wdtConfig);
    esp_task_wdt_add(nullptr);

    memset(&previousDisplay, 0xFF, sizeof(previousDisplay));
}

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
    if (now - timers.lastOLEDScheduleCheck < OLED_SCHEDULE_CHECK_INTERVAL)
        return;
    timers.lastOLEDScheduleCheck = now;
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

void getFormattedTimestamp(char *buf, size_t bufSize)
{
    struct tm timeInfo;
    if (!getTimeWithFallback(&timeInfo))
    {
        buf[0] = '\0';
        return;
    }
    strftime(buf, bufSize, "%Y-%m-%d %H:%M:%S", &timeInfo);
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
                char buf[6];
                snprintf(buf, sizeof(buf), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
                showOLED(F("WAKTU TERSYNC"), buf);
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
    if (millis() - timers.lastTimeSync < TIME_SYNC_INTERVAL)
        return;
    timers.lastTimeSync = millis();
    if (WiFi.status() == WL_CONNECTED)
        syncTimeWithFallback();
}

// ========================================
// NVS BUFFER
// ========================================
int nvsGetCount()
{
    prefs.begin(NVS_NAMESPACE, true);
    int c = prefs.getInt(NVS_KEY_COUNT, 0);
    prefs.end();
    return c;
}

void nvsSetCount(int count)
{
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putInt(NVS_KEY_COUNT, count);
    prefs.end();
}

bool nvsLoadRecord(int index, OfflineRecord &rec)
{
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, index);
    prefs.begin(NVS_NAMESPACE, true);
    size_t len = prefs.getBytesLength(key);
    if (len != sizeof(OfflineRecord))
    {
        prefs.end();
        return false;
    }
    prefs.getBytes(key, &rec, sizeof(OfflineRecord));
    prefs.end();
    return true;
}

bool nvsSaveRecord(int index, const OfflineRecord &rec)
{
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, index);
    prefs.begin(NVS_NAMESPACE, false);
    size_t written = prefs.putBytes(key, &rec, sizeof(OfflineRecord));
    prefs.end();
    return written == sizeof(OfflineRecord);
}

void nvsDeleteRecord(int index)
{
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, index);
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(key);
    prefs.end();
}

bool nvsIsDuplicate(const char *rfid, unsigned long unixTime)
{
    int count = nvsGetCount();
    for (int i = 0; i < count; i++)
    {
        OfflineRecord rec;
        if (!nvsLoadRecord(i, rec))
            continue;
        if (strcmp(rec.rfid, rfid) == 0 && (unixTime - rec.unixTime) < MIN_REPEAT_INTERVAL)
            return true;
    }
    return false;
}

bool nvsSaveToBuffer(const char *rfid, const char *timestamp, unsigned long unixTime)
{
    if (nvsIsDuplicate(rfid, unixTime))
        return false;
    int count = nvsGetCount();
    if (count >= NVS_MAX_RECORDS)
        return false;

    OfflineRecord rec;
    strncpy(rec.rfid, rfid, sizeof(rec.rfid) - 1);
    rec.rfid[sizeof(rec.rfid) - 1] = '\0';
    strncpy(rec.timestamp, timestamp, sizeof(rec.timestamp) - 1);
    rec.timestamp[sizeof(rec.timestamp) - 1] = '\0';
    strncpy(rec.deviceId, deviceId, sizeof(rec.deviceId) - 1);
    rec.deviceId[sizeof(rec.deviceId) - 1] = '\0';
    rec.unixTime = unixTime;

    if (!nvsSaveRecord(count, rec))
        return false;
    nvsSetCount(count + 1);
    return true;
}

bool nvsSyncToServer()
{
    int count = nvsGetCount();
    if (count == 0)
        return true;
    if (WiFi.status() != WL_CONNECTED)
        return false;

    HTTPClient http;
    http.setTimeout(30000);
    http.setConnectTimeout(10000);

    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/sync-bulk"));
    if (!http.begin(getSecureClient(), url))
        return false;

    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    DynamicJsonDocument doc(4096);
    JsonArray dataArray = doc.createNestedArray("data");
    for (int i = 0; i < count; i++)
    {
        OfflineRecord rec;
        if (!nvsLoadRecord(i, rec))
            continue;
        JsonObject obj = dataArray.createNestedObject();
        obj["rfid"] = rec.rfid;
        obj["timestamp"] = rec.timestamp;
        obj["device_id"] = rec.deviceId;
        obj["sync_mode"] = true;
    }

    String payload;
    serializeJson(doc, payload);
    doc.clear();

    esp_task_wdt_reset();
    int code = http.POST(payload);

    if (code == 200)
    {
        String body = http.getString();
        http.end();

        DynamicJsonDocument res(4096);
        if (deserializeJson(res, body) == DeserializationError::Ok)
        {
            JsonArray data = res["data"].as<JsonArray>();
            for (JsonObject item : data)
            {
                const char *status = item["status"] | "error";
                if (strcmp(status, "error") == 0)
                    appendFailedLog(item["rfid"] | "unknown", item["timestamp"] | "unknown", item["message"] | "UNKNOWN");
            }
        }

        for (int i = 0; i < count; i++)
            nvsDeleteRecord(i);
        nvsSetCount(0);
        return true;
    }

    http.end();
    return false;
}

// ========================================
// METADATA
// ========================================
void saveMetadata()
{
    if (!sdCardAvailable)
        return;
    selectSD();
    if (file.open(METADATA_FILE, O_WRONLY | O_CREAT | O_TRUNC))
    {
        file.print(cachedPendingRecords);
        file.print(",");
        file.println(currentQueueFile);
        file.sync();
        file.close();
    }
    deselectSD();
}

void loadMetadata()
{
    if (!sdCardAvailable)
        return;
    selectSD();
    if (!sd.exists(METADATA_FILE))
    {
        deselectSD();
        return;
    }
    if (!file.open(METADATA_FILE, O_RDONLY))
    {
        deselectSD();
        return;
    }
    char line[32];
    if (file.fgets(line, sizeof(line)) > 0)
    {
        char *comma = strchr(line, ',');
        if (comma)
        {
            *comma = '\0';
            cachedPendingRecords = atoi(line);
            currentQueueFile = atoi(comma + 1);
            rtcQueueFileValid = true;
            pendingCacheDirty = false;
        }
    }
    file.close();
    deselectSD();
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
    if (millis() - timers.lastSDRedetect < SD_REDETECT_INTERVAL)
        return;
    timers.lastSDRedetect = millis();

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

void getQueueFileName(int index, char *buf, size_t bufSize)
{
    snprintf(buf, bufSize, "/queue_%d.csv", index);
}

int countRecordsInFile(const char *filename)
{
    if (!sdCardAvailable)
        return 0;
    selectSD();
    if (!file.open(filename, O_RDONLY))
    {
        deselectSD();
        return 0;
    }
    int count = 0;
    char line[128];
    if (file.available())
        file.fgets(line, sizeof(line));
    while (file.fgets(line, sizeof(line)) > 0)
        if (strlen(line) > 10)
            count++;
    file.close();
    deselectSD();
    return count;
}

// [FIX WDT] Tambah esp_task_wdt_reset() di setiap iterasi loop
// untuk mencegah WDT timeout saat memindai hingga 2000 file
int countAllOfflineRecords()
{
    if (!sdCardAvailable)
        return 0;
    int total = 0;
    cachedQueueFileCount = 0;
    char filename[20];
    selectSD();
    for (int i = 0; i < MAX_QUEUE_FILES; i++)
    {
        esp_task_wdt_reset();
        getQueueFileName(i, filename, sizeof(filename));
        if (!sd.exists(filename))
            continue;
        cachedQueueFileCount++;
        if (!file.open(filename, O_RDONLY))
            continue;
        char line[128];
        if (file.available())
            file.fgets(line, sizeof(line));
        while (file.fgets(line, sizeof(line)) > 0)
        {
            esp_task_wdt_reset();
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
    saveMetadata();
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

    loadMetadata();

    if (!rtcQueueFileValid)
    {
        currentQueueFile = -1;
        char filename[20];
        for (int i = 0; i < MAX_QUEUE_FILES; i++)
        {
            esp_task_wdt_reset();
            getQueueFileName(i, filename, sizeof(filename));
            if (!sd.exists(filename))
            {
                if (file.open(filename, O_WRONLY | O_CREAT))
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
                if (file.open(filename, O_RDONLY))
                {
                    char line[128];
                    if (file.available())
                        file.fgets(line, sizeof(line));
                    while (file.fgets(line, sizeof(line)) > 0)
                    {
                        esp_task_wdt_reset();
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
        if (currentQueueFile == -1)
            currentQueueFile = 0;
        rtcQueueFileValid = true;
    }

    deselectSD();
    return true;
}

// ========================================
// DUPLICATE CHECK
// ========================================
// [FIX WDT] Tambah esp_task_wdt_reset() di setiap iterasi file
// untuk mencegah WDT timeout saat membaca beberapa file sekaligus
bool isDuplicateInternal(const char *rfid, unsigned long currentUnixTime)
{
    bool found = false;
    int checkRange = min(3, MAX_QUEUE_FILES);
    char filename[20];
    for (int offset = 0; offset < checkRange && !found; offset++)
    {
        esp_task_wdt_reset();
        int fileIdx = (currentQueueFile - offset + MAX_QUEUE_FILES) % MAX_QUEUE_FILES;
        getQueueFileName(fileIdx, filename, sizeof(filename));
        if (!sd.exists(filename))
            continue;
        if (!file.open(filename, O_RDONLY))
            continue;
        char line[128];
        if (file.available())
            file.fgets(line, sizeof(line));
        int linesRead = 0;
        while (file.fgets(line, sizeof(line)) > 0 && linesRead < MAX_DUPLICATE_CHECK_LINES)
        {
            esp_task_wdt_reset();
            int len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
            char *c1 = strchr(line, ',');
            char *c3 = strrchr(line, ',');
            if (c1 && c3 && c3 > c1)
            {
                *c1 = '\0';
                unsigned long fileUnixTime = strtoul(c3 + 1, nullptr, 10);
                if (strcmp(line, rfid) == 0 && (currentUnixTime - fileUnixTime) < MIN_REPEAT_INTERVAL)
                {
                    found = true;
                    file.close();
                    break;
                }
                *c1 = ',';
            }
            linesRead++;
        }
        if (file.isOpen())
            file.close();
    }
    return found;
}

// ========================================
// SAVE TO QUEUE
// ========================================
bool saveToQueue(const char *rfid, const char *timestamp, unsigned long unixTime)
{
    if (!sdCardAvailable || sdBusy)
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

    char currentFile[20];
    getQueueFileName(currentQueueFile, currentFile, sizeof(currentFile));

    if (!sd.exists(currentFile))
    {
        if (file.open(currentFile, O_WRONLY | O_CREAT))
        {
            file.println("rfid,timestamp,device_id,unix_time");
            file.close();
        }
    }

    int currentCount = 0;
    if (file.open(currentFile, O_RDONLY))
    {
        char line[128];
        if (file.available())
            file.fgets(line, sizeof(line));
        while (file.fgets(line, sizeof(line)) > 0)
        {
            esp_task_wdt_reset();
            if (strlen(line) > 10)
                currentCount++;
        }
        file.close();
    }

    if (currentCount >= MAX_RECORDS_PER_FILE)
    {
        int nextFile = (currentQueueFile + 1) % MAX_QUEUE_FILES;
        char nextFilename[20];
        getQueueFileName(nextFile, nextFilename, sizeof(nextFilename));

        if (sd.exists(nextFilename))
        {
            int nextCount = countRecordsInFile(nextFilename);
            if (nextCount > 0)
            {
                deselectSD();
                releaseSD();
                return false;
            }
            sd.remove(nextFilename);
        }

        currentQueueFile = nextFile;
        getQueueFileName(currentQueueFile, currentFile, sizeof(currentFile));
        if (!file.open(currentFile, O_WRONLY | O_CREAT))
        {
            deselectSD();
            releaseSD();
            return false;
        }
        file.println("rfid,timestamp,device_id,unix_time");
        file.close();
    }

    if (!file.open(currentFile, O_WRONLY | O_APPEND))
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

    cachedPendingRecords++;
    pendingCacheDirty = false;
    saveMetadata();
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
// SYNC FUNCTIONS
// ========================================
bool readQueueFile(const char *filename, OfflineRecord *records, int *count, int maxCount)
{
    if (!sdCardAvailable)
        return false;
    selectSD();
    if (!file.open(filename, O_RDONLY))
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
        esp_task_wdt_reset();
        if (file.fgets(line, sizeof(line)) > 0)
        {
            int len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
            if (len < 10)
                continue;

            char *c1 = strchr(line, ',');
            if (!c1)
                continue;
            char *c2 = strchr(c1 + 1, ',');
            if (!c2)
                continue;
            char *c3 = strchr(c2 + 1, ',');
            if (!c3)
                continue;

            int rfidLen = c1 - line;
            int tsLen = c2 - c1 - 1;
            int devLen = c3 - c2 - 1;
            if (rfidLen <= 0 || tsLen <= 0)
                continue;

            strncpy(records[*count].rfid, line, min(rfidLen, 10));
            records[*count].rfid[min(rfidLen, 10)] = '\0';
            strncpy(records[*count].timestamp, c1 + 1, min(tsLen, 19));
            records[*count].timestamp[min(tsLen, 19)] = '\0';
            strncpy(records[*count].deviceId, c2 + 1, min(devLen, 19));
            records[*count].deviceId[min(devLen, 19)] = '\0';
            records[*count].unixTime = strtoul(c3 + 1, nullptr, 10);

            if (records[*count].timestamp[0] == '\0')
                appendFailedLog(records[*count].rfid, "empty", "TIMESTAMP_KOSONG");
            else if ((unsigned long)currentTime - records[*count].unixTime <= MAX_OFFLINE_AGE)
                (*count)++;
        }
    }
    file.close();
    deselectSD();
    return *count > 0;
}

bool syncQueueFile(const char *filename)
{
    if (!sdCardAvailable || WiFi.status() != WL_CONNECTED)
        return false;

    OfflineRecord records[MAX_RECORDS_PER_FILE];
    int validCount = 0;

    if (!readQueueFile(filename, records, &validCount, MAX_RECORDS_PER_FILE) || validCount == 0)
    {
        acquireSD();
        selectSD();
        sd.remove(filename);
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
    if (!http.begin(getSecureClient(), url))
        return false;

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
    doc.clear();

    esp_task_wdt_reset();
    int responseCode = http.POST(payload);

    if (responseCode == 200)
    {
        String body = http.getString();
        http.end();

        DynamicJsonDocument res(4096);
        if (deserializeJson(res, body) == DeserializationError::Ok)
        {
            JsonArray data = res["data"].as<JsonArray>();
            for (JsonObject item : data)
            {
                const char *status = item["status"] | "error";
                if (strcmp(status, "error") == 0)
                    appendFailedLog(item["rfid"] | "unknown", item["timestamp"] | "unknown", item["message"] | "UNKNOWN");
            }
        }

        acquireSD();
        selectSD();
        sd.remove(filename);
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
        syncState.inProgress = false;
        return;
    }
    if (!syncState.inProgress)
    {
        syncState.inProgress = true;
        syncState.currentFile = 0;
        syncState.startTime = millis();
    }

    int filesSynced = 0;
    char filename[20];

    while (syncState.currentFile < MAX_QUEUE_FILES &&
           filesSynced < MAX_SYNC_FILES_PER_CYCLE &&
           millis() - syncState.startTime < MAX_SYNC_TIME)
    {
        getQueueFileName(syncState.currentFile, filename, sizeof(filename));
        acquireSD();
        selectSD();
        bool exists = sd.exists(filename);
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
                    syncState.inProgress = false;
                    return;
                }
            }
            else
            {
                acquireSD();
                selectSD();
                sd.remove(filename);
                deselectSD();
                releaseSD();
                pendingCacheDirty = true;
            }
        }
        syncState.currentFile++;
        yield();
        esp_task_wdt_reset();
    }

    if (syncState.currentFile >= MAX_QUEUE_FILES)
    {
        syncState.inProgress = false;
        syncState.currentFile = 0;
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
#if defined(WIFI_CONNECT_AP_BY_SIGNAL)
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
#endif
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);

    char ssid[32], password[32];
    strcpy_P(ssid, WIFI_SSID);
    strcpy_P(password, WIFI_PASSWORD);
    WiFi.begin(ssid, password);

    for (int retry = 0; retry < 20 && WiFi.status() != WL_CONNECTED; retry++)
    {
        esp_task_wdt_reset();
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
        char buf[20];
        snprintf(buf, sizeof(buf), "RSSI: %ld dBm", WiFi.RSSI());
        showOLED(F("WIFI OK"), buf);
        isOnline = true;
        delay(1500);
        return true;
    }

    isOnline = false;
    return false;
}

bool pingAPI()
{
    if (WiFi.status() != WL_CONNECTED)
        return false;
    HTTPClient http;
    http.setTimeout(5000);
    http.setConnectTimeout(3000);
    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/ping"));
    http.begin(getSecureClient(), url);
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
        if (WiFi.status() != WL_CONNECTED && millis() - timers.lastReconnect >= RECONNECT_INTERVAL)
        {
            timers.lastReconnect = millis();
            reconnectState = RECONNECT_INIT;
        }
        break;

    case RECONNECT_INIT:
        WiFi.disconnect(true);
        delay(100);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        WiFi.setSleep(WIFI_PS_MAX_MODEM);
        {
            char s[32], p[32];
            strcpy_P(s, WIFI_SSID);
            strcpy_P(p, WIFI_PASSWORD);
            WiFi.begin(s, p);
        }
        reconnectStartTime = millis();
        reconnectState = RECONNECT_TRYING;
        break;

    case RECONNECT_TRYING:
        if (WiFi.status() == WL_CONNECTED)
            reconnectState = RECONNECT_SUCCESS;
        else if (millis() - reconnectStartTime >= RECONNECT_TIMEOUT)
            reconnectState = RECONNECT_FAILED;
        break;

    case RECONNECT_SUCCESS:
        isOnline = true;
        syncTimeWithFallback();
        if (nvsGetCount() > 0)
            nvsSyncToServer();
        if (sdCardAvailable)
        {
            refreshPendingCache();
            if (cachedPendingRecords > 0)
            {
                char buf[20];
                snprintf(buf, sizeof(buf), "%d RECORDS", cachedPendingRecords);
                showOLED(F("SYNCING"), buf);
                syncState.inProgress = false;
                syncState.currentFile = 0;
                timers.lastSync = millis();
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
    if (!http.begin(getSecureClient(), url))
        return false;
    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"rfid\":\"%s\",\"timestamp\":\"%s\",\"device_id\":\"%s\",\"sync_mode\":false}",
             rfidUID, timestamp, deviceId);

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

    char timestamp[20];
    getFormattedTimestamp(timestamp, sizeof(timestamp));
    time_t currentUnixTime = time(nullptr);

    if (sdCardAvailable)
    {
        if (saveToQueue(rfidUID, timestamp, currentUnixTime))
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
        bool sent = kirimLangsung(rfidUID, timestamp, message);
        if (sent)
            return true;

        if (nvsIsDuplicate(rfidUID, currentUnixTime))
        {
            strcpy(message, "CUKUP SEKALI!");
            return false;
        }
        if (nvsSaveToBuffer(rfidUID, timestamp, currentUnixTime))
        {
            char buf[24];
            snprintf(buf, sizeof(buf), "BUFFER %d/%d", nvsGetCount(), NVS_MAX_RECORDS);
            strcpy(message, buf);
            return true;
        }
        strcpy(message, "BUFFER PENUH!");
        return false;
    }

    if (nvsIsDuplicate(rfidUID, currentUnixTime))
    {
        strcpy(message, "CUKUP SEKALI!");
        return false;
    }
    if (nvsSaveToBuffer(rfidUID, timestamp, currentUnixTime))
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "BUFFER %d/%d", nvsGetCount(), NVS_MAX_RECORDS);
        strcpy(message, buf);
        return true;
    }
    strcpy(message, "BUFFER PENUH!");
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
    if (!oledIsOn)
        return;
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
        esp_task_wdt_reset();
        display.fillRect(startX, 40, i, 4, WHITE);
        display.display();
        delay(delayPerStep);
    }
    esp_task_wdt_reset();
    delay(300);
}

void showStartupAnimation()
{
    static const char title[] = "ZEDLABS";
    static const char subtitle1[] = "INNOVATE BEYOND";
    static const char subtitle2[] = "LIMITS";
    static const char version[] = "v2.2.8";

    const int titleX = (SCREEN_WIDTH - (7 * 12)) / 2;
    const int sub1X = (SCREEN_WIDTH - 15 * 6) / 2;
    const int sub2X = (SCREEN_WIDTH - 6 * 6) / 2;
    const int verX = (SCREEN_WIDTH - 6 * 6) / 2;

    display.clearDisplay();
    display.setTextColor(WHITE);
    for (int x = -80; x <= titleX; x += 4)
    {
        esp_task_wdt_reset();
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(x, 5);
        display.println(title);
        display.setTextSize(1);
        display.setCursor(sub1X, 30);
        display.println(subtitle1);
        display.setCursor(sub2X, 40);
        display.println(subtitle2);
        display.display();
        delay(30);
    }
    esp_task_wdt_reset();
    delay(300);
    display.setTextSize(1);
    display.setCursor(verX, 55);
    display.print(version);
    display.display();
    for (int i = 0; i < 3; i++)
    {
        delay(300);
        display.print('.');
        display.display();
    }
    esp_task_wdt_reset();
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
        snprintf(currentDisplay.time, sizeof(currentDisplay.time), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
    refreshPendingCache();
    currentDisplay.pendingRecords = cachedPendingRecords + nvsGetCount();
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

    if (reconnectState == RECONNECT_INIT || reconnectState == RECONNECT_TRYING)
    {
        display.print(F("CONNECTING"));
        for (int i = 0; i < (int)((millis() / 500) % 4); i++)
            display.print('.');
    }
    else if (syncState.inProgress)
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
        char buf[16];
        snprintf(buf, sizeof(buf), "Q:%d", currentDisplay.pendingRecords);
        display.getTextBounds(buf, 0, 0, &x1, &y1, &w1, &h1);
        display.setCursor((SCREEN_WIDTH - w1) / 2, 50);
        display.print(buf);
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

    if (strcmp(rfidBuffer, lastUID) == 0 && millis() - timers.lastScan < DEBOUNCE_TIME)
    {
        rfidReader.PICC_HaltA();
        rfidReader.PCD_StopCrypto1();
        digitalWrite(PIN_RFID_SS, HIGH);
        return;
    }

    strcpy(lastUID, rfidBuffer);
    timers.lastScan = millis();

    bool wasOff = !oledIsOn;
    if (wasOff)
        turnOnOLED();

    showOLED(F("RFID"), rfidBuffer);
    playToneNotify();

    char message[32];
    bool success = kirimPresensi(rfidBuffer, message);

    showOLED(success ? F("BERHASIL") : F("INFO"), message);
    success ? playToneSuccess() : playToneError();

    rfidFeedback.active = true;
    rfidFeedback.shownAt = millis();
    rfidFeedback.wasOledOff = wasOff;

    rfidReader.PICC_HaltA();
    rfidReader.PCD_StopCrypto1();
    digitalWrite(PIN_RFID_SS, HIGH);
}

// ========================================
// SETUP
// ========================================
void setup()
{
    const esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true};
    esp_task_wdt_init(&wdtConfig);
    esp_task_wdt_add(nullptr);

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    pinMode(PIN_BUZZER, OUTPUT);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

    showStartupAnimation();
    playStartupMelody();
    esp_task_wdt_reset();

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
            char buf[20];
            snprintf(buf, sizeof(buf), "%d TERSISA", cachedPendingRecords);
            showOLED(F("DATA OFFLINE"), buf);
            delay(1000);
        }
    }
    else
    {
        showOLED(F("SD CARD"), "TIDAK ADA");
        playToneError();
        delay(1000);

        int nvsCount = nvsGetCount();
        if (nvsCount > 0)
        {
            char buf[20];
            snprintf(buf, sizeof(buf), "%d TERSISA", nvsCount);
            showOLED(F("NVS BUFFER"), buf);
            delay(1000);
        }
    }

    showProgress(F("CONNECTING WIFI"), 1500);
    bool wifiOk = connectToWiFi();
    esp_task_wdt_reset();

    if (!wifiOk)
    {
        offlineBootFallback();
    }
    else
    {
        showProgress(F("PING API"), 500);
        int apiRetryCount = 0;
        while (!pingAPI() && apiRetryCount < 3)
        {
            apiRetryCount++;
            char buf[12];
            snprintf(buf, sizeof(buf), "Retry %d/3", apiRetryCount);
            showOLED(F("API GAGAL"), buf);
            playToneError();
            delay(800);
            esp_task_wdt_reset();
        }
        if (isOnline)
        {
            showOLED(F("API OK"), "SYNCING TIME");
            playToneSuccess();
            delay(500);
            syncTimeWithFallback();
            esp_task_wdt_reset();

            int nvsCount = nvsGetCount();
            if (nvsCount > 0)
            {
                char buf[20];
                snprintf(buf, sizeof(buf), "%d NVS RECORDS", nvsCount);
                showOLED(F("SYNC NVS"), buf);
                delay(800);
                nvsSyncToServer();
                esp_task_wdt_reset();
            }

            if (sdCardAvailable)
            {
                refreshPendingCache();
                if (cachedPendingRecords > 0)
                {
                    char buf[20];
                    snprintf(buf, sizeof(buf), "%d records", cachedPendingRecords);
                    showOLED(F("SYNC DATA"), buf);
                    delay(1000);
                    chunkedSync();
                    esp_task_wdt_reset();
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

    unsigned long now = millis();
    timers.lastSync = now;
    timers.lastTimeSync = now;
    timers.lastReconnect = now;
    timers.lastDisplayUpdate = now;
    timers.lastPeriodicCheck = now;
    timers.lastOLEDScheduleCheck = now;
    timers.lastSDRedetect = now;
    timers.lastNvsSync = now;
    timers.lastOtaCheck = 0;

    delay(1000);
    checkOLEDSchedule();
}

// ========================================
// MAIN LOOP
// ========================================
void loop()
{
    esp_task_wdt_reset();
    unsigned long now = millis();

    if (rfidFeedback.active && now - rfidFeedback.shownAt >= RFID_FEEDBACK_DISPLAY_MS)
    {
        rfidFeedback.active = false;
        if (rfidFeedback.wasOledOff)
            checkOLEDSchedule();
        memset(&previousDisplay, 0xFF, sizeof(previousDisplay));
    }

    checkOLEDSchedule();
    checkSDHealth();

    if (rfidReader.PICC_IsNewCardPresent() && rfidReader.PICC_ReadCardSerial())
    {
        handleRFIDScan();
        return;
    }

    processReconnect();

    if (now - timers.lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL)
    {
        timers.lastDisplayUpdate = now;
        updateCurrentDisplayState();
        updateStandbySignal();
    }

    if (now - timers.lastPeriodicCheck >= PERIODIC_CHECK_INTERVAL)
    {
        timers.lastPeriodicCheck = now;
        if (WiFi.status() == WL_CONNECTED)
        {
            checkOtaUpdate();

            if (otaState.updateAvailable && !rfidFeedback.active)
                performOtaUpdate();

            if (nvsGetCount() > 0 && now - timers.lastNvsSync >= SYNC_INTERVAL)
            {
                timers.lastNvsSync = now;
                nvsSyncToServer();
            }

            if (sdCardAvailable)
            {
                if (syncState.inProgress)
                {
                    chunkedSync();
                }
                else if (now - timers.lastSync >= SYNC_INTERVAL)
                {
                    refreshPendingCache();
                    timers.lastSync = now;
                    if (cachedPendingRecords > 0)
                        chunkedSync();
                }
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
            if (syncState.inProgress)
            {
                chunkedSync();
                return;
            }

            showOLED(F("SLEEP MODE"), "...");
            delay(1000);

            int sleepSeconds;
            if (h >= SLEEP_START_HOUR)
                sleepSeconds = ((24 - h + SLEEP_END_HOUR) * 3600) - (timeInfo.tm_min * 60 + timeInfo.tm_sec);
            else
                sleepSeconds = ((SLEEP_END_HOUR - h) * 3600) - (timeInfo.tm_min * 60 + timeInfo.tm_sec);

            if (sleepSeconds < 60)
                sleepSeconds = 60;
            if (sleepSeconds > 43200)
                sleepSeconds = 43200;

            char buf[24];
            snprintf(buf, sizeof(buf), "%d Jam %d Menit", sleepSeconds / 3600, (sleepSeconds % 3600) / 60);
            showOLED(F("SLEEP FOR"), buf);
            delay(2000);

            display.clearDisplay();
            display.display();
            display.ssd1306_command(SSD1306_DISPLAYOFF);

            esp_task_wdt_deinit();
            esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
            esp_deep_sleep_start();

            const esp_task_wdt_config_t wdtConfig = {
            .timeout_ms = WDT_TIMEOUT_SEC * 1000,
            .idle_core_mask = 0,
            .trigger_panic = true
            };
            esp_task_wdt_init(&wdtConfig);
            esp_task_wdt_add(nullptr);
        }
    }
    yield();
}
