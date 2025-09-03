#ifndef CONFIG_H
#define CONFIG_H

// Pin Configuration
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

// Configuration Constants
const char *WIFI_SSIDS[] = {"ZEDLABS", "ZEDLABS"};
const char *WIFI_PASSWORDS[] = {"18012000", "18012000"};
const int WIFI_COUNT = sizeof(WIFI_SSIDS) / sizeof(WIFI_SSIDS[0]);
const String API_BASE_URL = "http://192.168.1.25:8000/api";
const String API_SECRET = "P@ndegl@ng_14012000*";
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

// System Constants
const int MAX_RETRY_ATTEMPTS = 3;
const int WIFI_TIMEOUT = 20000;
const int HTTP_TIMEOUT = 10000;
const int MAX_OFFLINE_RECORDS = 100;
const int WATCHDOG_TIMEOUT = 30; // seconds
const int SLEEP_HOUR_START = 18;
const int SLEEP_HOUR_END = 3;

// Global Variables
bool sdSiap = false;
String lastUid = "";
unsigned long lastScanTime = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastSyncAttempt = 0;
unsigned long systemUptime = 0;
int wifiReconnectCount = 0;
int apiErrorCount = 0;
bool isDeepSleepMode = false;

// Struktur untuk menyimpan tap history
struct TapHistory
{
    String rfid;
    unsigned long tapTime;
    String timestamp;
};

// Array untuk menyimpan history tap terakhir (maksimal 50 entries)
const int MAX_TAP_HISTORY = 50;
TapHistory tapHistoryBuffer[MAX_TAP_HISTORY];
int tapHistoryIndex = 0;
int tapHistoryCount = 0;

// Konstanta untuk minimum interval tap (30 menit = 1800000 ms)
const unsigned long MIN_TAP_INTERVAL = 30 * 60 * 1000; // 30 menit dalam milliseconds

// System Status
enum SystemStatus
{
    STATUS_INIT,
    STATUS_READY,
    STATUS_PROCESSING,
    STATUS_ERROR,
    STATUS_OFFLINE,
    STATUS_MAINTENANCE
};

#endif