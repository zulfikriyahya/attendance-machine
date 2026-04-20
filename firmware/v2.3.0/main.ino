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
#include <esp_ota_ops.h>

// #define DEV_MODE

#define PIN_SPI_SCK   4
#define PIN_SPI_MOSI  6
#define PIN_SPI_MISO  5
#define PIN_RFID_SS   7
#define PIN_RFID_RST  3
#define PIN_SD_CS     1
#define PIN_OLED_SDA  8
#define PIN_OLED_SCL  9
#define PIN_BUZZER    10
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

#define DEBOUNCE_TIME              150UL
#define SYNC_INTERVAL              300000UL
#define MAX_OFFLINE_AGE            2592000UL
#define MIN_REPEAT_INTERVAL        1800UL
#define TIME_SYNC_INTERVAL         3600000UL
#define RECONNECT_INTERVAL         300000UL
#define RECONNECT_TIMEOUT          15000UL
#define DISPLAY_UPDATE_INTERVAL    1000UL
#define PERIODIC_CHECK_INTERVAL    1000UL
#define OLED_SCHEDULE_CHECK_INTERVAL 60000UL
#define RFID_FEEDBACK_DISPLAY_MS   1800UL
#define SD_REDETECT_INTERVAL       30000UL
#define MAX_TIME_ESTIMATE_AGE      43200UL
#define WDT_TIMEOUT_SEC            60
#define WDT_SYNC_TIMEOUT_MS        180000UL
#define OTA_CHECK_INTERVAL         10800000UL
#define RFID_DB_CHECK_INTERVAL     10800000UL

#define MAX_SYNC_FILES_PER_CYCLE   10
#define SYNC_AGGRESSIVE_THRESHOLD  50
#define MAX_SYNC_RETRIES           2
#define SYNC_RETRY_DELAY_MS        2000UL

#define MAX_RECORDS_PER_FILE       25
#define MAX_QUEUE_FILES            2000
#define MAX_DUPLICATE_CHECK_LINES  (MAX_RECORDS_PER_FILE + 1)
#define QUEUE_WARN_THRESHOLD       1600
#define METADATA_FILE              "/queue_meta.txt"

#define NVS_MAX_RECORDS            20
#define NVS_NAMESPACE              "presensi"
#define NVS_KEY_COUNT              "nvs_count"
#define NVS_KEY_PREFIX             "rec_"
#define NVS_KEY_LAST_TIME          "last_time"
#define NVS_KEY_TZ_OFFSET          "tz_offset"

#define FIRMWARE_VERSION           "2.3.0"

#define RFID_DB_FILE               "/rfid_db.txt"
#define NVS_KEY_RFID_VER           "rfid_db_ver"
#define RFID_CACHE_MAX             2000

#define SLEEP_START_HOUR           18
#define SLEEP_END_HOUR             5
#define OLED_DIM_START_HOUR        8
#define OLED_DIM_END_HOUR          14
#define GMT_OFFSET_SEC             25200L

#define SIGNAL_THRESHOLD_WEAK      -85
#define SIGNAL_THRESHOLD_CRITICAL  -90

#define NTP_SYNC_TIMEOUT_MS        5000
#define NTP_MAX_ATTEMPTS           3
#define TIME_VALID_YEAR_MIN        2024

const char WIFI_SSID[]       PROGMEM = "SSID_WIFI";
const char WIFI_PASSWORD[]   PROGMEM = "PasswordWifi";
const char API_SECRET_KEY[]  PROGMEM = "SecretAPIToken";
const char NTP_SERVER_1[]    PROGMEM = "pool.ntp.org";
const char NTP_SERVER_2[]    PROGMEM = "time.google.com";
const char NTP_SERVER_3[]    PROGMEM = "id.pool.ntp.org";

#ifdef DEV_MODE
const char API_BASE_URL[] PROGMEM = "http://192.168.1.254:8000";
#else
const char API_BASE_URL[] PROGMEM = "https://zedlabs.id";
#endif

RTC_DATA_ATTR time_t         lastValidTime      = 0;
RTC_DATA_ATTR bool           timeWasSynced      = false;
RTC_DATA_ATTR unsigned long  bootTime           = 0;
RTC_DATA_ATTR bool           bootTimeSet        = false;
RTC_DATA_ATTR int            currentQueueFile   = 0;
RTC_DATA_ATTR bool           rtcQueueFileValid  = false;
RTC_DATA_ATTR uint64_t       sleepDurationSeconds = 0;
RTC_DATA_ATTR long           savedTzOffset      = GMT_OFFSET_SEC;

enum ReconnectState { RECONNECT_IDLE, RECONNECT_INIT, RECONNECT_TRYING, RECONNECT_SUCCESS, RECONNECT_FAILED };
enum SaveResult     { SAVE_OK, SAVE_DUPLICATE, SAVE_QUEUE_FULL, SAVE_SD_ERROR };
enum SyncFileResult { SYNC_FILE_OK, SYNC_FILE_EMPTY, SYNC_FILE_HTTP_FAIL, SYNC_FILE_NO_WIFI };

struct Timers {
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

struct DisplayState {
    bool isOnline;
    char time[6];
    int  pendingRecords;
    int  wifiSignal;
};

struct OfflineRecord {
    char          rfid[11];
    char          timestamp[20];
    char          deviceId[20];
    unsigned long unixTime;
};

struct SyncState {
    int           currentFile;
    bool          inProgress;
    unsigned long startTime;
    int           filesProcessed;
    int           filesSucceeded;
    bool          aggressiveMode;
};

struct RfidFeedback {
    bool          active;
    unsigned long shownAt;
    bool          wasOledOff;
};

struct OtaState {
    bool updateAvailable;
    char version[16];
    char url[128];
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522          rfidReader(PIN_RFID_SS, PIN_RFID_RST);
SdFat            sd;
FsFile           file;
Preferences      prefs;

Timers       timers       = {};
DisplayState currentDisplay  = {false, "00:00", 0, 0};
DisplayState previousDisplay = {false, "--:--", -1, -1};
SyncState    syncState    = {0, false, 0, 0, 0, false};
RfidFeedback rfidFeedback = {false, 0, false};
OtaState     otaState     = {false, "", ""};

char lastUID[11]   = "";
char deviceId[20]  = "";
bool isOnline          = false;
bool sdCardAvailable   = false;
bool oledIsOn          = true;
bool wdtExtended       = false;

int  cachedPendingRecords  = 0;
bool pendingCacheDirty     = true;
int  cachedQueueFileCount  = 0;

volatile bool  sdBusy        = false;
ReconnectState reconnectState     = RECONNECT_IDLE;
unsigned long  reconnectStartTime = 0;

char **rfidCache      = nullptr;
int    rfidCacheCount = 0;
bool   rfidCacheLoaded = false;

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
int  nvsGetCount();
void nvsSetCount(int count);
bool nvsLoadRecord(int index, OfflineRecord &rec);
bool nvsSaveRecord(int index, const OfflineRecord &rec);
void nvsDeleteRecord(int index);
bool nvsSaveToBuffer(const char *rfid, const char *timestamp, unsigned long unixTime);
bool nvsSyncToServer();
bool nvsIsDuplicate(const char *rfid, unsigned long unixTime);
void nvsSaveLastTime(time_t t);
time_t nvsLoadLastTime();
void nvsSaveTzOffset(long offset);
long nvsLoadTzOffset();
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
SaveResult    saveToQueue(const char *rfid, const char *timestamp, unsigned long unixTime);
bool isWifiConnected();
bool isSignalWeak();
bool isSignalCritical();
void extendWdtForSync();
void restoreWdtNormal();
SyncFileResult     syncQueueFile(const char *filename);
bool               syncQueueFileWithRetry(const char *filename);
void               chunkedSync();
bool               shouldContinueSync();

inline void acquireSD() {
    unsigned long t = millis();
    while (sdBusy) {
        if (millis() - t > 5000) break;
        delay(5);
    }
    sdBusy = true;
}
inline void releaseSD() { sdBusy = false; }
inline void selectSD()   { digitalWrite(PIN_RFID_SS, HIGH); digitalWrite(PIN_SD_CS, LOW); }
inline void deselectSD() { digitalWrite(PIN_SD_CS, HIGH); }

#ifdef DEV_MODE
WiFiClient &getHttpClient() { static WiFiClient c; return c; }
#else
WiFiClientSecure &getHttpClient() {
    static WiFiClientSecure c;
    c.setInsecure();
    return c;
}
#endif

bool isWifiConnected()  { return WiFi.status() == WL_CONNECTED; }
bool isSignalWeak()     { return !isWifiConnected() || WiFi.RSSI() < SIGNAL_THRESHOLD_WEAK; }
bool isSignalCritical() { return !isWifiConnected() || WiFi.RSSI() < SIGNAL_THRESHOLD_CRITICAL; }

void extendWdtForSync() {
    if (wdtExtended) return;
    esp_task_wdt_delete(nullptr);
    esp_task_wdt_deinit();
    const esp_task_wdt_config_t cfg = { .timeout_ms = (uint32_t)WDT_SYNC_TIMEOUT_MS, .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_init(&cfg);
    esp_task_wdt_add(nullptr);
    wdtExtended = true;
}

void restoreWdtNormal() {
    if (!wdtExtended) return;
    esp_task_wdt_delete(nullptr);
    esp_task_wdt_deinit();
    const esp_task_wdt_config_t cfg = { .timeout_ms = WDT_TIMEOUT_SEC * 1000, .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_init(&cfg);
    esp_task_wdt_add(nullptr);
    wdtExtended = false;
}

// ============================================================
// NVS TIMEZONE
// ============================================================
void nvsSaveTzOffset(long offset) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putLong(NVS_KEY_TZ_OFFSET, offset);
    prefs.end();
}

long nvsLoadTzOffset() {
    prefs.begin(NVS_NAMESPACE, true);
    long offset = prefs.getLong(NVS_KEY_TZ_OFFSET, GMT_OFFSET_SEC);
    prefs.end();
    return offset;
}

// ============================================================
// TIME — perbaikan akurasi
// Perubahan utama:
//   1. configTime hanya dipanggil sekali di setup, bukan di setiap sync
//   2. Fallback estimasi menggunakan waktu NTP terakhir + elapsed millis
//   3. Validasi tahun menggunakan TIME_VALID_YEAR_MIN (2024)
//   4. Timezone disimpan ke NVS dan RTC, konsisten antar boot
// ============================================================

bool isTimeValid() {
    struct tm t;
    return getTimeWithFallback(&t);
}

bool getTimeWithFallback(struct tm *timeInfo) {
    if (getLocalTime(timeInfo) && timeInfo->tm_year >= (TIME_VALID_YEAR_MIN - 1900))
        return true;

    if (!timeWasSynced || lastValidTime == 0 || !bootTimeSet)
        return false;

    unsigned long elapsed = (millis() - bootTime) / 1000;
    if (elapsed > MAX_TIME_ESTIMATE_AGE)
        return false;

    time_t est = lastValidTime + (time_t)elapsed;
    struct tm *p = localtime(&est);
    if (!p) return false;
    *timeInfo = *p;
    return true;
}

void getFormattedTimestamp(char *buf, size_t bufSize) {
    struct tm t;
    if (!getTimeWithFallback(&t)) { buf[0] = '\0'; return; }
    strftime(buf, bufSize, "%Y-%m-%d %H:%M:%S", &t);
}

// Dipanggil sekali saat boot untuk set timezone ke sistem
void applyTimezone(long tzOffset) {
    char tzStr[16];
    long absOff = tzOffset < 0 ? -tzOffset : tzOffset;
    int  h = (int)(absOff / 3600);
    int  m = (int)((absOff % 3600) / 60);
    snprintf(tzStr, sizeof(tzStr), "UTC%s%d:%02d", tzOffset >= 0 ? "-" : "+", h, m);
    setenv("TZ", tzStr, 1);
    tzset();
    savedTzOffset = tzOffset;
}

// Sync NTP: configTime dipanggil sekali, setelah itu hanya sntp_restart()
// untuk menghindari timezone reset yang menyebabkan jam kacau
bool syncTimeWithFallback() {
    if (isSignalCritical()) return false;

    const char *servers[] = { NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3 };

    for (int s = 0; s < NTP_MAX_ATTEMPTS; s++) {
        char ntpSvr[32];
        strcpy_P(ntpSvr, servers[s % 3]);

        configTime(savedTzOffset, 0, ntpSvr);

        struct tm t;
        unsigned long deadline = millis() + NTP_SYNC_TIMEOUT_MS;
        while (millis() < deadline) {
            esp_task_wdt_reset();
            if (getLocalTime(&t) && t.tm_year >= (TIME_VALID_YEAR_MIN - 1900)) {
                time_t now = mktime(&t);

                lastValidTime = now;
                nvsSaveLastTime(lastValidTime);
                timeWasSynced = true;

                bootTime    = millis() - (unsigned long)((now - lastValidTime) * 1000UL);
                bootTimeSet = true;

                char buf[6];
                snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
                showOLED(F("WAKTU TERSYNC"), buf);
                delay(800);
                return true;
            }
            delay(100);
        }
    }
    return false;
}

void periodicTimeSync() {
    if (millis() - timers.lastTimeSync < TIME_SYNC_INTERVAL) return;
    timers.lastTimeSync = millis();
    if (!isSignalCritical()) syncTimeWithFallback();
}

// ============================================================
// NVS
// ============================================================
void nvsSaveLastTime(time_t t) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putULong(NVS_KEY_LAST_TIME, (unsigned long)t);
    prefs.end();
}
time_t nvsLoadLastTime() {
    prefs.begin(NVS_NAMESPACE, true);
    unsigned long t = prefs.getULong(NVS_KEY_LAST_TIME, 0);
    prefs.end();
    return (time_t)t;
}
int nvsGetCount() {
    prefs.begin(NVS_NAMESPACE, true);
    int c = prefs.getInt(NVS_KEY_COUNT, 0);
    prefs.end();
    return c;
}
void nvsSetCount(int c) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putInt(NVS_KEY_COUNT, c);
    prefs.end();
}
bool nvsLoadRecord(int idx, OfflineRecord &rec) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, idx);
    prefs.begin(NVS_NAMESPACE, true);
    size_t len = prefs.getBytesLength(key);
    if (len != sizeof(OfflineRecord)) { prefs.end(); return false; }
    prefs.getBytes(key, &rec, sizeof(OfflineRecord));
    prefs.end();
    return true;
}
bool nvsSaveRecord(int idx, const OfflineRecord &rec) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, idx);
    prefs.begin(NVS_NAMESPACE, false);
    size_t w = prefs.putBytes(key, &rec, sizeof(OfflineRecord));
    prefs.end();
    return w == sizeof(OfflineRecord);
}
void nvsDeleteRecord(int idx) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, idx);
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(key);
    prefs.end();
}
bool nvsIsDuplicate(const char *rfid, unsigned long unixTime) {
    int count = nvsGetCount();
    for (int i = 0; i < count; i++) {
        OfflineRecord rec;
        if (!nvsLoadRecord(i, rec)) continue;
        if (strcmp(rec.rfid, rfid) == 0 && unixTime >= rec.unixTime
            && (unixTime - rec.unixTime) < MIN_REPEAT_INTERVAL) return true;
    }
    return false;
}
bool nvsSaveToBuffer(const char *rfid, const char *timestamp, unsigned long unixTime) {
    if (nvsIsDuplicate(rfid, unixTime)) return false;
    int count = nvsGetCount();
    if (count >= NVS_MAX_RECORDS) return false;
    OfflineRecord rec;
    strncpy(rec.rfid, rfid, sizeof(rec.rfid) - 1);         rec.rfid[sizeof(rec.rfid) - 1] = '\0';
    strncpy(rec.timestamp, timestamp, sizeof(rec.timestamp) - 1); rec.timestamp[sizeof(rec.timestamp) - 1] = '\0';
    strncpy(rec.deviceId, deviceId, sizeof(rec.deviceId) - 1);   rec.deviceId[sizeof(rec.deviceId) - 1] = '\0';
    rec.unixTime = unixTime;
    if (!nvsSaveRecord(count, rec)) return false;
    nvsSetCount(count + 1);
    return true;
}
bool nvsSyncToServer() {
    int count = nvsGetCount();
    if (count == 0) return true;
    if (isSignalCritical()) return false;

    HTTPClient http;
    http.setTimeout(30000);
    http.setConnectTimeout(10000);
    char url[80];
    strcpy_P(url, API_BASE_URL);
    strcat_P(url, PSTR("/api/presensi/sync-bulk"));
    if (!http.begin(getHttpClient(), url)) return false;
    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32]; strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);

    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("data");
    for (int i = 0; i < count; i++) {
        OfflineRecord rec;
        if (!nvsLoadRecord(i, rec)) continue;
        JsonObject o = arr.createNestedObject();
        o["rfid"] = rec.rfid; o["timestamp"] = rec.timestamp;
        o["device_id"] = rec.deviceId; o["sync_mode"] = true;
    }
    String payload; serializeJson(doc, payload); doc.clear();
    esp_task_wdt_reset();
    int code = http.POST(payload);
    if (code == 200) {
        String body = http.getString();
        esp_task_wdt_reset();
        http.end();
        DynamicJsonDocument res(4096);
        if (deserializeJson(res, body) == DeserializationError::Ok) {
            for (JsonObject item : res["data"].as<JsonArray>()) {
                if (strcmp(item["status"] | "error", "error") == 0)
                    appendFailedLog(item["rfid"] | "unknown", item["timestamp"] | "unknown", item["message"] | "UNKNOWN");
            }
        }
        nvsSetCount(0);
        for (int i = 0; i < count; i++) nvsDeleteRecord(i);
        return true;
    }
    http.end();
    return false;
}

// ============================================================
// RFID LOCAL DB
// ============================================================
unsigned long nvsGetRfidDbVer() {
    prefs.begin(NVS_NAMESPACE, true);
    unsigned long v = prefs.getULong(NVS_KEY_RFID_VER, 0);
    prefs.end(); return v;
}
void nvsSetRfidDbVer(unsigned long v) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putULong(NVS_KEY_RFID_VER, v);
    prefs.end();
}
void freeRfidCache() {
    if (rfidCache) {
        for (int i = 0; i < rfidCacheCount; i++) if (rfidCache[i]) free(rfidCache[i]);
        free(rfidCache); rfidCache = nullptr;
    }
    rfidCacheCount = 0; rfidCacheLoaded = false;
}
bool loadRfidCacheFromFile() {
    freeRfidCache();
    if (!sdCardAvailable) return false;
    acquireSD(); selectSD();
    if (!sd.exists(RFID_DB_FILE)) { deselectSD(); releaseSD(); return false; }
    FsFile dbFile;
    if (!dbFile.open(RFID_DB_FILE, O_RDONLY)) { deselectSD(); releaseSD(); return false; }
    int count = 0;
    char line[12];
    while (dbFile.fgets(line, sizeof(line)) > 0) {
        esp_task_wdt_reset();
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len != 10) continue;
        bool ok = true;
        for (int j = 0; j < 10; j++) if (!isdigit((unsigned char)line[j])) { ok = false; break; }
        if (ok) count++;
    }
    dbFile.seekSet(0);
    rfidCache = (char **)malloc(count * sizeof(char *));
    if (!rfidCache) { dbFile.close(); deselectSD(); releaseSD(); return false; }
    int idx = 0;
    while (dbFile.fgets(line, sizeof(line)) > 0 && idx < count) {
        esp_task_wdt_reset();
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len != 10) continue;
        bool ok = true;
        for (int j = 0; j < 10; j++) if (!isdigit((unsigned char)line[j])) { ok = false; break; }
        if (ok) { rfidCache[idx] = (char *)malloc(11); if (rfidCache[idx]) { memcpy(rfidCache[idx], line, 11); idx++; } }
    }
    dbFile.close(); deselectSD(); releaseSD();
    rfidCacheCount = idx; rfidCacheLoaded = true;
    return true;
}
bool isRfidInCache(const char *rfid) {
    if (!rfidCacheLoaded || rfidCacheCount == 0) return true;
    for (int i = 0; i < rfidCacheCount; i++)
        if (rfidCache[i] && strcmp(rfidCache[i], rfid) == 0) return true;
    return false;
}
unsigned long checkRfidDbVersion() {
    if (isSignalWeak()) return 0;
    HTTPClient http; http.setTimeout(8000); http.setConnectTimeout(5000);
    char url[80]; strcpy_P(url, API_BASE_URL); strcat_P(url, PSTR("/api/presensi/rfid-list/version"));
    if (!http.begin(getHttpClient(), url)) return 0;
    char apiKey[32]; strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);
    int code = http.GET();
    if (code != 200) { http.end(); return 0; }
    String body = http.getString(); http.end();
    DynamicJsonDocument doc(128);
    if (deserializeJson(doc, body) != DeserializationError::Ok) return 0;
    return doc["ver"] | 0UL;
}
bool downloadRfidDb() {
    if (isSignalWeak() || !sdCardAvailable) return false;
    showOLED(F("RFID DB"), "MENGUNDUH...");
    HTTPClient http; http.setTimeout(30000); http.setConnectTimeout(10000);
    char url[80]; strcpy_P(url, API_BASE_URL); strcat_P(url, PSTR("/api/presensi/rfid-list"));
    if (!http.begin(getHttpClient(), url)) return false;
    char apiKey[32]; strcpy_P(apiKey, API_SECRET_KEY);
    http.addHeader(F("X-API-KEY"), apiKey);
    int code = http.GET();
    if (code != 200) { http.end(); showOLED(F("RFID DB"), "GAGAL UNDUH"); playToneError(); delay(800); return false; }
    acquireSD(); selectSD();
    const char *tmp = "/rfid_db.tmp";
    if (sd.exists(tmp)) sd.remove(tmp);
    FsFile dbFile;
    if (!dbFile.open(tmp, O_WRONLY | O_CREAT | O_TRUNC)) { deselectSD(); releaseSD(); http.end(); return false; }
    WiFiClient *stream = http.getStreamPtr();
    int total = http.getSize(), written = 0;
    unsigned long serverVer = 0;
    bool firstLine = true;
    char lineBuf[32]; int lPos = 0;
    uint8_t chunk[256];
    while (http.connected() && (total < 0 || written < total)) {
        esp_task_wdt_reset();
        int avail = stream->available();
        if (!avail) { delay(5); continue; }
        int rd = stream->readBytes(chunk, min(avail, (int)sizeof(chunk)));
        for (int i = 0; i < rd; i++) {
            char c = (char)chunk[i];
            if (c == '\r') continue;
            if (c == '\n') {
                lineBuf[lPos] = '\0'; lPos = 0;
                if (firstLine) { firstLine = false; if (strncmp(lineBuf, "ver:", 4) == 0) { serverVer = strtoul(lineBuf + 4, nullptr, 10); continue; } }
                if (strlen(lineBuf) == 10) {
                    bool ok = true;
                    for (int j = 0; j < 10; j++) if (!isdigit((unsigned char)lineBuf[j])) { ok = false; break; }
                    if (ok) { dbFile.print(lineBuf); dbFile.print('\n'); written++; }
                }
            } else { if (lPos < (int)sizeof(lineBuf) - 1) lineBuf[lPos++] = c; }
        }
    }
    http.end(); dbFile.sync(); dbFile.close();
    if (sd.exists(RFID_DB_FILE)) sd.remove(RFID_DB_FILE);
    sd.rename(tmp, RFID_DB_FILE);
    deselectSD(); releaseSD();
    if (serverVer > 0) nvsSetRfidDbVer(serverVer);
    loadRfidCacheFromFile();
    char buf[20]; snprintf(buf, sizeof(buf), "%d RFID", written);
    showOLED(F("RFID DB"), buf); playToneSuccess(); delay(800);
    return true;
}
void checkAndUpdateRfidDb() {
    if (!sdCardAvailable || isSignalWeak()) return;
    if (millis() - timers.lastRfidDbCheck < RFID_DB_CHECK_INTERVAL) return;
    timers.lastRfidDbCheck = millis();

    acquireSD(); selectSD();
    bool dbExists = sd.exists(RFID_DB_FILE);
    deselectSD(); releaseSD();

    if (!dbExists) { downloadRfidDb(); return; }
    if (checkRfidDbVersion() > nvsGetRfidDbVer()) downloadRfidDb();
}

// ============================================================
// OTA
// ============================================================
void checkOtaUpdate() {
    if (isSignalWeak()) return;
    if (millis() - timers.lastOtaCheck < OTA_CHECK_INTERVAL) return;
    timers.lastOtaCheck = millis();
    HTTPClient http; http.setTimeout(8000); http.setConnectTimeout(5000);
    char url[80]; strcpy_P(url, API_BASE_URL); strcat_P(url, PSTR("/api/presensi/firmware/check"));
    if (!http.begin(getHttpClient(), url)) return;
    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32]; strcpy_P(apiKey, API_SECRET_KEY); http.addHeader(F("X-API-KEY"), apiKey);
    char payload[64]; snprintf(payload, sizeof(payload), "{\"version\":\"%s\",\"device_id\":\"%s\"}", FIRMWARE_VERSION, deviceId);
    int code = http.POST(payload);
    if (code != 200) { http.end(); return; }
    String body = http.getString(); http.end();
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body) != DeserializationError::Ok) return;
    bool has = doc["update"] | false;
    const char *ver = doc["version"] | "";
    const char *bin = doc["url"] | "";
    if (!has || !strlen(ver) || !strlen(bin)) return;
    strncpy(otaState.version, ver, sizeof(otaState.version) - 1); otaState.version[sizeof(otaState.version) - 1] = '\0';
    strncpy(otaState.url, bin, sizeof(otaState.url) - 1);         otaState.url[sizeof(otaState.url) - 1] = '\0';
    otaState.updateAvailable = true;
    char buf[20]; snprintf(buf, sizeof(buf), "v%s TERSEDIA", otaState.version);
    showOLED(F("UPDATE"), buf); playToneNotify(); delay(2000);
}
void performOtaUpdate() {
    if (!otaState.updateAvailable || isSignalWeak()) return;
    char buf[20]; snprintf(buf, sizeof(buf), "v%s", otaState.version);
    showOLED(F("UPDATE OTA"), buf); delay(500);
    showOLED(F("MENGUNDUH"), "MOHON TUNGGU...");
    extendWdtForSync();
#ifdef DEV_MODE
    WiFiClient otaClient;
#else
    WiFiClientSecure otaClient; otaClient.setInsecure();
#endif
    HTTPClient http; http.begin(otaClient, otaState.url);
    http.addHeader(F("X-API-KEY"), API_SECRET_KEY); http.setTimeout(60000);
    int code = http.GET();
    if (code != 200) {
        snprintf(buf, sizeof(buf), "HTTP ERR %d", code);
        showOLED(F("UPDATE GAGAL"), buf); playToneError(); http.end();
        otaState.updateAvailable = false; restoreWdtNormal(); return;
    }
    int total = http.getSize();
    WiFiClient *stream = http.getStreamPtr();
    if (!Update.begin(total)) {
        showOLED(F("UPDATE GAGAL"), "NO SPACE"); playToneError(); http.end();
        otaState.updateAvailable = false; restoreWdtNormal(); return;
    }
    uint8_t buff[1024]; int written = 0;
    while (http.connected() && written < total) {
        size_t avail = stream->available();
        if (avail) { int rd = stream->readBytes(buff, min((size_t)sizeof(buff), avail)); Update.write(buff, rd); written += rd; }
        esp_task_wdt_reset(); delay(1);
    }
    http.end();
    if (Update.end() && Update.isFinished()) {
        showOLED(F("UPDATE OK"), "RESTART..."); playToneSuccess(); delay(2000); ESP.restart();
    } else {
        snprintf(buf, sizeof(buf), "ERR %d", Update.getError());
        showOLED(F("UPDATE GAGAL"), buf); playToneError(); otaState.updateAvailable = false;
    }
    restoreWdtNormal(); memset(&previousDisplay, 0xFF, sizeof(previousDisplay));
}

// ============================================================
// OLED
// ============================================================
void turnOffOLED() {
    if (!oledIsOn) return;
    display.clearDisplay(); display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF); oledIsOn = false;
}
void turnOnOLED() {
    if (oledIsOn) return;
    display.ssd1306_command(SSD1306_DISPLAYON); oledIsOn = true;
    memset(&previousDisplay, 0xFF, sizeof(previousDisplay));
}
void checkOLEDSchedule() {
    if (millis() - timers.lastOLEDScheduleCheck < OLED_SCHEDULE_CHECK_INTERVAL) return;
    timers.lastOLEDScheduleCheck = millis();
    struct tm t; if (!getTimeWithFallback(&t)) return;
    int h = t.tm_hour;
    if (h >= OLED_DIM_START_HOUR && h < OLED_DIM_END_HOUR) turnOffOLED(); else turnOnOLED();
}

// ============================================================
// SD CARD
// ============================================================
bool reinitSDCard() {
    if (file.isOpen()) file.close();
    sd.end(); delay(100); selectSD(); delay(10);
    bool ok = sd.begin(PIN_SD_CS, SD_SCK_MHZ(10)); deselectSD(); return ok;
}
void checkSDHealth() {
    if (millis() - timers.lastSDRedetect < SD_REDETECT_INTERVAL) return;
    timers.lastSDRedetect = millis();
    if (!sdCardAvailable) {
        acquireSD();
        bool ok = reinitSDCard(); releaseSD();
        if (ok) {
            sdCardAvailable = true; pendingCacheDirty = true;
            showOLED(F("SD CARD"), "TERBACA KEMBALI"); playToneSuccess(); delay(800);
            loadRfidCacheFromFile();
        }
        return;
    }
    acquireSD(); selectSD();
    bool healthy = sd.vol()->fatType() > 0;
    deselectSD(); releaseSD();
    if (!healthy) {
        sdCardAvailable = false; freeRfidCache();
        showOLED(F("SD CARD"), "TERLEPAS!"); playToneError(); delay(800);
    }
}
void getQueueFileName(int idx, char *buf, size_t sz) { snprintf(buf, sz, "/queue_%d.csv", idx); }
int countRecordsInFile(const char *filename) {
    if (!sdCardAvailable) return 0;
    selectSD();
    if (!file.open(filename, O_RDONLY)) { deselectSD(); return 0; }
    int count = 0; char line[128];
    if (file.available()) file.fgets(line, sizeof(line));
    while (file.fgets(line, sizeof(line)) > 0) if (strlen(line) > 10) count++;
    file.close(); deselectSD(); return count;
}
int countAllOfflineRecords() {
    if (!sdCardAvailable) return 0;
    int total = 0; cachedQueueFileCount = 0;
    char fn[20]; selectSD();
    for (int i = 0; i < MAX_QUEUE_FILES; i++) {
        esp_task_wdt_reset();
        getQueueFileName(i, fn, sizeof(fn));
        if (!sd.exists(fn)) continue;
        cachedQueueFileCount++;
        if (!file.open(fn, O_RDONLY)) continue;
        char line[128];
        if (file.available()) file.fgets(line, sizeof(line));
        while (file.fgets(line, sizeof(line)) > 0) { esp_task_wdt_reset(); if (strlen(line) > 10) total++; }
        file.close();
    }
    deselectSD(); return total;
}
void refreshPendingCache() {
    if (!pendingCacheDirty) return;
    cachedPendingRecords = countAllOfflineRecords();
    pendingCacheDirty = false; saveMetadata();
}
void saveMetadata() {
    if (!sdCardAvailable) return;
    selectSD();
    if (file.open(METADATA_FILE, O_WRONLY | O_CREAT | O_TRUNC)) {
        file.print(cachedPendingRecords); file.print(","); file.println(currentQueueFile);
        file.sync(); file.close();
    }
    deselectSD();
}
void loadMetadata() {
    if (!sdCardAvailable) return;
    selectSD();
    if (!sd.exists(METADATA_FILE)) { deselectSD(); return; }
    if (!file.open(METADATA_FILE, O_RDONLY)) { deselectSD(); return; }
    char line[32];
    if (file.fgets(line, sizeof(line)) > 0) {
        char *comma = strchr(line, ',');
        if (comma) {
            *comma = '\0';
            cachedPendingRecords = atoi(line);
            currentQueueFile = atoi(comma + 1);
            rtcQueueFileValid = true; pendingCacheDirty = false;
        }
    }
    file.close(); deselectSD();
}
bool initSDCard() {
    pinMode(PIN_SD_CS, OUTPUT); pinMode(PIN_RFID_SS, OUTPUT);
    deselectSD(); digitalWrite(PIN_RFID_SS, HIGH);
    selectSD(); delay(10);
    if (!sd.begin(PIN_SD_CS, SD_SCK_MHZ(10))) { deselectSD(); return false; }
    loadMetadata();
    if (!rtcQueueFileValid) {
        currentQueueFile = -1; char fn[20];
        for (int i = 0; i < MAX_QUEUE_FILES; i++) {
            esp_task_wdt_reset();
            getQueueFileName(i, fn, sizeof(fn));
            if (!sd.exists(fn)) {
                if (file.open(fn, O_WRONLY | O_CREAT)) { file.println("rfid,timestamp,device_id,unix_time"); file.close(); currentQueueFile = i; break; }
            } else {
                int cnt = 0;
                if (file.open(fn, O_RDONLY)) {
                    char line[128];
                    if (file.available()) file.fgets(line, sizeof(line));
                    while (file.fgets(line, sizeof(line)) > 0) { esp_task_wdt_reset(); if (strlen(line) > 10) cnt++; }
                    file.close();
                }
                if (cnt < MAX_RECORDS_PER_FILE) { currentQueueFile = i; break; }
            }
        }
        if (currentQueueFile == -1) currentQueueFile = 0;
        rtcQueueFileValid = true;
    }
    deselectSD(); return true;
}

// ============================================================
// DUPLICATE CHECK
// ============================================================
bool isDuplicateInternal(const char *rfid, unsigned long unixTime) {
    bool found = false;
    char fn[20];
    for (int offset = 0; offset < min(3, MAX_QUEUE_FILES) && !found; offset++) {
        esp_task_wdt_reset();
        int idx = (currentQueueFile - offset + MAX_QUEUE_FILES) % MAX_QUEUE_FILES;
        getQueueFileName(idx, fn, sizeof(fn));
        if (!sd.exists(fn) || !file.open(fn, O_RDONLY)) continue;
        char line[128];
        if (file.available()) file.fgets(line, sizeof(line));
        int linesRead = 0;
        while (file.fgets(line, sizeof(line)) > 0 && linesRead < MAX_DUPLICATE_CHECK_LINES) {
            esp_task_wdt_reset();
            int len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
            char *c1 = strchr(line, ','), *c3 = strrchr(line, ',');
            if (c1 && c3 && c3 > c1) {
                *c1 = '\0';
                unsigned long ft = strtoul(c3 + 1, nullptr, 10);
                if (strcmp(line, rfid) == 0 && (unixTime - ft) < MIN_REPEAT_INTERVAL) { found = true; file.close(); break; }
                *c1 = ',';
            }
            linesRead++;
        }
        if (file.isOpen()) file.close();
    }
    return found;
}

// ============================================================
// SAVE TO QUEUE
// ============================================================
SaveResult saveToQueue(const char *rfid, const char *timestamp, unsigned long unixTime) {
    if (!sdCardAvailable || sdBusy) return SAVE_SD_ERROR;
    acquireSD(); selectSD();
    if (isDuplicateInternal(rfid, unixTime)) { deselectSD(); releaseSD(); return SAVE_DUPLICATE; }
    if (currentQueueFile < 0 || currentQueueFile >= MAX_QUEUE_FILES) currentQueueFile = 0;
    char curFile[20]; getQueueFileName(currentQueueFile, curFile, sizeof(curFile));
    if (!sd.exists(curFile)) {
        if (file.open(curFile, O_WRONLY | O_CREAT)) { file.println("rfid,timestamp,device_id,unix_time"); file.close(); }
    }
    int cnt = 0;
    if (file.open(curFile, O_RDONLY)) {
        char line[128];
        if (file.available()) file.fgets(line, sizeof(line));
        while (file.fgets(line, sizeof(line)) > 0) { esp_task_wdt_reset(); if (strlen(line) > 10) cnt++; }
        file.close();
    }
    if (cnt >= MAX_RECORDS_PER_FILE) {
        int next = (currentQueueFile + 1) % MAX_QUEUE_FILES;
        char nextFn[20]; getQueueFileName(next, nextFn, sizeof(nextFn));
        if (sd.exists(nextFn)) {
            int nc = countRecordsInFile(nextFn);
            if (nc > 0) { deselectSD(); releaseSD(); return SAVE_QUEUE_FULL; }
            sd.remove(nextFn);
        }
        currentQueueFile = next;
        getQueueFileName(currentQueueFile, curFile, sizeof(curFile));
        if (!file.open(curFile, O_WRONLY | O_CREAT)) { deselectSD(); releaseSD(); return SAVE_SD_ERROR; }
        file.println("rfid,timestamp,device_id,unix_time"); file.close();
    }
    if (!file.open(curFile, O_WRONLY | O_APPEND)) { deselectSD(); releaseSD(); return SAVE_SD_ERROR; }
    file.print(rfid); file.print(","); file.print(timestamp); file.print(",");
    file.print(deviceId); file.print(","); file.println(unixTime);
    file.sync(); file.close();
    deselectSD(); releaseSD();
    cachedPendingRecords++;
    pendingCacheDirty = false;
    saveMetadata();
    return SAVE_OK;
}

// ============================================================
// FAILED LOG
// ============================================================
void appendFailedLog(const char *rfid, const char *timestamp, const char *reason) {
    if (!sdCardAvailable) return;
    acquireSD(); selectSD();
    if (file.open("/failed_log.csv", O_WRONLY | O_CREAT | O_APPEND)) {
        if (file.size() == 0) file.println("rfid,timestamp,reason");
        file.print(rfid); file.print(","); file.print(timestamp); file.print(","); file.println(reason);
        file.sync(); file.close();
    }
    deselectSD(); releaseSD();
}

// ============================================================
// SYNC
// ============================================================
bool readQueueFile(const char *filename, OfflineRecord *records, int *count, int maxCount) {
    if (!sdCardAvailable) return false;
    selectSD();
    if (!file.open(filename, O_RDONLY)) { deselectSD(); return false; }
    *count = 0;
    time_t now = time(nullptr);
    char line[128];
    if (file.available()) file.fgets(line, sizeof(line));
    while (file.available() && *count < maxCount) {
        esp_task_wdt_reset();
        if (file.fgets(line, sizeof(line)) <= 0) continue;
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len < 10) continue;
        char *c1 = strchr(line, ','); if (!c1) continue;
        char *c2 = strchr(c1+1, ','); if (!c2) continue;
        char *c3 = strchr(c2+1, ','); if (!c3) continue;
        int rLen = c1 - line, tLen = c2 - c1 - 1, dLen = c3 - c2 - 1;
        if (rLen <= 0 || tLen <= 0) continue;
        strncpy(records[*count].rfid, line, min(rLen, 10)); records[*count].rfid[min(rLen, 10)] = '\0';
        strncpy(records[*count].timestamp, c1+1, min(tLen, 19)); records[*count].timestamp[min(tLen, 19)] = '\0';
        strncpy(records[*count].deviceId, c2+1, min(dLen, 19)); records[*count].deviceId[min(dLen, 19)] = '\0';
        records[*count].unixTime = strtoul(c3+1, nullptr, 10);
        if (records[*count].timestamp[0] == '\0') appendFailedLog(records[*count].rfid, "empty", "TIMESTAMP_KOSONG");
        else if ((unsigned long)now - records[*count].unixTime <= MAX_OFFLINE_AGE) (*count)++;
    }
    file.close(); deselectSD();
    return *count > 0;
}

SyncFileResult syncQueueFile(const char *filename) {
    if (!sdCardAvailable) return SYNC_FILE_NO_WIFI;
    if (!isWifiConnected()) return SYNC_FILE_NO_WIFI;
    OfflineRecord records[MAX_RECORDS_PER_FILE];
    int validCount = 0;
    if (!readQueueFile(filename, records, &validCount, MAX_RECORDS_PER_FILE) || validCount == 0) {
        acquireSD(); selectSD(); sd.remove(filename); deselectSD(); releaseSD();
        pendingCacheDirty = true; return SYNC_FILE_EMPTY;
    }
    HTTPClient http; http.setTimeout(45000); http.setConnectTimeout(15000);
    char url[80]; strcpy_P(url, API_BASE_URL); strcat_P(url, PSTR("/api/presensi/sync-bulk"));
    if (!http.begin(getHttpClient(), url)) return SYNC_FILE_HTTP_FAIL;
    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32]; strcpy_P(apiKey, API_SECRET_KEY); http.addHeader(F("X-API-KEY"), apiKey);
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("data");
    for (int i = 0; i < validCount; i++) {
        JsonObject o = arr.createNestedObject();
        o["rfid"] = records[i].rfid; o["timestamp"] = records[i].timestamp;
        o["device_id"] = records[i].deviceId; o["sync_mode"] = true;
    }
    String payload; serializeJson(doc, payload); doc.clear();
    esp_task_wdt_reset();
    int code = http.POST(payload);
    esp_task_wdt_reset();
    if (code == 200) {
        String body = http.getString(); esp_task_wdt_reset(); http.end();
        DynamicJsonDocument res(4096);
        if (deserializeJson(res, body) == DeserializationError::Ok) {
            for (JsonObject item : res["data"].as<JsonArray>()) {
                if (strcmp(item["status"] | "error", "error") == 0)
                    appendFailedLog(item["rfid"] | "unknown", item["timestamp"] | "unknown", item["message"] | "UNKNOWN");
            }
        }
        acquireSD(); selectSD(); sd.remove(filename); deselectSD(); releaseSD();
        pendingCacheDirty = true;
        if (cachedPendingRecords >= validCount) cachedPendingRecords -= validCount; else cachedPendingRecords = 0;
        return SYNC_FILE_OK;
    }
    http.end();
    if (!isWifiConnected()) { syncState.inProgress = false; return SYNC_FILE_NO_WIFI; }
    return SYNC_FILE_HTTP_FAIL;
}

bool syncQueueFileWithRetry(const char *filename) {
    for (int attempt = 0; attempt <= MAX_SYNC_RETRIES; attempt++) {
        esp_task_wdt_reset();
        if (!isWifiConnected()) { syncState.inProgress = false; return false; }
        SyncFileResult r = syncQueueFile(filename);
        if (r == SYNC_FILE_OK || r == SYNC_FILE_EMPTY) return true;
        if (r == SYNC_FILE_NO_WIFI) { syncState.inProgress = false; return false; }
        if (attempt < MAX_SYNC_RETRIES) {
            char buf[20]; snprintf(buf, sizeof(buf), "RETRY %d/%d...", attempt + 1, MAX_SYNC_RETRIES);
            showOLED(F("SYNC ULANG"), buf);
            unsigned long backoff = SYNC_RETRY_DELAY_MS * (unsigned long)(1 << attempt);
            unsigned long end = millis() + backoff;
            while (millis() < end) { esp_task_wdt_reset(); delay(100); }
        }
    }
    return false;
}

// ============================================================
// shouldContinueSync — tentukan apakah sync harus jalan terus
// Di mode agresif (data banyak): tidak ada batas file per siklus
// Di mode normal: batas MAX_SYNC_FILES_PER_CYCLE
// ============================================================
bool shouldContinueSync() {
    if (!isWifiConnected()) return false;
    if (syncState.aggressiveMode) return true;
    return syncState.filesProcessed < MAX_SYNC_FILES_PER_CYCLE;
}

// ============================================================
// chunkedSync — perbaikan utama:
//   1. Mode agresif: jalan terus sampai semua file habis jika data > threshold
//   2. Melanjutkan dari currentFile, tidak mulai dari 0 setiap siklus
//   3. Setelah satu siklus selesai (currentFile >= MAX_QUEUE_FILES),
//      reset dan mulai lagi jika masih ada data (mode agresif)
//   4. WDT di-extend sekali saja
// ============================================================
void chunkedSync() {
    if (!sdCardAvailable || !isWifiConnected()) { syncState.inProgress = false; return; }

    extendWdtForSync();

    if (!syncState.inProgress) {
        syncState.inProgress       = true;
        syncState.currentFile      = 0;
        syncState.startTime        = millis();
        syncState.filesProcessed   = 0;
        syncState.filesSucceeded   = 0;
        syncState.aggressiveMode   = (cachedPendingRecords >= SYNC_AGGRESSIVE_THRESHOLD);
    }

    char fn[20];

    while (syncState.currentFile < MAX_QUEUE_FILES && shouldContinueSync()) {
        if (!isWifiConnected()) { syncState.inProgress = false; break; }
        esp_task_wdt_reset();
        getQueueFileName(syncState.currentFile, fn, sizeof(fn));

        acquireSD(); selectSD();
        bool exists = sd.exists(fn);
        deselectSD(); releaseSD();

        if (exists) {
            acquireSD();
            int recs = countRecordsInFile(fn);
            releaseSD();

            if (recs > 0) {
                char buf[24];
                snprintf(buf, sizeof(buf), "FILE %d (%d)", syncState.currentFile, recs);
                showOLED(F("SYNC"), buf);

                bool ok = syncQueueFileWithRetry(fn);
                syncState.filesProcessed++;
                if (ok)  syncState.filesSucceeded++;
                else if (!isWifiConnected()) { syncState.inProgress = false; break; }
            } else {
                acquireSD(); selectSD(); sd.remove(fn); deselectSD(); releaseSD();
                pendingCacheDirty = true;
            }
        }
        syncState.currentFile++;
        yield(); esp_task_wdt_reset();
    }

    if (syncState.currentFile >= MAX_QUEUE_FILES) {
        refreshPendingCache();

        // Jika masih ada data dan mode agresif, lanjutkan dari file 0 lagi
        if (syncState.aggressiveMode && cachedPendingRecords > 0 && isWifiConnected()) {
            syncState.currentFile    = 0;
            syncState.filesProcessed = 0;
            // Tetap inProgress = true, akan dilanjutkan loop berikutnya
        } else {
            syncState.inProgress     = false;
            syncState.currentFile    = 0;
            syncState.filesProcessed = 0;

            if (syncState.filesSucceeded > 0) {
                if (cachedPendingRecords == 0) {
                    showOLED(F("SYNC"), "SELESAI!");
                    playToneSuccess(); delay(500);
                } else {
                    char buf[20];
                    snprintf(buf, sizeof(buf), "SISA %d", cachedPendingRecords);
                    showOLED(F("SYNC PARSIAL"), buf); delay(500);
                }
            }
            syncState.filesSucceeded = 0;
        }
    }

    restoreWdtNormal();
}

// ============================================================
// NETWORK
// ============================================================
bool connectToWiFi() {
    WiFi.mode(WIFI_STA); WiFi.disconnect(true); delay(100);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setSleep(WIFI_PS_MAX_MODEM);
    WiFi.persistent(true); WiFi.setAutoReconnect(true);
    char s[32], p[32]; strcpy_P(s, WIFI_SSID); strcpy_P(p, WIFI_PASSWORD);
    WiFi.begin(s, p);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
        esp_task_wdt_reset();
        display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
        display.setCursor((SCREEN_WIDTH - (int)strlen(s) * 6) / 2, 10); display.println(s);
        display.setCursor(35, 30); display.print(F("CONNECTING"));
        for (int j = 0; j < (i % 4); j++) display.print('.');
        display.display(); delay(300);
    }
    if (WiFi.status() == WL_CONNECTED) {
        char buf[20]; snprintf(buf, sizeof(buf), "RSSI: %ld dBm", WiFi.RSSI());
        showOLED(F("WIFI OK"), buf); isOnline = true; delay(1500); return true;
    }
    isOnline = false; return false;
}
bool pingAPI() {
    if (isSignalCritical()) return false;
    HTTPClient http; http.setTimeout(5000); http.setConnectTimeout(3000);
    char url[80]; strcpy_P(url, API_BASE_URL); strcat_P(url, PSTR("/api/presensi/ping"));
    http.begin(getHttpClient(), url);
    char apiKey[32]; strcpy_P(apiKey, API_SECRET_KEY); http.addHeader(F("X-API-KEY"), apiKey);
    int code = http.GET(); http.end();
    isOnline = (code == 200); return isOnline;
}
void offlineBootFallback() { showOLED(F("NO WIFI"), "OFFLINE MODE"); playToneError(); delay(1500); }

// ============================================================
// RECONNECT STATE MACHINE
// ============================================================
void processReconnect() {
    switch (reconnectState) {
    case RECONNECT_IDLE:
        if (!isWifiConnected() && millis() - timers.lastReconnect >= RECONNECT_INTERVAL) {
            timers.lastReconnect = millis(); reconnectState = RECONNECT_INIT;
        }
        break;
    case RECONNECT_INIT:
        WiFi.disconnect(true); delay(100);
        WiFi.setTxPower(WIFI_POWER_19_5dBm); WiFi.setSleep(WIFI_PS_MAX_MODEM);
        { char s[32], p[32]; strcpy_P(s, WIFI_SSID); strcpy_P(p, WIFI_PASSWORD); WiFi.begin(s, p); }
        reconnectStartTime = millis(); reconnectState = RECONNECT_TRYING;
        break;
    case RECONNECT_TRYING:
        if (isWifiConnected())                                       reconnectState = RECONNECT_SUCCESS;
        else if (millis() - reconnectStartTime >= RECONNECT_TIMEOUT) reconnectState = RECONNECT_FAILED;
        break;
    case RECONNECT_SUCCESS:
        isOnline = true;
        if (!isSignalCritical()) {
            syncTimeWithFallback();
            if (nvsGetCount() > 0) nvsSyncToServer();
            if (sdCardAvailable && isWifiConnected()) {
                refreshPendingCache();
                if (cachedPendingRecords > 0) {
                    char buf[20]; snprintf(buf, sizeof(buf), "%d RECORDS", cachedPendingRecords);
                    showOLED(F("SYNCING"), buf);
                    syncState.inProgress = false; syncState.currentFile = 0;
                    timers.lastSync = millis();
                    chunkedSync();
                }
            }
        }
        reconnectState = RECONNECT_IDLE;
        break;
    case RECONNECT_FAILED:
        isOnline = false; reconnectState = RECONNECT_IDLE;
        break;
    }
}

// ============================================================
// RFID
// ============================================================
void uidToString(uint8_t *uid, uint8_t len, char *out) {
    if (len >= 4) {
        uint32_t v = ((uint32_t)uid[3] << 24) | ((uint32_t)uid[2] << 16) | ((uint32_t)uid[1] << 8) | uid[0];
        sprintf(out, "%010lu", v);
    } else {
        sprintf(out, "%02X%02X", uid[0], uid[1]);
    }
}
bool kirimLangsung(const char *rfid, const char *timestamp, char *message) {
    if (!isWifiConnected()) return false;
    HTTPClient http; http.setTimeout(10000); http.setConnectTimeout(5000);
    char url[80]; strcpy_P(url, API_BASE_URL); strcat_P(url, PSTR("/api/presensi"));
    if (!http.begin(getHttpClient(), url)) return false;
    http.addHeader(F("Content-Type"), F("application/json"));
    char apiKey[32]; strcpy_P(apiKey, API_SECRET_KEY); http.addHeader(F("X-API-KEY"), apiKey);
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"rfid\":\"%s\",\"timestamp\":\"%s\",\"device_id\":\"%s\",\"sync_mode\":false}", rfid, timestamp, deviceId);
    int code = http.POST(payload); http.end();
    if (code == 200)  { strcpy(message, "PRESENSI OK"); return true; }
    if (code == 400)  { strcpy(message, "CUKUP SEKALI!"); return false; }
    if (code == 404)  { strcpy(message, "RFID UNKNOWN"); return false; }
    snprintf(message, 32, "SERVER ERR %d", code); return false;
}
bool kirimPresensi(const char *rfid, char *message) {
    if (!isTimeValid()) { strcpy(message, "WAKTU INVALID"); return false; }
    char timestamp[20]; getFormattedTimestamp(timestamp, sizeof(timestamp));
    time_t now = time(nullptr);

    if (sdCardAvailable) {
        if (!isRfidInCache(rfid)) { strcpy(message, "HUBUNGI ADMIN"); return false; }
        SaveResult r = saveToQueue(rfid, timestamp, (unsigned long)now);
        switch (r) {
        case SAVE_OK:        strcpy(message, cachedQueueFileCount >= QUEUE_WARN_THRESHOLD ? "QUEUE HAMPIR PENUH!" : "DATA TERSIMPAN"); return true;
        case SAVE_DUPLICATE: strcpy(message, "CUKUP SEKALI!"); return false;
        case SAVE_QUEUE_FULL:strcpy(message, "QUEUE PENUH!"); return false;
        default:             strcpy(message, "SD CARD ERROR"); return false;
        }
    }
    if (isWifiConnected()) {
        if (kirimLangsung(rfid, timestamp, message)) return true;
        if (nvsIsDuplicate(rfid, (unsigned long)now)) { strcpy(message, "CUKUP SEKALI!"); return false; }
        if (nvsSaveToBuffer(rfid, timestamp, (unsigned long)now)) {
            char buf[24]; snprintf(buf, sizeof(buf), "BUFFER %d/%d", nvsGetCount(), NVS_MAX_RECORDS);
            strcpy(message, buf); return true;
        }
        strcpy(message, "BUFFER PENUH!"); return false;
    }
    if (nvsIsDuplicate(rfid, (unsigned long)now)) { strcpy(message, "CUKUP SEKALI!"); return false; }
    if (nvsSaveToBuffer(rfid, timestamp, (unsigned long)now)) {
        char buf[24]; snprintf(buf, sizeof(buf), "BUFFER %d/%d", nvsGetCount(), NVS_MAX_RECORDS);
        strcpy(message, buf); return true;
    }
    strcpy(message, "BUFFER PENUH!"); return false;
}

// ============================================================
// DISPLAY
// ============================================================
void showOLED(const __FlashStringHelper *l1, const char *l2) {
    if (!oledIsOn) return;
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
    int16_t x, y; uint16_t w, h;
    display.getTextBounds(l1, 0, 0, &x, &y, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 10); display.println(l1);
    display.getTextBounds(l2, 0, 0, &x, &y, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 30); display.println(l2);
    display.display();
}
void showOLED(const __FlashStringHelper *l1, const __FlashStringHelper *l2) {
    char buf[32]; strncpy_P(buf, (const char *)l2, 31); buf[31] = '\0';
    showOLED(l1, buf);
}
void showProgress(const __FlashStringHelper *msg, int durationMs) {
    if (!oledIsOn) return;
    const int step = 8, width = 80;
    int dpStep = durationMs / (width / step);
    int startX = (SCREEN_WIDTH - width) / 2;
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
    int16_t x, y; uint16_t w, h;
    display.getTextBounds(msg, 0, 0, &x, &y, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 20); display.println(msg); display.display();
    for (int i = 0; i <= width; i += step) {
        esp_task_wdt_reset(); display.fillRect(startX, 40, i, 4, WHITE); display.display(); delay(dpStep);
    }
    esp_task_wdt_reset(); delay(300);
}
void showStartupAnimation() {
    static const char title[]    = "ZEDLABS";
    static const char sub1[]     = "INNOVATE BEYOND";
    static const char sub2[]     = "LIMITS";
    static const char version[]  = "v2.3.0";
    const int tX   = (SCREEN_WIDTH - (7 * 12)) / 2;
    const int s1X  = (SCREEN_WIDTH - 15 * 6) / 2;
    const int s2X  = (SCREEN_WIDTH - 6 * 6) / 2;
    const int verX = (SCREEN_WIDTH - 7 * 6) / 2;
    display.clearDisplay(); display.setTextColor(WHITE);
    for (int x = -80; x <= tX; x += 4) {
        esp_task_wdt_reset();
        display.clearDisplay(); display.setTextSize(2); display.setCursor(x, 5); display.println(title);
        display.setTextSize(1); display.setCursor(s1X, 30); display.println(sub1);
        display.setCursor(s2X, 40); display.println(sub2); display.display(); delay(30);
    }
    esp_task_wdt_reset(); delay(300);
    display.setTextSize(1); display.setCursor(verX, 55); display.print(version); display.display();
    for (int i = 0; i < 3; i++) { delay(300); display.print('.'); display.display(); }
    esp_task_wdt_reset(); delay(500);
}
bool displayStateChanged() {
    return currentDisplay.isOnline       != previousDisplay.isOnline ||
           currentDisplay.pendingRecords != previousDisplay.pendingRecords ||
           currentDisplay.wifiSignal     != previousDisplay.wifiSignal ||
           strncmp(currentDisplay.time, previousDisplay.time, 6) != 0;
}
void updateCurrentDisplayState() {
    currentDisplay.isOnline = isWifiConnected();
    struct tm t;
    if (getTimeWithFallback(&t))
        snprintf(currentDisplay.time, sizeof(currentDisplay.time), "%02d:%02d", t.tm_hour, t.tm_min);
    if (pendingCacheDirty) refreshPendingCache();
    currentDisplay.pendingRecords = cachedPendingRecords + nvsGetCount();
    if (isWifiConnected()) {
        long rssi = WiFi.RSSI();
        currentDisplay.wifiSignal = (rssi > -67) ? 4 : (rssi > -70) ? 3 : (rssi > -80) ? 2 : (rssi > -90) ? 1 : 0;
    } else {
        currentDisplay.wifiSignal = 0;
    }
}
void updateStandbySignal() {
    if (!oledIsOn || !displayStateChanged()) return;
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
    display.setCursor(2, 2);
    if (reconnectState == RECONNECT_INIT || reconnectState == RECONNECT_TRYING) {
        display.print(F("CONNECTING"));
        for (int i = 0; i < (int)((millis() / 500) % 4); i++) display.print('.');
    } else if (syncState.inProgress) {
        display.print(syncState.aggressiveMode ? F("SYNC PENUH") : F("SYNCING"));
        for (int i = 0; i < (int)((millis() / 500) % 4); i++) display.print('.');
    } else {
        display.print(currentDisplay.isOnline ? F("ONLINE") : F("OFFLINE"));
    }
    const char *tapText = "TAP KARTU";
    int16_t x1, y1; uint16_t w1, h1;
    display.getTextBounds(tapText, 0, 0, &x1, &y1, &w1, &h1);
    display.setCursor((SCREEN_WIDTH - w1) / 2, 20); display.print(tapText);
    display.getTextBounds(currentDisplay.time, 0, 0, &x1, &y1, &w1, &h1);
    display.setCursor((SCREEN_WIDTH - w1) / 2, 35); display.print(currentDisplay.time);
    if (currentDisplay.pendingRecords > 0) {
        char buf[16]; snprintf(buf, sizeof(buf), "Q:%d", currentDisplay.pendingRecords);
        display.getTextBounds(buf, 0, 0, &x1, &y1, &w1, &h1);
        display.setCursor((SCREEN_WIDTH - w1) / 2, 50); display.print(buf);
    }
    if (currentDisplay.wifiSignal > 0) {
        for (int i = 0; i < 4; i++) {
            int bh = 2 + i * 2, bx = SCREEN_WIDTH - 18 + i * 5;
            if (i < currentDisplay.wifiSignal) display.fillRect(bx, 10 - bh, 3, bh, WHITE);
            else                               display.drawRect(bx, 10 - bh, 3, bh, WHITE);
        }
    }
    display.display(); memcpy(&previousDisplay, &currentDisplay, sizeof(DisplayState));
}

// ============================================================
// BUZZER
// ============================================================
void playToneSuccess() { for (int i = 0; i < 2; i++) { tone(PIN_BUZZER, 3000, 100); delay(150); } noTone(PIN_BUZZER); }
void playToneError()   { for (int i = 0; i < 3; i++) { tone(PIN_BUZZER, 3000, 150); delay(200); } noTone(PIN_BUZZER); }
void playToneNotify()  { tone(PIN_BUZZER, 3000, 100); delay(120); noTone(PIN_BUZZER); }
void playStartupMelody() {
    static const int m[] = {2500, 3000, 2500, 3000};
    for (int i = 0; i < 4; i++) { tone(PIN_BUZZER, m[i], 100); delay(150); }
    noTone(PIN_BUZZER);
}

// ============================================================
// RFID SCAN HANDLER
// ============================================================
void handleRFIDScan() {
    deselectSD(); digitalWrite(PIN_RFID_SS, LOW);
    char rfidBuf[11]; uidToString(rfidReader.uid.uidByte, rfidReader.uid.size, rfidBuf);
    if (strcmp(rfidBuf, lastUID) == 0 && millis() - timers.lastScan < DEBOUNCE_TIME) {
        rfidReader.PICC_HaltA(); rfidReader.PCD_StopCrypto1(); digitalWrite(PIN_RFID_SS, HIGH); return;
    }
    strcpy(lastUID, rfidBuf); timers.lastScan = millis();
    bool wasOff = !oledIsOn; if (wasOff) turnOnOLED();
    showOLED(F("RFID"), rfidBuf); playToneNotify();
    char message[32]; bool success = kirimPresensi(rfidBuf, message);
    showOLED(success ? F("BERHASIL") : F("INFO"), message);
    success ? playToneSuccess() : playToneError();
    rfidFeedback.active = true; rfidFeedback.shownAt = millis(); rfidFeedback.wasOledOff = wasOff;
    rfidReader.PICC_HaltA(); rfidReader.PCD_StopCrypto1(); digitalWrite(PIN_RFID_SS, HIGH);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    const esp_task_wdt_config_t wdt = { .timeout_ms = WDT_TIMEOUT_SEC * 1000, .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_init(&wdt); esp_task_wdt_add(nullptr);
    esp_ota_mark_app_valid_cancel_rollback();

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    pinMode(PIN_BUZZER, OUTPUT);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    showStartupAnimation(); playStartupMelody();
    esp_task_wdt_reset();

    // Muat timezone yang tersimpan sebelum apapun
    savedTzOffset = nvsLoadTzOffset();
    if (savedTzOffset == 0) savedTzOffset = GMT_OFFSET_SEC;
    applyTimezone(savedTzOffset);

    uint8_t mac[6]; WiFi.macAddress(mac);
    snprintf(deviceId, sizeof(deviceId), "ESP32_%02X%02X", mac[4], mac[5]);
    for (int i = 0; deviceId[i]; i++) deviceId[i] = toupper(deviceId[i]);

    // Perbaiki lastValidTime setelah deep sleep
    if (timeWasSynced && lastValidTime > 0 && sleepDurationSeconds > 0) {
        lastValidTime += (time_t)sleepDurationSeconds;
        // Set waktu sistem langsung supaya jam tidak blank setelah wake
        struct timeval tv = { .tv_sec = lastValidTime, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        nvsSaveLastTime(lastValidTime);
        bootTime = millis(); bootTimeSet = true;
        sleepDurationSeconds = 0;
    }
    if (!timeWasSynced || lastValidTime == 0) {
        time_t saved = nvsLoadLastTime();
        if (saved > 0) {
            lastValidTime = saved;
            // Set waktu sistem dari NVS supaya jam langsung tampil
            struct timeval tv = { .tv_sec = lastValidTime, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            timeWasSynced = true; bootTime = millis(); bootTimeSet = true;
        }
    }

    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

    showProgress(F("INIT SD CARD"), 1500);
    sdCardAvailable = initSDCard();
    if (sdCardAvailable) {
        showOLED(F("SD CARD"), "TERSEDIA"); playToneSuccess(); delay(800);
        refreshPendingCache();
        if (cachedPendingRecords > 0) {
            char buf[20]; snprintf(buf, sizeof(buf), "%d TERSISA", cachedPendingRecords);
            showOLED(F("DATA OFFLINE"), buf); delay(1000);
        }
        showProgress(F("LOAD RFID DB"), 500);
        if (loadRfidCacheFromFile()) {
            char buf[20]; snprintf(buf, sizeof(buf), "%d RFID", rfidCacheCount);
            showOLED(F("RFID DB"), buf); delay(600);
        } else {
            showOLED(F("RFID DB"), "BELUM ADA"); delay(600);
        }
    } else {
        showOLED(F("SD CARD"), "TIDAK ADA"); playToneError(); delay(1000);
        int nc = nvsGetCount();
        if (nc > 0) { char buf[20]; snprintf(buf, sizeof(buf), "%d TERSISA", nc); showOLED(F("NVS BUFFER"), buf); delay(1000); }
    }

    showProgress(F("CONNECTING WIFI"), 1500);
    bool wifiOk = connectToWiFi();
    esp_task_wdt_reset();

    if (!wifiOk) {
        offlineBootFallback();
    } else {
        showProgress(F("PING API"), 500);
        int apiRetry = 0;
        while (!pingAPI() && apiRetry < 3) {
            apiRetry++;
            char buf[12]; snprintf(buf, sizeof(buf), "Retry %d/3", apiRetry);
            showOLED(F("API GAGAL"), buf); playToneError(); delay(800); esp_task_wdt_reset();
        }
        if (isOnline && !isSignalCritical()) {
            showOLED(F("API OK"), "SYNCING TIME"); playToneSuccess(); delay(500);

            // Sync waktu: setelah ini jam sistem akurat
            bool timeSynced = syncTimeWithFallback();
            if (timeSynced) {
                // Simpan timezone ke NVS supaya boot offline berikutnya tetap benar
                nvsSaveTzOffset(savedTzOffset);
            }
            esp_task_wdt_reset();

            int nc = nvsGetCount();
            if (nc > 0) {
                char buf[20]; snprintf(buf, sizeof(buf), "%d NVS RECORDS", nc);
                showOLED(F("SYNC NVS"), buf); delay(800);
                nvsSyncToServer(); esp_task_wdt_reset();
            }
            if (sdCardAvailable && isWifiConnected()) {
                refreshPendingCache();
                if (cachedPendingRecords > 0) {
                    char buf[20]; snprintf(buf, sizeof(buf), "%d records", cachedPendingRecords);
                    showOLED(F("SYNC DATA"), buf); delay(1000);
                    chunkedSync(); esp_task_wdt_reset();
                }
                showProgress(F("SYNC RFID DB"), 500);
                {
                    acquireSD(); selectSD();
                    bool dbExists = sd.exists(RFID_DB_FILE);
                    deselectSD(); releaseSD();

                    if (!dbExists) {
                        showOLED(F("RFID DB"), "BELUM ADA, UNDUH");
                        delay(600);
                        downloadRfidDb();
                        timers.lastRfidDbCheck = millis();
                    } else {
                        unsigned long serverVer = checkRfidDbVersion();
                        if (serverVer > nvsGetRfidDbVer()) {
                            downloadRfidDb();
                        } else {
                            char buf[20];
                            snprintf(buf, sizeof(buf), "%d RFID", rfidCacheCount);
                            showOLED(F("RFID DB"), rfidCacheCount > 0 ? buf : "UP TO DATE");
                        }
                        timers.lastRfidDbCheck = millis();
                    }
                }
                delay(600); esp_task_wdt_reset();
            }
        } else {
            showOLED(F("API GAGAL"), "OFFLINE MODE"); playToneError(); delay(1500);
        }
    }

    showProgress(F("INIT RFID"), 1000);
    rfidReader.PCD_Init(); delay(100);
    digitalWrite(PIN_RFID_SS, HIGH);
    byte ver = rfidReader.PCD_ReadRegister(rfidReader.VersionReg);
    if (ver == 0x00 || ver == 0xFF) { showOLED(F("RC522 GAGAL"), "RESTART..."); playToneError(); delay(3000); ESP.restart(); }

    showOLED(F("SISTEM SIAP"), isOnline ? "ONLINE" : "OFFLINE"); playToneSuccess();
    if (!bootTimeSet) { bootTime = millis(); bootTimeSet = true; }

    unsigned long now = millis();
    timers.lastSync              = now;
    timers.lastTimeSync          = now;
    timers.lastReconnect         = now;
    timers.lastDisplayUpdate     = now;
    timers.lastPeriodicCheck     = now;
    timers.lastOLEDScheduleCheck = now;
    timers.lastSDRedetect        = now;
    timers.lastNvsSync           = now;
    timers.lastOtaCheck          = 0;
    timers.lastRfidDbCheck       = now;

    delay(1000); checkOLEDSchedule();
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
    esp_task_wdt_reset();
    unsigned long now = millis();

    if (rfidFeedback.active && now - rfidFeedback.shownAt >= RFID_FEEDBACK_DISPLAY_MS) {
        rfidFeedback.active = false;
        if (rfidFeedback.wasOledOff) checkOLEDSchedule();
        memset(&previousDisplay, 0xFF, sizeof(previousDisplay));
    }

    checkOLEDSchedule();
    checkSDHealth();

    if (rfidReader.PICC_IsNewCardPresent() && rfidReader.PICC_ReadCardSerial()) {
        handleRFIDScan(); return;
    }

    processReconnect();

    if (now - timers.lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        timers.lastDisplayUpdate = now;
        updateCurrentDisplayState(); updateStandbySignal();
    }

    if (now - timers.lastPeriodicCheck >= PERIODIC_CHECK_INTERVAL) {
        timers.lastPeriodicCheck = now;

        if (isWifiConnected()) {
            if (!isSignalWeak()) {
                checkOtaUpdate(); checkAndUpdateRfidDb();
                if (otaState.updateAvailable && !rfidFeedback.active) performOtaUpdate();
            }

            if (nvsGetCount() > 0 && now - timers.lastNvsSync >= SYNC_INTERVAL) {
                timers.lastNvsSync = now; nvsSyncToServer();
            }

            if (sdCardAvailable) {
                if (syncState.inProgress) {
                    // Sync sedang berjalan (mode normal atau agresif), lanjutkan
                    chunkedSync();
                } else if (now - timers.lastSync >= SYNC_INTERVAL) {
                    refreshPendingCache();
                    timers.lastSync = now;
                    if (cachedPendingRecords > 0) {
                        syncState.aggressiveMode = (cachedPendingRecords >= SYNC_AGGRESSIVE_THRESHOLD);
                        chunkedSync();
                    }
                }
            }
        }

        periodicTimeSync();
    }

    // Deep sleep mode
    struct tm t;
    if (getTimeWithFallback(&t)) {
        int h = t.tm_hour;
        if (h >= SLEEP_START_HOUR || h < SLEEP_END_HOUR) {
            if (syncState.inProgress) { chunkedSync(); return; }
            showOLED(F("SLEEP MODE"), "..."); delay(1000);
            int sleepSec;
            if (h >= SLEEP_START_HOUR)
                sleepSec = ((24 - h + SLEEP_END_HOUR) * 3600) - (t.tm_min * 60 + t.tm_sec);
            else
                sleepSec = ((SLEEP_END_HOUR - h) * 3600) - (t.tm_min * 60 + t.tm_sec);
            if (sleepSec < 60)    sleepSec = 60;
            if (sleepSec > 43200) sleepSec = 43200;
            char buf[24];
            snprintf(buf, sizeof(buf), "%d Jam %d Menit", sleepSec / 3600, (sleepSec % 3600) / 60);
            showOLED(F("SLEEP FOR"), buf); delay(2000);
            display.clearDisplay(); display.display(); display.ssd1306_command(SSD1306_DISPLAYOFF);
            sleepDurationSeconds = (uint64_t)sleepSec;
            restoreWdtNormal(); esp_task_wdt_deinit();
            esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
            esp_deep_sleep_start();
        }
    }

    yield();
}
