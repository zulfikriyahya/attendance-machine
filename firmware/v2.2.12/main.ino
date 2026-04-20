/*
 * ======================================================================================
 * SISTEM PRESENSI PINTAR (RFID) - QUEUE SYSTEM v2.2.12 (With OTA + RFID Local DB)
 * ======================================================================================
 * Device  : ESP32-C3 Super Mini
 * Author  : Yahya Zulfikri
 * Created : Juli 2025
 * Updated : Maret 2026
 * Version : 2.2.12
 * ======================================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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
#include <esp_ota_ops.h>

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
#define DISPLAY_UPDATE_INTERVAL 1000UL
#define PERIODIC_CHECK_INTERVAL 1000UL
#define OLED_SCHEDULE_CHECK_INTERVAL 60000UL
#define RFID_FEEDBACK_DISPLAY_MS 1800UL
#define SD_REDETECT_INTERVAL 30000UL
#define MAX_TIME_ESTIMATE_AGE 43200UL
#define WDT_TIMEOUT_SEC 60
#define WDT_SYNC_TIMEOUT_MS 180000UL
#define OTA_CHECK_INTERVAL 30000UL
#define RFID_DB_CHECK_INTERVAL 30000UL
#define SD_MUTEX_TIMEOUT_MS 5000UL

// ========================================
// SYNC CONFIG
// ========================================
#define MAX_RECORDS_PER_FILE 25
#define MAX_QUEUE_FILES 2000
#define MAX_DUPLICATE_CHECK_FILES 3
#define MAX_DUPLICATE_CHECK_LINES (MAX_RECORDS_PER_FILE + 1)
#define QUEUE_WARN_THRESHOLD 1600
#define METADATA_FILE "/queue_meta.txt"
#define MAX_SYNC_FILES_PER_CYCLE 5
#define MAX_SYNC_RETRIES 2
#define SYNC_RETRY_DELAY_MS 2000UL

// ========================================
// NVS BUFFER CONFIG
// ========================================
#define NVS_MAX_RECORDS 20
#define NVS_NAMESPACE "presensi"
#define NVS_KEY_COUNT "nvs_count"
#define NVS_KEY_PREFIX "rec_"
#define NVS_KEY_LAST_TIME "last_time"

// ========================================
// OTA CONFIG
// ========================================
#define FIRMWARE_VERSION "2.2.12"

// ========================================
// RFID LOCAL DB CONFIG
// ========================================
#define RFID_DB_FILE "/rfid_db.txt"
#define NVS_KEY_RFID_VER "rfid_db_ver"
#define RFID_CACHE_MAX 2000

// ========================================
// SCHEDULE CONFIG
// ========================================
#define SLEEP_START_HOUR 18
#define SLEEP_END_HOUR 5
#define OLED_DIM_START_HOUR 8
#define OLED_DIM_END_HOUR 12
#define GMT_OFFSET_SEC 25200L

// ========================================
// SIGNAL CONFIG
// ========================================
#define SIGNAL_THRESHOLD_WEAK -85
#define SIGNAL_THRESHOLD_CRITICAL -90

// ========================================
// NETWORK CONFIG
// ========================================
static const char WIFI_SSID[] PROGMEM = "SSID_WIFI";
static const char WIFI_PASSWORD[] PROGMEM = "PasswordWifi";
static const char API_SECRET_KEY[] PROGMEM = "SecretAPIToken";
static const char NTP_SERVER_1[] PROGMEM = "pool.ntp.org";
static const char NTP_SERVER_2[] PROGMEM = "time.google.com";
static const char NTP_SERVER_3[] PROGMEM = "id.pool.ntp.org";
static const char API_BASE_URL[] PROGMEM = "https://presensi.zedlabs.id";

// ========================================
// RTC MEMORY
// ========================================
RTC_DATA_ATTR time_t lastValidTime = 0;
RTC_DATA_ATTR bool timeWasSynced = false;
RTC_DATA_ATTR unsigned long bootTime = 0;
RTC_DATA_ATTR bool bootTimeSet = false;
RTC_DATA_ATTR int currentQueueFile = 0;
RTC_DATA_ATTR bool rtcQueueFileValid = false;
RTC_DATA_ATTR uint64_t sleepDurationSeconds = 0;

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

enum SaveResult
{
    SAVE_OK,
    SAVE_DUPLICATE,
    SAVE_QUEUE_FULL,
    SAVE_SD_ERROR
};

enum SyncFileResult
{
    SYNC_FILE_OK,
    SYNC_FILE_EMPTY,
    SYNC_FILE_HTTP_FAIL,
    SYNC_FILE_NO_WIFI
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
    unsigned long lastRfidDbCheck;
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
    int filesProcessed;
    int filesSucceeded;
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
SyncState syncState = {0, false, 0, 0, 0};
RfidFeedback rfidFeedback = {false, 0, false};
OtaState otaState = {false, "", ""};

char lastUID[11] = "";
char deviceId[20] = "";
bool isOnline = false;
bool sdCardAvailable = false;
bool oledIsOn = true;
bool wdtExtended = false;

int cachedPendingRecords = 0;
bool pendingCacheDirty = true;
int cachedQueueFileCount = 0;

volatile bool sdBusy = false;
ReconnectState reconnectState = RECONNECT_IDLE;
unsigned long reconnectStartTime = 0;

// ========================================
// RFID RAM CACHE
// ========================================
char **rfidCache = nullptr;
int rfidCacheCount = 0;
bool rfidCacheLoaded = false;

// ========================================
// FORWARD DECLARATIONS
// ========================================
void showOLED(const __FlashStringHelper *l1, const char *l2);
void showOLED(const __FlashStringHelper *l1, const __FlashStringHelper *l2);
void showProgress(const __FlashStringHelper *msg, int ms);
void playToneSuccess();
void playToneError();
void playToneNotify();
void playStartupMelody();
void offlineBootFallback();
void handleRFIDScan();
void updateCurrentDisplayState();
void updateStandbyDisplay();
void checkOLEDSchedule();
bool displayStateChanged();
bool reinitSDCard();
bool isTimeValid();
bool getTimeWithFallback(struct tm *ti);
bool syncTimeWithFallback();
void periodicTimeSync();
void checkSDHealth();
void refreshPendingCache();
void saveMetadata();
void loadMetadata();
void appendFailedLog(const char *rfid, const char *ts, const char *reason);
int nvsGetCount();
void nvsSetCount(int count);
bool nvsLoadRecord(int idx, OfflineRecord &rec);
bool nvsSaveRecord(int idx, const OfflineRecord &rec);
void nvsDeleteRecord(int idx);
bool nvsSaveToBuffer(const char *rfid, const char *ts, unsigned long t);
bool nvsSyncToServer();
bool nvsIsDuplicate(const char *rfid, unsigned long t);
void nvsSaveLastTime(time_t t);
time_t nvsLoadLastTime();
void checkOtaUpdate();
void performOtaUpdate();
unsigned long nvsGetRfidDbVer();
void nvsSetRfidDbVer(unsigned long ver);
unsigned long checkRfidDbVersion();
bool downloadRfidDb();
bool loadRfidCacheFromFile();
void freeRfidCache();
bool isRfidInCache(const char *rfid);
void checkAndUpdateRfidDb();
SaveResult saveToQueue(const char *rfid, const char *ts, unsigned long t);
bool isWifiConnected();
bool isSignalWeak();
bool isSignalCritical();
void extendWdtForSync();
void restoreWdtNormal();
SyncFileResult syncQueueFile(const char *filename);
bool syncQueueFileWithRetry(const char *filename);
void chunkedSync();
void getQueueFileName(int idx, char *buf, size_t sz);
int countRecordsInFileLocked(const char *filename);

// ========================================
// SD MUTEX
// Caller WAJIB releaseSD() setelah acquireSD().
// Semua fungsi internal SD tidak boleh memanggil acquireSD() lagi.
// ========================================
inline void acquireSD()
{
    unsigned long t0 = millis();
    while (sdBusy)
    {
        if (millis() - t0 > SD_MUTEX_TIMEOUT_MS)
            break;
        delay(2);
        esp_task_wdt_reset();
    }
    sdBusy = true;
}
inline void releaseSD()
{
    sdBusy = false;
}

// ========================================
// SPI DEVICE SELECT
// Hanya satu perangkat aktif dalam satu waktu.
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

// ========================================
// HTTP CLIENT
// Buat instance baru setiap request untuk hindari stale state TLS.
// ========================================
static WiFiClientSecure _httpClient;
WiFiClientSecure &getHttpClient()
{
    _httpClient.setInsecure();
    return _httpClient;
}

// ========================================
// SIGNAL & WIFI HELPERS
// ========================================
bool isWifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

bool isSignalWeak()
{
    return !isWifiConnected() || WiFi.RSSI() < SIGNAL_THRESHOLD_WEAK;
}

bool isSignalCritical()
{
    return !isWifiConnected() || WiFi.RSSI() < SIGNAL_THRESHOLD_CRITICAL;
}

// ========================================
// WDT HELPERS
// ========================================
void extendWdtForSync()
{
    if (wdtExtended)
        return;
    esp_task_wdt_delete(nullptr);
    esp_task_wdt_deinit();
    const esp_task_wdt_config_t cfg = {
        .timeout_ms = (uint32_t)WDT_SYNC_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic = true};
    esp_task_wdt_init(&cfg);
    esp_task_wdt_add(nullptr);
    wdtExtended = true;
}

void restoreWdtNormal()
{
    if (!wdtExtended)
        return;
    esp_task_wdt_delete(nullptr);
    esp_task_wdt_deinit();
    const esp_task_wdt_config_t cfg = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000u,
        .idle_core_mask = 0,
        .trigger_panic = true};
    esp_task_wdt_init(&cfg);
    esp_task_wdt_add(nullptr);
    wdtExtended = false;
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
    memset(previousDisplay.time, 0xFF, sizeof(previousDisplay.time));
    previousDisplay.pendingRecords = -1;
    previousDisplay.wifiSignal = -1;
    previousDisplay.isOnline = !currentDisplay.isOnline;
}

void checkOLEDSchedule()
{
    if (millis() - timers.lastOLEDScheduleCheck < OLED_SCHEDULE_CHECK_INTERVAL)
        return;
    timers.lastOLEDScheduleCheck = millis();
    struct tm ti;
    if (!getTimeWithFallback(&ti))
        return;
    int h = ti.tm_hour;
    if (h >= OLED_DIM_START_HOUR && h < OLED_DIM_END_HOUR)
        turnOffOLED();
    else
        turnOnOLED();
}

// ========================================
// TIME
// ========================================
bool getTimeWithFallback(struct tm *ti)
{
    if (getLocalTime(ti) && ti->tm_year >= 120)
        return true;
    if (!timeWasSynced || lastValidTime == 0 || !bootTimeSet)
        return false;
    unsigned long elapsed = (millis() - bootTime) / 1000UL;
    if (elapsed > MAX_TIME_ESTIMATE_AGE)
        return false;
    time_t est = lastValidTime + (time_t)elapsed;
    *ti = *localtime(&est);
    return true;
}

bool isTimeValid()
{
    struct tm ti;
    return getTimeWithFallback(&ti);
}

void getFormattedTimestamp(char *buf, size_t sz)
{
    struct tm ti;
    if (!getTimeWithFallback(&ti))
    {
        buf[0] = '\0';
        return;
    }
    strftime(buf, sz, "%Y-%m-%d %H:%M:%S", &ti);
}

bool syncTimeWithFallback()
{
    if (isSignalCritical())
        return false;
    const char *servers[] = {NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3};
    for (int i = 0; i < 3; i++)
    {
        char srv[32];
        strcpy_P(srv, servers[i]);
        configTime(GMT_OFFSET_SEC, 0, srv);
        struct tm ti;
        unsigned long t0 = millis();
        while (millis() - t0 < 2500)
        {
            esp_task_wdt_reset();
            if (getLocalTime(&ti) && ti.tm_year >= 120)
            {
                lastValidTime = mktime(&ti);
                nvsSaveLastTime(lastValidTime);
                timeWasSynced = true;
                if (!bootTimeSet)
                {
                    bootTime = millis();
                    bootTimeSet = true;
                }
                char buf[6];
                snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
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
    if (!isSignalCritical())
        syncTimeWithFallback();
}

// ========================================
// NVS BUFFER
// ========================================
void nvsSaveLastTime(time_t t)
{
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putULong(NVS_KEY_LAST_TIME, (unsigned long)t);
    prefs.end();
}

time_t nvsLoadLastTime()
{
    prefs.begin(NVS_NAMESPACE, true);
    unsigned long t = prefs.getULong(NVS_KEY_LAST_TIME, 0);
    prefs.end();
    return (time_t)t;
}

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

bool nvsLoadRecord(int idx, OfflineRecord &rec)
{
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, idx);
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

bool nvsSaveRecord(int idx, const OfflineRecord &rec)
{
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, idx);
    prefs.begin(NVS_NAMESPACE, false);
    size_t w = prefs.putBytes(key, &rec, sizeof(OfflineRecord));
    prefs.end();
    return w == sizeof(OfflineRecord);
}

void nvsDeleteRecord(int idx)
{
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, idx);
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(key);
    prefs.end();
}

bool nvsIsDuplicate(const char *rfid, unsigned long t)
{
    int cnt = nvsGetCount();
    for (int i = 0; i < cnt; i++)
    {
        OfflineRecord rec;
        if (!nvsLoadRecord(i, rec))
            continue;
        if (strcmp(rec.rfid, rfid) == 0 && t >= rec.unixTime && (t - rec.unixTime) < MIN_REPEAT_INTERVAL)
            return true;
    }
    return false;
}

bool nvsSaveToBuffer(const char *rfid, const char *ts, unsigned long t)
{
    if (nvsIsDuplicate(rfid, t))
        return false;
    int cnt = nvsGetCount();
    if (cnt >= NVS_MAX_RECORDS)
        return false;
    OfflineRecord rec;
    strncpy(rec.rfid, rfid, sizeof(rec.rfid) - 1);
    rec.rfid[sizeof(rec.rfid) - 1] = '\0';
    strncpy(rec.timestamp, ts, sizeof(rec.timestamp) - 1);
    rec.timestamp[sizeof(rec.timestamp) - 1] = '\0';
    strncpy(rec.deviceId, deviceId, sizeof(rec.deviceId) - 1);
    rec.deviceId[sizeof(rec.deviceId) - 1] = '\0';
    rec.unixTime = t;
    if (!nvsSaveRecord(cnt, rec))
        return false;
    nvsSetCount(cnt + 1);
    return true;
}

bool nvsSyncToServer()
{
    int cnt = nvsGetCount();
    if (cnt == 0)
        return true;
    if (isSignalCritical())
        return false;

    HTTPClient http;
    http.setTimeout(30000);
    http.setConnectTimeout(10000);

    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/sync-bulk"));
    if (!http.begin(getHttpClient(), url))
        return false;

    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    // Estimasi: tiap record ~120 byte JSON + overhead
    const size_t docSz = 512 + (size_t)cnt * 128;
    DynamicJsonDocument doc(docSz);
    JsonArray arr = doc.createNestedArray("data");
    for (int i = 0; i < cnt; i++)
    {
        OfflineRecord rec;
        if (!nvsLoadRecord(i, rec))
            continue;
        JsonObject o = arr.createNestedObject();
        o["rfid"] = rec.rfid;
        o["timestamp"] = rec.timestamp;
        o["device_id"] = rec.deviceId;
        o["sync_mode"] = true;
    }
    String payload;
    serializeJson(doc, payload);
    doc.clear();

    esp_task_wdt_reset();
    int code = http.POST(payload);
    esp_task_wdt_reset();

    if (code == 200)
    {
        String body = http.getString();
        esp_task_wdt_reset();
        http.end();

        DynamicJsonDocument res(512 + (size_t)cnt * 128);
        if (deserializeJson(res, body) == DeserializationError::Ok)
        {
            for (JsonObject item : res["data"].as<JsonArray>())
            {
                const char *st = item["status"] | "error";
                if (strcmp(st, "error") == 0)
                    appendFailedLog(item["rfid"] | "unknown",
                                    item["timestamp"] | "unknown",
                                    item["message"] | "UNKNOWN");
            }
        }
        nvsSetCount(0);
        for (int i = 0; i < cnt; i++)
            nvsDeleteRecord(i);
        return true;
    }
    http.end();
    return false;
}

// ========================================
// RFID LOCAL DB + RAM CACHE
// ========================================
unsigned long nvsGetRfidDbVer()
{
    prefs.begin(NVS_NAMESPACE, true);
    unsigned long v = prefs.getULong(NVS_KEY_RFID_VER, 0);
    prefs.end();
    return v;
}

void nvsSetRfidDbVer(unsigned long ver)
{
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putULong(NVS_KEY_RFID_VER, ver);
    prefs.end();
}

void freeRfidCache()
{
    if (rfidCache)
    {
        for (int i = 0; i < rfidCacheCount; i++)
            if (rfidCache[i])
                free(rfidCache[i]);
        free(rfidCache);
        rfidCache = nullptr;
    }
    rfidCacheCount = 0;
    rfidCacheLoaded = false;
}

// Caller harus sudah acquireSD() + selectSD() ATAU panggil tanpa mutex (saat setup).
// Untuk keamanan, fungsi ini selalu dipanggil dengan mutex dipegang caller.
bool loadRfidCacheFromFileLocked()
{
    freeRfidCache();
    if (!sd.exists(RFID_DB_FILE))
        return false;

    FsFile f;
    if (!f.open(RFID_DB_FILE, O_RDONLY))
        return false;

    // Pass pertama: hitung baris valid
    int cnt = 0;
    char line[12];
    while (f.fgets(line, sizeof(line)) > 0)
    {
        esp_task_wdt_reset();
        int len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len != 10)
            continue;
        bool ok = true;
        for (int j = 0; j < 10 && ok; j++)
            ok = isdigit((unsigned char)line[j]);
        if (ok)
            cnt++;
    }
    if (cnt == 0)
    {
        f.close();
        return false;
    }

    rfidCache = (char **)malloc((size_t)cnt * sizeof(char *));
    if (!rfidCache)
    {
        f.close();
        return false;
    }

    // Pass kedua: isi cache
    f.seekSet(0);
    int idx = 0;
    while (f.fgets(line, sizeof(line)) > 0 && idx < cnt)
    {
        esp_task_wdt_reset();
        int len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len != 10)
            continue;
        bool ok = true;
        for (int j = 0; j < 10 && ok; j++)
            ok = isdigit((unsigned char)line[j]);
        if (!ok)
            continue;
        rfidCache[idx] = (char *)malloc(11);
        if (rfidCache[idx])
        {
            memcpy(rfidCache[idx], line, 11);
            idx++;
        }
    }
    f.close();
    rfidCacheCount = idx;
    rfidCacheLoaded = true;
    return idx > 0;
}

bool loadRfidCacheFromFile()
{
    if (!sdCardAvailable)
        return false;
    acquireSD();
    selectSD();
    bool ok = loadRfidCacheFromFileLocked();
    deselectSD();
    releaseSD();
    return ok;
}

bool isRfidInCache(const char *rfid)
{
    if (!rfidCacheLoaded || rfidCacheCount == 0)
        return true; // fail-open
    for (int i = 0; i < rfidCacheCount; i++)
        if (rfidCache[i] && strcmp(rfidCache[i], rfid) == 0)
            return true;
    return false;
}

unsigned long checkRfidDbVersion()
{
    if (isSignalWeak())
        return 0;
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(5000);
    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/rfid-list/version"));
    if (!http.begin(getHttpClient(), url))
        return 0;
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);
    int code = http.GET();
    if (code != 200)
    {
        http.end();
        return 0;
    }
    String body = http.getString();
    http.end();
    DynamicJsonDocument doc(128);
    if (deserializeJson(doc, body) != DeserializationError::Ok)
        return 0;
    return doc["ver"] | 0UL;
}

bool downloadRfidDb()
{
    if (isSignalWeak() || !sdCardAvailable)
        return false;
    showOLED(F("RFID DB"), "MENGUNDUH...");

    HTTPClient http;
    http.setTimeout(30000);
    http.setConnectTimeout(10000);
    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/rfid-list"));
    if (!http.begin(getHttpClient(), url))
        return false;
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    int code = http.GET();
    if (code != 200)
    {
        http.end();
        showOLED(F("RFID DB"), "GAGAL UNDUH");
        playToneError();
        delay(800);
        return false;
    }

    acquireSD();
    selectSD();

    const char *tmpPath = "/rfid_db.tmp";
    if (sd.exists(tmpPath))
        sd.remove(tmpPath);

    FsFile dbf;
    if (!dbf.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC))
    {
        deselectSD();
        releaseSD();
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    int total = http.getSize();
    int written = 0;
    unsigned long serverVer = 0;
    bool firstLine = true;
    char lineBuf[32];
    int lbPos = 0;

    uint8_t chunk[256];
    while (http.connected() && (total < 0 || written < total))
    {
        esp_task_wdt_reset();
        int avail = stream->available();
        if (!avail)
        {
            delay(5);
            continue;
        }
        int toRead = min(avail, (int)sizeof(chunk));
        int rd = stream->readBytes(chunk, toRead);
        for (int i = 0; i < rd; i++)
        {
            char c = (char)chunk[i];
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                lineBuf[lbPos] = '\0';
                lbPos = 0;
                if (firstLine)
                {
                    firstLine = false;
                    if (strncmp(lineBuf, "ver:", 4) == 0)
                    {
                        serverVer = strtoul(lineBuf + 4, nullptr, 10);
                        continue;
                    }
                }
                int ll = strlen(lineBuf);
                if (ll == 10)
                {
                    bool ok = true;
                    for (int j = 0; j < 10 && ok; j++)
                        ok = isdigit((unsigned char)lineBuf[j]);
                    if (ok)
                    {
                        dbf.print(lineBuf);
                        dbf.print('\n');
                        written++;
                    }
                }
            }
            else
            {
                if (lbPos < (int)sizeof(lineBuf) - 1)
                    lineBuf[lbPos++] = c;
            }
        }
    }
    http.end();

    dbf.sync();
    dbf.close();

    if (sd.exists(RFID_DB_FILE))
        sd.remove(RFID_DB_FILE);
    sd.rename(tmpPath, RFID_DB_FILE);

    // Reload cache dalam lock yang sama
    loadRfidCacheFromFileLocked();

    deselectSD();
    releaseSD();

    if (serverVer > 0)
        nvsSetRfidDbVer(serverVer);

    char buf[20];
    snprintf(buf, sizeof(buf), "%d RFID", written);
    showOLED(F("RFID DB"), buf);
    playToneSuccess();
    delay(800);
    return true;
}

void checkAndUpdateRfidDb()
{
    if (!sdCardAvailable || isSignalWeak())
        return;
    if (millis() - timers.lastRfidDbCheck < RFID_DB_CHECK_INTERVAL)
        return;
    timers.lastRfidDbCheck = millis();
    unsigned long local = nvsGetRfidDbVer();
    unsigned long server = checkRfidDbVersion();
    if (server == 0 || server <= local)
        return;
    downloadRfidDb();
}

// ========================================
// METADATA — selalu dipanggil dengan SD mutex dipegang caller
// ========================================
void saveMetadataLocked()
{
    selectSD();
    if (file.open(METADATA_FILE, O_WRONLY | O_CREAT | O_TRUNC))
    {
        file.print(cachedPendingRecords);
        file.print(',');
        file.println(currentQueueFile);
        file.sync();
        file.close();
    }
    deselectSD();
}

void loadMetadataLocked()
{
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
void getQueueFileName(int idx, char *buf, size_t sz)
{
    snprintf(buf, sz, "/queue_%d.csv", idx);
}

// Caller HARUS sudah selectSD()
int countRecordsInFileLocked(const char *filename)
{
    if (!file.open(filename, O_RDONLY))
        return 0;
    int cnt = 0;
    char line[128];
    if (file.available())
        file.fgets(line, sizeof(line)); // skip header
    while (file.fgets(line, sizeof(line)) > 0)
    {
        esp_task_wdt_reset();
        if (strlen(line) > 10)
            cnt++;
    }
    file.close();
    return cnt;
}

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
        bool ok = reinitSDCard();
        releaseSD();
        if (ok)
        {
            sdCardAvailable = true;
            pendingCacheDirty = true;
            showOLED(F("SD CARD"), "TERBACA KEMBALI");
            playToneSuccess();
            delay(800);
            loadRfidCacheFromFile();
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
        freeRfidCache();
        showOLED(F("SD CARD"), "TERLEPAS!");
        playToneError();
        delay(800);
    }
}

int countAllOfflineRecords()
{
    if (!sdCardAvailable)
        return 0;
    int total = 0;
    cachedQueueFileCount = 0;
    char fn[20];
    acquireSD();
    selectSD();
    for (int i = 0; i < MAX_QUEUE_FILES; i++)
    {
        esp_task_wdt_reset();
        getQueueFileName(i, fn, sizeof(fn));
        if (!sd.exists(fn))
            continue;
        cachedQueueFileCount++;
        int n = countRecordsInFileLocked(fn);
        total += n;
    }
    deselectSD();
    releaseSD();
    return total;
}

void refreshPendingCache()
{
    if (!pendingCacheDirty)
        return;
    cachedPendingRecords = countAllOfflineRecords();
    pendingCacheDirty = false;
    acquireSD();
    saveMetadataLocked();
    releaseSD();
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

    loadMetadataLocked(); // SPI sudah dikuasai, tidak perlu mutex lagi saat init

    if (!rtcQueueFileValid)
    {
        currentQueueFile = -1;
        char fn[20];
        for (int i = 0; i < MAX_QUEUE_FILES; i++)
        {
            esp_task_wdt_reset();
            getQueueFileName(i, fn, sizeof(fn));
            if (!sd.exists(fn))
            {
                if (file.open(fn, O_WRONLY | O_CREAT))
                {
                    file.println(F("rfid,timestamp,device_id,unix_time"));
                    file.close();
                    currentQueueFile = i;
                    break;
                }
            }
            else
            {
                int cnt = countRecordsInFileLocked(fn);
                if (cnt < MAX_RECORDS_PER_FILE)
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
// DUPLICATE CHECK — caller harus acquireSD() + selectSD()
// ========================================
bool isDuplicateLocked(const char *rfid, unsigned long t)
{
    char fn[20];
    for (int offset = 0; offset < MAX_DUPLICATE_CHECK_FILES; offset++)
    {
        esp_task_wdt_reset();
        int idx = (currentQueueFile - offset + MAX_QUEUE_FILES) % MAX_QUEUE_FILES;
        getQueueFileName(idx, fn, sizeof(fn));
        if (!sd.exists(fn))
            continue;
        if (!file.open(fn, O_RDONLY))
            continue;
        char line[128];
        if (file.available())
            file.fgets(line, sizeof(line)); // skip header
        int read = 0;
        bool found = false;
        while (!found && read < MAX_DUPLICATE_CHECK_LINES && file.fgets(line, sizeof(line)) > 0)
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
                unsigned long ft = strtoul(c3 + 1, nullptr, 10);
                if (strcmp(line, rfid) == 0 && t >= ft && (t - ft) < MIN_REPEAT_INTERVAL)
                    found = true;
                *c1 = ',';
            }
            read++;
        }
        file.close();
        if (found)
            return true;
    }
    return false;
}

// ========================================
// SAVE TO QUEUE
// ========================================
SaveResult saveToQueue(const char *rfid, const char *ts, unsigned long t)
{
    if (!sdCardAvailable)
        return SAVE_SD_ERROR;

    acquireSD();
    selectSD();

    if (isDuplicateLocked(rfid, t))
    {
        deselectSD();
        releaseSD();
        return SAVE_DUPLICATE;
    }

    if (currentQueueFile < 0 || currentQueueFile >= MAX_QUEUE_FILES)
        currentQueueFile = 0;

    char curFn[20];
    getQueueFileName(currentQueueFile, curFn, sizeof(curFn));

    if (!sd.exists(curFn))
    {
        if (file.open(curFn, O_WRONLY | O_CREAT))
        {
            file.println(F("rfid,timestamp,device_id,unix_time"));
            file.close();
        }
    }

    int curCnt = countRecordsInFileLocked(curFn);

    if (curCnt >= MAX_RECORDS_PER_FILE)
    {
        int nextIdx = (currentQueueFile + 1) % MAX_QUEUE_FILES;
        char nextFn[20];
        getQueueFileName(nextIdx, nextFn, sizeof(nextFn));

        if (sd.exists(nextFn))
        {
            int nextCnt = countRecordsInFileLocked(nextFn);
            if (nextCnt > 0)
            {
                deselectSD();
                releaseSD();
                return SAVE_QUEUE_FULL;
            }
            sd.remove(nextFn);
        }

        currentQueueFile = nextIdx;
        getQueueFileName(currentQueueFile, curFn, sizeof(curFn));
        if (!file.open(curFn, O_WRONLY | O_CREAT))
        {
            deselectSD();
            releaseSD();
            return SAVE_SD_ERROR;
        }
        file.println(F("rfid,timestamp,device_id,unix_time"));
        file.close();
    }

    if (!file.open(curFn, O_WRONLY | O_APPEND))
    {
        deselectSD();
        releaseSD();
        return SAVE_SD_ERROR;
    }

    file.print(rfid);
    file.print(',');
    file.print(ts);
    file.print(',');
    file.print(deviceId);
    file.print(',');
    file.println(t);
    file.sync();
    file.close();

    deselectSD();

    cachedPendingRecords++;
    pendingCacheDirty = false;
    saveMetadataLocked(); // masih dalam acquireSD
    releaseSD();

    return SAVE_OK;
}

// ========================================
// FAILED LOG — acquires own mutex
// ========================================
void appendFailedLog(const char *rfid, const char *ts, const char *reason)
{
    if (!sdCardAvailable)
        return;
    acquireSD();
    selectSD();
    if (file.open("/failed_log.csv", O_WRONLY | O_CREAT | O_APPEND))
    {
        if (file.size() == 0)
            file.println(F("rfid,timestamp,reason"));
        file.print(rfid);
        file.print(',');
        file.print(ts);
        file.print(',');
        file.println(reason);
        file.sync();
        file.close();
    }
    deselectSD();
    releaseSD();
}

// ========================================
// READ QUEUE FILE — caller sudah acquireSD() + selectSD()
// ========================================
bool readQueueFileLocked(const char *fn, OfflineRecord *recs, int *cnt, int maxCnt)
{
    if (!file.open(fn, O_RDONLY))
        return false;
    *cnt = 0;
    time_t now = time(nullptr);
    char line[128];
    if (file.available())
        file.fgets(line, sizeof(line)); // skip header
    while (file.available() && *cnt < maxCnt)
    {
        esp_task_wdt_reset();
        if (file.fgets(line, sizeof(line)) <= 0)
            break;
        int len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len < 10)
            continue;
        char *c1 = strchr(line, ',');
        char *c2 = c1 ? strchr(c1 + 1, ',') : nullptr;
        char *c3 = c2 ? strchr(c2 + 1, ',') : nullptr;
        if (!c1 || !c2 || !c3)
            continue;
        int rl = c1 - line, tl = c2 - c1 - 1, dl = c3 - c2 - 1;
        if (rl <= 0 || tl <= 0)
            continue;
        strncpy(recs[*cnt].rfid, line, min(rl, 10));
        recs[*cnt].rfid[min(rl, 10)] = '\0';
        strncpy(recs[*cnt].timestamp, c1 + 1, min(tl, 19));
        recs[*cnt].timestamp[min(tl, 19)] = '\0';
        strncpy(recs[*cnt].deviceId, c2 + 1, min(dl, 19));
        recs[*cnt].deviceId[min(dl, 19)] = '\0';
        recs[*cnt].unixTime = strtoul(c3 + 1, nullptr, 10);
        if (recs[*cnt].timestamp[0] == '\0')
        {
            appendFailedLog(recs[*cnt].rfid, "empty", "TIMESTAMP_KOSONG");
        }
        else if ((unsigned long)now - recs[*cnt].unixTime <= MAX_OFFLINE_AGE)
        {
            (*cnt)++;
        }
    }
    file.close();
    return *cnt > 0;
}

// ========================================
// SYNC QUEUE FILE
// acquireSD dilakukan di sini (sebelum read) dan setelah HTTP (sebelum delete).
// Tidak ada nested acquireSD karena appendFailedLog acquire sendiri,
// tapi itu tidak terjadi di dalam section yang sudah lock.
// ========================================
SyncFileResult syncQueueFile(const char *fn)
{
    if (!sdCardAvailable || !isWifiConnected())
        return SYNC_FILE_NO_WIFI;

    OfflineRecord recs[MAX_RECORDS_PER_FILE];
    int validCnt = 0;

    // Baca file
    acquireSD();
    selectSD();
    bool hasData = readQueueFileLocked(fn, recs, &validCnt, MAX_RECORDS_PER_FILE);
    if (!hasData || validCnt == 0)
    {
        if (sd.exists(fn))
            sd.remove(fn);
        deselectSD();
        releaseSD();
        pendingCacheDirty = true;
        return SYNC_FILE_EMPTY;
    }
    deselectSD();
    releaseSD();

    // Kirim ke server
    HTTPClient http;
    http.setTimeout(45000);
    http.setConnectTimeout(15000);

    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/sync-bulk"));
    if (!http.begin(getHttpClient(), url))
        return SYNC_FILE_HTTP_FAIL;

    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    const size_t docSz = 512 + (size_t)validCnt * 128;
    DynamicJsonDocument doc(docSz);
    JsonArray arr = doc.createNestedArray("data");
    for (int i = 0; i < validCnt; i++)
    {
        JsonObject o = arr.createNestedObject();
        o["rfid"] = recs[i].rfid;
        o["timestamp"] = recs[i].timestamp;
        o["device_id"] = recs[i].deviceId;
        o["sync_mode"] = true;
    }
    String payload;
    serializeJson(doc, payload);
    doc.clear();

    esp_task_wdt_reset();
    int code = http.POST(payload);
    esp_task_wdt_reset();

    if (code == 200)
    {
        String body = http.getString();
        esp_task_wdt_reset();
        http.end();

        // Parse response (appendFailedLog acquire SD sendiri — aman karena kita tidak hold lock)
        DynamicJsonDocument res(512 + (size_t)validCnt * 128);
        if (deserializeJson(res, body) == DeserializationError::Ok)
        {
            for (JsonObject item : res["data"].as<JsonArray>())
            {
                const char *st = item["status"] | "error";
                if (strcmp(st, "error") == 0)
                    appendFailedLog(item["rfid"] | "unknown",
                                    item["timestamp"] | "unknown",
                                    item["message"] | "UNKNOWN");
            }
        }

        // Hapus file setelah berhasil
        acquireSD();
        selectSD();
        sd.remove(fn);
        deselectSD();
        releaseSD();

        pendingCacheDirty = true;
        if (cachedPendingRecords >= validCnt)
            cachedPendingRecords -= validCnt;
        else
            cachedPendingRecords = 0;

        return SYNC_FILE_OK;
    }

    http.end();

    if (!isWifiConnected())
    {
        syncState.inProgress = false;
        return SYNC_FILE_NO_WIFI;
    }
    return SYNC_FILE_HTTP_FAIL;
}

// ========================================
// SYNC WITH RETRY
// ========================================
bool syncQueueFileWithRetry(const char *fn)
{
    for (int attempt = 0; attempt <= MAX_SYNC_RETRIES; attempt++)
    {
        esp_task_wdt_reset();
        if (!isWifiConnected())
        {
            syncState.inProgress = false;
            return false;
        }

        SyncFileResult r = syncQueueFile(fn);
        if (r == SYNC_FILE_OK || r == SYNC_FILE_EMPTY)
            return true;
        if (r == SYNC_FILE_NO_WIFI)
        {
            syncState.inProgress = false;
            return false;
        }

        if (attempt < MAX_SYNC_RETRIES)
        {
            char buf[20];
            snprintf(buf, sizeof(buf), "RETRY %d/%d...", attempt + 1, MAX_SYNC_RETRIES);
            showOLED(F("SYNC ULANG"), buf);
            unsigned long end = millis() + SYNC_RETRY_DELAY_MS * (1UL << attempt);
            while (millis() < end)
            {
                esp_task_wdt_reset();
                delay(100);
            }
        }
    }
    return false;
}

// ========================================
// CHUNKED SYNC
// ========================================
void chunkedSync()
{
    if (!sdCardAvailable || !isWifiConnected())
    {
        syncState.inProgress = false;
        return;
    }

    extendWdtForSync();

    if (!syncState.inProgress)
    {
        syncState.inProgress = true;
        syncState.currentFile = 0;
        syncState.startTime = millis();
        syncState.filesProcessed = 0;
        syncState.filesSucceeded = 0;
    }

    char fn[20];

    while (syncState.currentFile < MAX_QUEUE_FILES && syncState.filesProcessed < MAX_SYNC_FILES_PER_CYCLE)
    {
        if (!isWifiConnected())
        {
            syncState.inProgress = false;
            break;
        }
        esp_task_wdt_reset();

        getQueueFileName(syncState.currentFile, fn, sizeof(fn));

        acquireSD();
        selectSD();
        bool exists = sd.exists(fn);
        int nRecs = exists ? countRecordsInFileLocked(fn) : 0;
        if (exists && nRecs == 0)
        {
            sd.remove(fn);
            pendingCacheDirty = true;
        }
        deselectSD();
        releaseSD();

        if (exists)
        {
            if (nRecs > 0)
            {
                char buf[24];
                snprintf(buf, sizeof(buf), "FILE %d (%d rec)", syncState.currentFile, nRecs);
                showOLED(F("SYNC"), buf);

                bool ok = syncQueueFileWithRetry(fn);
                syncState.filesProcessed++;
                if (ok)
                    syncState.filesSucceeded++;
                else if (!isWifiConnected())
                {
                    syncState.inProgress = false;
                    break;
                }
            }
            else
            {
                syncState.filesProcessed++; // file kosong sudah dihapus di atas
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
        syncState.filesProcessed = 0;

        refreshPendingCache();

        if (syncState.filesSucceeded > 0)
        {
            char buf[20];
            if (cachedPendingRecords == 0)
            {
                showOLED(F("SYNC"), "SELESAI!");
                playToneSuccess();
            }
            else
            {
                snprintf(buf, sizeof(buf), "SISA %d", cachedPendingRecords);
                showOLED(F("SYNC PARSIAL"), buf);
            }
            delay(500);
        }
        syncState.filesSucceeded = 0;
    }

    restoreWdtNormal();
}

// ========================================
// OTA UPDATE
// ========================================
void checkOtaUpdate()
{
    if (isSignalWeak())
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
    if (!http.begin(getHttpClient(), url))
        return;

    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"version\":\"%s\",\"device_id\":\"%s\"}", FIRMWARE_VERSION, deviceId);

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
    const char *ver = doc["version"] | "";
    const char *burl = doc["url"] | "";
    if (!hasUpdate || !strlen(ver) || !strlen(burl))
        return;

    strncpy(otaState.version, ver, sizeof(otaState.version) - 1);
    otaState.version[sizeof(otaState.version) - 1] = '\0';
    strncpy(otaState.url, burl, sizeof(otaState.url) - 1);
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
    if (!otaState.updateAvailable || isSignalWeak())
        return;

    char buf[20];
    snprintf(buf, sizeof(buf), "v%s", otaState.version);
    showOLED(F("UPDATE OTA"), buf);
    delay(500);
    showOLED(F("MENGUNDUH"), "MOHON TUNGGU...");

    extendWdtForSync();

    WiFiClientSecure otaClient;
    otaClient.setInsecure();
    HTTPClient http;
    http.begin(otaClient, otaState.url);
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);
    http.setTimeout(60000);

    int code = http.GET();
    if (code != 200)
    {
        snprintf(buf, sizeof(buf), "HTTP ERR %d", code);
        showOLED(F("UPDATE GAGAL"), buf);
        playToneError();
        http.end();
        otaState.updateAvailable = false;
        restoreWdtNormal();
        return;
    }

    int total = http.getSize();
    WiFiClient *stream = http.getStreamPtr();

    if (!Update.begin((size_t)total))
    {
        showOLED(F("UPDATE GAGAL"), "NO SPACE");
        playToneError();
        http.end();
        otaState.updateAvailable = false;
        restoreWdtNormal();
        return;
    }

    uint8_t buff[1024];
    int written = 0;
    while (http.connected() && written < total)
    {
        int avail = stream->available();
        if (avail)
        {
            int rd = stream->readBytes(buff, min((int)sizeof(buff), avail));
            Update.write(buff, rd);
            written += rd;
        }
        esp_task_wdt_reset();
        delay(1);
    }
    http.end();

    if (Update.end() && Update.isFinished())
    {
        showOLED(F("UPDATE OK"), "RESTART...");
        playToneSuccess();
        delay(2000);
        restoreWdtNormal();
        ESP.restart();
    }
    else
    {
        snprintf(buf, sizeof(buf), "ERR %d", Update.getError());
        showOLED(F("UPDATE GAGAL"), buf);
        playToneError();
        otaState.updateAvailable = false;
    }

    restoreWdtNormal();
    // Force display redraw setelah OTA gagal
    memset(previousDisplay.time, 0xFF, sizeof(previousDisplay.time));
    previousDisplay.pendingRecords = -1;
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
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);

    char ssid[32], pass[32];
    strcpy_P(ssid, WIFI_SSID);
    strcpy_P(pass, WIFI_PASSWORD);
    WiFi.begin(ssid, pass);

    for (int i = 0; i < 20 && !isWifiConnected(); i++)
    {
        esp_task_wdt_reset();
        yield();
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor((SCREEN_WIDTH - (int)strlen(ssid) * 6) / 2, 10);
        display.println(ssid);
        display.setCursor(35, 30);
        display.print(F("CONNECTING"));
        for (int j = 0; j < (i % 4); j++)
            display.print('.');
        display.display();
        delay(300);
    }

    if (isWifiConnected())
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
    if (isSignalCritical())
        return false;
    HTTPClient http;
    http.setTimeout(5000);
    http.setConnectTimeout(3000);
    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/ping"));
    http.begin(getHttpClient(), url);
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
        if (!isWifiConnected() && millis() - timers.lastReconnect >= RECONNECT_INTERVAL)
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
        if (isWifiConnected())
            reconnectState = RECONNECT_SUCCESS;
        else if (millis() - reconnectStartTime >= RECONNECT_TIMEOUT)
            reconnectState = RECONNECT_FAILED;
        break;

    case RECONNECT_SUCCESS:
        isOnline = true;
        if (!isSignalCritical())
        {
            syncTimeWithFallback();
            if (nvsGetCount() > 0)
                nvsSyncToServer();
            if (sdCardAvailable && isWifiConnected())
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
void uidToString(uint8_t *uid, uint8_t len, char *out)
{
    if (len >= 4)
    {
        uint32_t v = ((uint32_t)uid[3] << 24) | ((uint32_t)uid[2] << 16) | ((uint32_t)uid[1] << 8) | uid[0];
        sprintf(out, "%010lu", v);
    }
    else
    {
        sprintf(out, "%02X%02X", uid[0], uid[1]);
    }
}

bool kirimLangsung(const char *rfid, const char *ts, char *msg)
{
    if (!isWifiConnected())
        return false;
    HTTPClient http;
    http.setTimeout(10000);
    http.setConnectTimeout(5000);
    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi"));
    if (!http.begin(getHttpClient(), url))
        return false;
    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32];
    strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"rfid\":\"%s\",\"timestamp\":\"%s\",\"device_id\":\"%s\",\"sync_mode\":false}",
             rfid, ts, deviceId);
    int code = http.POST(payload);
    http.end();
    if (code == 200)
    {
        strcpy(msg, "PRESENSI OK");
        return true;
    }
    if (code == 400)
    {
        strcpy(msg, "CUKUP SEKALI!");
        return false;
    }
    if (code == 404)
    {
        strcpy(msg, "RFID UNKNOWN");
        return false;
    }
    snprintf(msg, 32, "SERVER ERR %d", code);
    return false;
}

bool kirimPresensi(const char *rfid, char *msg)
{
    if (!isTimeValid())
    {
        strcpy(msg, "WAKTU INVALID");
        return false;
    }

    char ts[20];
    getFormattedTimestamp(ts, sizeof(ts));
    time_t now = time(nullptr);

    if (sdCardAvailable)
    {
        if (!isRfidInCache(rfid))
        {
            strcpy(msg, "HUBUNGI ADMIN");
            return false;
        }
        SaveResult r = saveToQueue(rfid, ts, (unsigned long)now);
        switch (r)
        {
        case SAVE_OK:
            strcpy(msg, cachedQueueFileCount >= QUEUE_WARN_THRESHOLD
                            ? "QUEUE HAMPIR PENUH!"
                            : "DATA TERSIMPAN");
            return true;
        case SAVE_DUPLICATE:
            strcpy(msg, "CUKUP SEKALI!");
            return false;
        case SAVE_QUEUE_FULL:
            strcpy(msg, "QUEUE PENUH!");
            return false;
        default:
            strcpy(msg, "SD CARD ERROR");
            return false;
        }
    }

    if (isWifiConnected())
    {
        if (kirimLangsung(rfid, ts, msg))
            return true;
        if (nvsIsDuplicate(rfid, (unsigned long)now))
        {
            strcpy(msg, "CUKUP SEKALI!");
            return false;
        }
        if (nvsSaveToBuffer(rfid, ts, (unsigned long)now))
        {
            snprintf(msg, 32, "BUFFER %d/%d", nvsGetCount(), NVS_MAX_RECORDS);
            return true;
        }
        strcpy(msg, "BUFFER PENUH!");
        return false;
    }

    if (nvsIsDuplicate(rfid, (unsigned long)now))
    {
        strcpy(msg, "CUKUP SEKALI!");
        return false;
    }
    if (nvsSaveToBuffer(rfid, ts, (unsigned long)now))
    {
        snprintf(msg, 32, "BUFFER %d/%d", nvsGetCount(), NVS_MAX_RECORDS);
        return true;
    }
    strcpy(msg, "BUFFER PENUH!");
    return false;
}

// ========================================
// DISPLAY
// ========================================
void showOLED(const __FlashStringHelper *l1, const char *l2)
{
    if (!oledIsOn)
        return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    int16_t x, y;
    uint16_t w, h;
    display.getTextBounds(l1, 0, 0, &x, &y, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 10);
    display.println(l1);
    display.getTextBounds(l2, 0, 0, &x, &y, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 30);
    display.println(l2);
    display.display();
}

void showOLED(const __FlashStringHelper *l1, const __FlashStringHelper *l2)
{
    char buf[32];
    strncpy_P(buf, (const char *)l2, 31);
    buf[31] = '\0';
    showOLED(l1, buf);
}

void showProgress(const __FlashStringHelper *msg, int ms)
{
    if (!oledIsOn)
        return;
    const int step = 8, total = 80;
    int perStep = ms / (total / step);
    int startX = (SCREEN_WIDTH - total) / 2;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    int16_t x, y;
    uint16_t w, h;
    display.getTextBounds(msg, 0, 0, &x, &y, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 20);
    display.println(msg);
    display.display();
    for (int i = 0; i <= total; i += step)
    {
        esp_task_wdt_reset();
        display.fillRect(startX, 40, i, 4, WHITE);
        display.display();
        delay(perStep);
    }
    esp_task_wdt_reset();
    delay(300);
}

void showStartupAnimation()
{
    static const char title[] = "ZEDLABS";
    static const char sub1[] = "INNOVATE BEYOND";
    static const char sub2[] = "LIMITS";
    static const char version[] = "v" FIRMWARE_VERSION;

    const int tX = (SCREEN_WIDTH - 7 * 12) / 2;
    const int s1X = (SCREEN_WIDTH - 15 * 6) / 2;
    const int s2X = (SCREEN_WIDTH - 6 * 6) / 2;
    const int vX = (SCREEN_WIDTH - (int)strlen(version) * 6) / 2;

    display.clearDisplay();
    display.setTextColor(WHITE);
    for (int x = -80; x <= tX; x += 4)
    {
        esp_task_wdt_reset();
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(x, 5);
        display.println(title);
        display.setTextSize(1);
        display.setCursor(s1X, 30);
        display.println(sub1);
        display.setCursor(s2X, 40);
        display.println(sub2);
        display.display();
        delay(30);
    }
    esp_task_wdt_reset();
    delay(300);
    display.setTextSize(1);
    display.setCursor(vX, 55);
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
    return currentDisplay.isOnline != previousDisplay.isOnline || currentDisplay.pendingRecords != previousDisplay.pendingRecords || currentDisplay.wifiSignal != previousDisplay.wifiSignal || strncmp(currentDisplay.time, previousDisplay.time, 5) != 0;
}

void updateCurrentDisplayState()
{
    currentDisplay.isOnline = isWifiConnected();

    struct tm ti;
    if (getTimeWithFallback(&ti))
        snprintf(currentDisplay.time, sizeof(currentDisplay.time),
                 "%02d:%02d", ti.tm_hour, ti.tm_min);

    if (pendingCacheDirty)
        refreshPendingCache();
    currentDisplay.pendingRecords = cachedPendingRecords + nvsGetCount();

    if (isWifiConnected())
    {
        long r = WiFi.RSSI();
        currentDisplay.wifiSignal = r > -67 ? 4 : r > -70 ? 3
                                              : r > -80   ? 2
                                              : r > -90   ? 1
                                                          : 0;
    }
    else
    {
        currentDisplay.wifiSignal = 0;
    }
}

void updateStandbyDisplay()
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

    const char *tap = "TAP KARTU";
    int16_t x, y;
    uint16_t w, h;
    display.getTextBounds(tap, 0, 0, &x, &y, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 20);
    display.print(tap);

    display.getTextBounds(currentDisplay.time, 0, 0, &x, &y, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 35);
    display.print(currentDisplay.time);

    if (currentDisplay.pendingRecords > 0)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "Q:%d", currentDisplay.pendingRecords);
        display.getTextBounds(buf, 0, 0, &x, &y, &w, &h);
        display.setCursor((SCREEN_WIDTH - w) / 2, 50);
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
    static const int mel[] = {2500, 3000, 2500, 3000};
    for (int i = 0; i < 4; i++)
    {
        tone(PIN_BUZZER, mel[i], 100);
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

    char rfidBuf[11];
    uidToString(rfidReader.uid.uidByte, rfidReader.uid.size, rfidBuf);

    if (strcmp(rfidBuf, lastUID) == 0 && millis() - timers.lastScan < DEBOUNCE_TIME)
    {
        rfidReader.PICC_HaltA();
        rfidReader.PCD_StopCrypto1();
        digitalWrite(PIN_RFID_SS, HIGH);
        return;
    }

    // Tolak RFID jika SD sedang busy (sync sedang berjalan)
    if (sdBusy)
    {
        rfidReader.PICC_HaltA();
        rfidReader.PCD_StopCrypto1();
        digitalWrite(PIN_RFID_SS, HIGH);
        return;
    }

    strcpy(lastUID, rfidBuf);
    timers.lastScan = millis();

    bool wasOff = !oledIsOn;
    if (wasOff)
        turnOnOLED();

    showOLED(F("RFID"), rfidBuf);
    playToneNotify();

    char msg[32];
    bool ok = kirimPresensi(rfidBuf, msg);

    showOLED(ok ? F("BERHASIL") : F("INFO"), msg);
    ok ? playToneSuccess() : playToneError();

    rfidFeedback = {true, millis(), wasOff};

    rfidReader.PICC_HaltA();
    rfidReader.PCD_StopCrypto1();
    digitalWrite(PIN_RFID_SS, HIGH);
}

// ========================================
// SETUP
// ========================================
void setup()
{
    const esp_task_wdt_config_t wdtCfg = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000u,
        .idle_core_mask = 0,
        .trigger_panic = true};
    esp_task_wdt_init(&wdtCfg);
    esp_task_wdt_add(nullptr);
    esp_ota_mark_app_valid_cancel_rollback();

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

    // Kompensasi waktu setelah deep sleep
    if (timeWasSynced && lastValidTime > 0 && sleepDurationSeconds > 0)
    {
        lastValidTime += (time_t)sleepDurationSeconds;
        nvsSaveLastTime(lastValidTime);
        bootTime = millis();
        bootTimeSet = true;
        sleepDurationSeconds = 0;
    }
    if (!timeWasSynced || lastValidTime == 0)
    {
        time_t saved = nvsLoadLastTime();
        if (saved > 0)
        {
            lastValidTime = saved;
            timeWasSynced = true;
            bootTime = millis();
            bootTimeSet = true;
        }
    }

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
        showProgress(F("LOAD RFID DB"), 500);
        if (loadRfidCacheFromFile())
        {
            char buf[20];
            snprintf(buf, sizeof(buf), "%d RFID", rfidCacheCount);
            showOLED(F("RFID DB"), buf);
            delay(600);
        }
    }
    else
    {
        showOLED(F("SD CARD"), "TIDAK ADA");
        playToneError();
        delay(1000);
        int nc = nvsGetCount();
        if (nc > 0)
        {
            char buf[20];
            snprintf(buf, sizeof(buf), "%d TERSISA", nc);
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
        int apiRetry = 0;
        while (!pingAPI() && apiRetry < 3)
        {
            apiRetry++;
            char buf[12];
            snprintf(buf, sizeof(buf), "Retry %d/3", apiRetry);
            showOLED(F("API GAGAL"), buf);
            playToneError();
            delay(800);
            esp_task_wdt_reset();
        }

        if (isOnline && !isSignalCritical())
        {
            showOLED(F("API OK"), "SYNCING TIME");
            playToneSuccess();
            delay(500);
            syncTimeWithFallback();
            esp_task_wdt_reset();

            int nc = nvsGetCount();
            if (nc > 0)
            {
                char buf[20];
                snprintf(buf, sizeof(buf), "%d NVS RECORDS", nc);
                showOLED(F("SYNC NVS"), buf);
                delay(800);
                nvsSyncToServer();
                esp_task_wdt_reset();
            }

            if (sdCardAvailable && isWifiConnected())
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
                showProgress(F("SYNC RFID DB"), 500);
                unsigned long lv = nvsGetRfidDbVer();
                unsigned long sv = checkRfidDbVersion();
                if (sv > lv)
                    downloadRfidDb();
                else
                    showOLED(F("RFID DB"), "UP TO DATE");
                delay(600);
                esp_task_wdt_reset();
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
    byte ver = rfidReader.PCD_ReadRegister(rfidReader.VersionReg);
    if (ver == 0x00 || ver == 0xFF)
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
    timers.lastOtaCheck = 0; // cek OTA segera di loop pertama
    timers.lastRfidDbCheck = now;

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

    // Kembalikan tampilan standby setelah feedback RFID
    if (rfidFeedback.active && now - rfidFeedback.shownAt >= RFID_FEEDBACK_DISPLAY_MS)
    {
        rfidFeedback.active = false;
        if (rfidFeedback.wasOledOff)
            checkOLEDSchedule();
        // Invalidate display agar redraw
        memset(previousDisplay.time, 0xFF, sizeof(previousDisplay.time));
        previousDisplay.pendingRecords = -1;
        previousDisplay.isOnline = !currentDisplay.isOnline;
    }

    checkOLEDSchedule();
    checkSDHealth();

    // RFID mendapat prioritas tertinggi
    if (!syncState.inProgress && rfidReader.PICC_IsNewCardPresent() && rfidReader.PICC_ReadCardSerial())
    {
        handleRFIDScan();
        return;
    }

    processReconnect();

    if (now - timers.lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL)
    {
        timers.lastDisplayUpdate = now;
        updateCurrentDisplayState();
        updateStandbyDisplay();
    }

    if (now - timers.lastPeriodicCheck >= PERIODIC_CHECK_INTERVAL)
    {
        timers.lastPeriodicCheck = now;

        if (isWifiConnected())
        {
            if (!isSignalWeak())
            {
                checkOtaUpdate();
                checkAndUpdateRfidDb();
                if (otaState.updateAvailable && !rfidFeedback.active)
                    performOtaUpdate();
            }

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

    // Deep sleep
    struct tm ti;
    if (getTimeWithFallback(&ti))
    {
        int h = ti.tm_hour;
        if (h >= SLEEP_START_HOUR || h < SLEEP_END_HOUR)
        {
            if (syncState.inProgress)
            {
                chunkedSync();
                return;
            }

            showOLED(F("SLEEP MODE"), "...");
            delay(1000);

            // Hitung sisa detik hingga SLEEP_END_HOUR, hindari overflow/negatif
            int nowSec = ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec;
            int endSec;
            if (h >= SLEEP_START_HOUR)
                endSec = SLEEP_END_HOUR * 3600 + 86400; // lewat tengah malam
            else
                endSec = SLEEP_END_HOUR * 3600;

            int sleepSec = endSec - nowSec;
            if (sleepSec < 60)
                sleepSec = 60;
            if (sleepSec > 43200)
                sleepSec = 43200;

            char buf[24];
            snprintf(buf, sizeof(buf), "%dj %dm", sleepSec / 3600, (sleepSec % 3600) / 60);
            showOLED(F("SLEEP FOR"), buf);
            delay(2000);

            display.clearDisplay();
            display.display();
            display.ssd1306_command(SSD1306_DISPLAYOFF);

            sleepDurationSeconds = (uint64_t)sleepSec;
            restoreWdtNormal();
            esp_task_wdt_deinit();
            esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
            esp_deep_sleep_start();
        }
    }

    yield();
}
