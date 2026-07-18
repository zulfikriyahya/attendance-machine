/*
 * ATTENDANCE MACHINE
 * Device           : ESP32-C3 Super Mini
 * Tools            : Arduino IDE 2.3.6
 * Environtment     : v3.3.10
 * Schema Partition : Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)
 * Author           : Yahya Zulfikri
 * Created          : Juli 2025
 * Updated          : Mei 2026
 * Version          : 2.3.1
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
#include <esp_mac.h>
#include <esp_efuse_table.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#define PIN_SPI_SCK 4
#define PIN_SPI_MOSI 6
#define PIN_SPI_MISO 5
#define PIN_RFID_SS 7
#define PIN_RFID_RST 3
#define PIN_SD_CS 1
#define PIN_OLED_SDA 8
#define PIN_OLED_SCL 9
#define PIN_BUZZER 10
// #define PIN_BOOT 9
#define PIN_BOOT 0 // GAP-01: dipindah dari GPIO9 (bentrok dengan PIN_OLED_SCL). \
                   // WAJIB: pasang tombol tekan eksternal GPIO0 <-> GND. \
                   // Tombol silkscreen "BOOT" bawaan board TIDAK lagi berfungsi untuk factory reset.
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DEBOUNCE_TIME 150UL
#define SYNC_INTERVAL 300000UL
#define MAX_OFFLINE_AGE 31536000UL
#define MIN_REPEAT_INTERVAL 1800UL
#define TIME_SYNC_INTERVAL 1800000UL
#define RECONNECT_INTERVAL 60000UL
#define RECONNECT_TIMEOUT 20000UL
#define DISPLAY_UPDATE_INTERVAL 1000UL
#define PERIODIC_CHECK_INTERVAL 1000UL
#define OLED_SCHEDULE_CHECK_INTERVAL 60000UL
#define RFID_FEEDBACK_DISPLAY_MS 1800UL
#define SD_REDETECT_INTERVAL 30000UL
#define MAX_TIME_ESTIMATE_AGE 43200UL
#define OTA_CHECK_INTERVAL 30000UL
#define RFID_DB_CHECK_INTERVAL 30000UL
#define TELEMETRY_INTERVAL 300000UL
#define REMOTE_CONFIG_INTERVAL 600000UL
#define FACTORY_RESET_HOLD_MS 5000UL
#define PROVISIONING_TIMEOUT_MS 300000UL
#define WDT_TIMEOUT_SEC 60
#define WDT_SYNC_TIMEOUT_MS 180000UL
#define WDT_NORMAL_TIMEOUT_MS 90000UL
#define SD_MUTEX_TIMEOUT_MS 5000UL
#define MAX_RECORDS_PER_FILE 25
#define MAX_QUEUE_FILES 60000
#define MAX_DUPLICATE_CHECK_FILES 3
#define MAX_DUPLICATE_CHECK_LINES (MAX_RECORDS_PER_FILE + 1)
#define QUEUE_WARN_THRESHOLD 48000
#define METADATA_FILE "/queue_meta.txt"
#define MAX_SYNC_FILES_PER_CYCLE 5
#define MAX_SYNC_RETRIES 2
#define QUEUE_SLOT_SEARCH_LIMIT 50
// TODO: GAP-09 ASUMSI - jumlah slot yang dicoba sebelum menyatakan queue penuh.
// SRS/spesifikasi asli tidak menentukan angka pasti; 50 dipilih sebagai batas wajar
// agar tidak memindai keseluruhan MAX_QUEUE_FILES (60000) setiap kali penuh.
#define SYNC_RETRY_DELAY_MS 2000UL
#define FAILED_LOG_MAX_LINES 500
#define NVS_MAX_RECORDS 40
#define NVS_NAMESPACE "presensi"
#define NVS_KEY_COUNT "nvs_count"
#define NVS_KEY_PREFIX "rec_"
#define NVS_KEY_LAST_TIME "last_time"
#define NVS_KEY_RFID_VER "rfid_db_ver"
#define NVS_KEY_SCAN_DATE "scan_date"
#define NVS_KEY_SCAN_COUNT "scan_count"
#define NVS_NS_CONFIG "cfg"
#define NVS_KEY_SSID1 "ssid1"
#define NVS_KEY_PASS1 "pass1"
#define NVS_KEY_SSID2 "ssid2"
#define NVS_KEY_PASS2 "pass2"
#define NVS_KEY_SSID3 "ssid3"
#define NVS_KEY_PASS3 "pass3"
#define NVS_KEY_APIKEY "apikey"
#define NVS_KEY_DEVNAME "devname"
#define NVS_KEY_APIURL "apiurl"
#define NVS_KEY_CFG_SLP_S "slp_s"
#define NVS_KEY_CFG_SLP_E "slp_e"
#define NVS_KEY_CFG_DIM_S "dim_s"
#define NVS_KEY_CFG_DIM_E "dim_e"
#define NVS_KEY_CFG_SYNCIV "sync_iv"
#define NVS_KEY_CFG_OTAIV "ota_iv"
#define NVS_KEY_PROVISIONED "prov"
#define NVS_KEY_LAST_RFID "last_rfid"
#define NVS_KEY_LAST_SCAN_T "last_scan_t"
#define RFID_DB_FILE "/rfid_db.txt"
#define RFID_CACHE_MAX 5000
#define ADMIN_RFID_FILE "/admin_rfid.txt"
#define SLEEP_START_HOUR_DEFAULT 18
#define SLEEP_END_HOUR_DEFAULT 5
#define OLED_DIM_START_HOUR_DEFAULT 8
#define OLED_DIM_END_HOUR_DEFAULT 12
#define GMT_OFFSET_SEC 25200L
#define SIGNAL_THRESHOLD_WEAK -85
#define SIGNAL_THRESHOLD_CRITICAL -90
#define FIRMWARE_VERSION "2.3.1"
#define PROV_AP_SSID "ATTENDANCE MACHINE"
// #define PROV_AP_PASS "P@ssw0rd"
// GAP-02: PROV_AP_PASS statis dihapus, digantikan deriveProvisioningPassword()
// yang menghasilkan password unik per device dari MAC address.
#define PROV_DNS_PORT 53
#define CRC8_POLY 0x07
#define TASK_RFID_STACK 8192
#define TASK_SYNC_STACK 8192
#define TASK_DISPLAY_STACK 12288
#define TASK_RFID_PRIORITY 3
#define TASK_SYNC_PRIORITY 2
#define TASK_DISPLAY_PRIORITY 1
#define RFID_QUEUE_LEN 8
#define DEEP_SLEEP_TASK_WAIT_MS 5000UL
#define DEVICE_NAME_MAX_LEN 31

static const char NTP_SERVER_1[] PROGMEM = "pool.ntp.org";
static const char NTP_SERVER_2[] PROGMEM = "time.google.com";
static const char NTP_SERVER_3[] PROGMEM = "id.pool.ntp.org";

RTC_DATA_ATTR time_t lastValidTime = 0;
RTC_DATA_ATTR bool timeWasSynced = false;
RTC_DATA_ATTR unsigned long bootTime = 0;
RTC_DATA_ATTR bool bootTimeSet = false;
RTC_DATA_ATTR int currentQueueFile = 0;
RTC_DATA_ATTR bool rtcQueueFileValid = false;
RTC_DATA_ATTR uint64_t sleepDurationSeconds = 0;

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

struct Timers
{
  unsigned long lastScan, lastSync, lastTimeSync, lastReconnect;
  unsigned long lastDisplayUpdate, lastPeriodicCheck, lastOLEDScheduleCheck;
  unsigned long lastSDRedetect, lastNvsSync, lastOtaCheck, lastRfidDbCheck;
  unsigned long lastTelemetry, lastRemoteConfig;
  // GAP-13: field lastFactoryCheck dihapus karena tidak pernah dibaca di manapun.
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
  char md5[36];
};
struct RfidScanEvent
{
  uint8_t uid[10];
  uint8_t uidLen;
};
struct RuntimeConfig
{
  int sleepStartHour;
  int sleepEndHour;
  int dimStartHour;
  int dimEndHour;
  unsigned long syncIntervalMs;
  unsigned long otaCheckIntervalMs;
};
struct EncryptedCredential
{
  uint8_t iv[16];
  uint8_t data[48];
  uint8_t len;
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 rfidReader(PIN_RFID_SS, PIN_RFID_RST);
SdFat sd;
FsFile file;
Preferences prefs;
WebServer provServer(80);
DNSServer dnsServer;

Timers timers = {};
DisplayState currentDisplay = {false, "00:00", 0, 0};
DisplayState previousDisplay = {false, "--:--", -1, -1};
SyncState syncState = {0, false, 0, 0, 0};
RfidFeedback rfidFeedback = {false, 0, false};
OtaState otaState = {false, "", "", ""};
RuntimeConfig rtCfg = {
    SLEEP_START_HOUR_DEFAULT, SLEEP_END_HOUR_DEFAULT,
    OLED_DIM_START_HOUR_DEFAULT, OLED_DIM_END_HOUR_DEFAULT,
    SYNC_INTERVAL, OTA_CHECK_INTERVAL};

char lastUID[11] = "";
char deviceId[20] = "";
char deviceName[DEVICE_NAME_MAX_LEN + 1] = "";
char provApPassword[16] = ""; // GAP-02
bool isOnline = false;
bool sdCardAvailable = false;
bool oledIsOn = true;
bool isProvisioned = false;
volatile bool wdtExtended = false;
portMUX_TYPE wdtMux = portMUX_INITIALIZER_UNLOCKED;
int cachedPendingRecords = 0;
bool pendingCacheDirty = true;
int cachedQueueFileCount = 0;

ReconnectState reconnectState = RECONNECT_IDLE;
unsigned long reconnectStartTime = 0;
int currentSsidIdx = 0;

char rfidCacheFlat[RFID_CACHE_MAX][11];
int rfidCacheCount = 0;
bool rfidCacheLoaded = false;
bool rfidDbValid = false;
char adminRfidList[5][11];
int adminRfidCount = 0;

TaskHandle_t hTaskRfid = nullptr;
TaskHandle_t hTaskSync = nullptr;
TaskHandle_t hTaskDisplay = nullptr;
TaskHandle_t hTaskLoop = nullptr;
SemaphoreHandle_t xSdMutex = nullptr;
SemaphoreHandle_t xConfigMutex = nullptr; // GAP-12: melindungi rtCfg
SemaphoreHandle_t xDisplayMutex = nullptr;
QueueHandle_t xRfidQueue = nullptr;
volatile bool sleepRequested = false;

static uint8_t crc8(const uint8_t *data, size_t len)
{
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? ((crc << 1) ^ CRC8_POLY) : (crc << 1);
  }
  return crc;
}

static uint8_t recordCrc8(const char *rfid, unsigned long t)
{
  uint8_t buf[14];
  memcpy(buf, rfid, 10);
  buf[10] = (t >> 24) & 0xFF;
  buf[11] = (t >> 16) & 0xFF;
  buf[12] = (t >> 8) & 0xFF;
  buf[13] = (t) & 0xFF;
  return crc8(buf, 14);
}

static void deriveAesKey(uint8_t key[16])
{
  Serial.println("[AES] Deriving AES key from eFuse MAC...");
  // GAP-04/GAP-05: keputusan sadar - skema penurunan kunci dari MAC+salt tetap dan
  // AES-CBC tanpa HMAC/integritas TIDAK diubah pada iterasi ini, karena sudah ada
  // device terpasang di lapangan yang kredensial NVS-nya akan tidak terbaca (corrupt)
  // jika skema kunci berubah. Perubahan skema memerlukan mekanisme migrasi terpisah
  // dan re-provisioning massal - di luar cakupan iterasi ini.
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  uint8_t seed[22];
  memcpy(seed, mac, 6);
  const char *salt = "ZEDLABS_PRESENSI";
  memcpy(seed + 6, salt, 16);
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, seed, 22);
  uint8_t hash[32];
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);
  memcpy(key, hash, 16);
  Serial.println("[AES] Key derivation done.");
}

static bool encryptString(const char *plain, EncryptedCredential &out)
{
  Serial.printf("[ENC] Encrypting string (len=%d)...\n", strlen(plain));
  uint8_t key[16];
  deriveAesKey(key);
  size_t plen = strlen(plain);
  if (plen > 47)
  {
    Serial.println("[ENC] ERROR: string too long (>47)");
    return false;
  }
  uint8_t buf[48] = {};
  memcpy(buf, plain, plen);
  out.len = (uint8_t)plen;
  esp_fill_random(out.iv, 16);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);
  uint8_t iv[16];
  memcpy(iv, out.iv, 16);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 48, iv, buf, out.data);
  mbedtls_aes_free(&aes);
  Serial.println("[ENC] Encrypt OK.");
  return true;
}

static bool decryptString(const EncryptedCredential &in, char *plain, size_t maxLen)
{
  Serial.println("[DEC] Decrypting credential...");
  uint8_t key[16];
  deriveAesKey(key);
  uint8_t buf[48];
  uint8_t iv[16];
  memcpy(iv, in.iv, 16);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, 128);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, 48, iv, in.data, buf);
  mbedtls_aes_free(&aes);
  size_t copyLen = (in.len < maxLen - 1) ? in.len : maxLen - 1;
  memcpy(plain, buf, copyLen);
  plain[copyLen] = '\0';
  Serial.println("[DEC] Decrypt OK.");
  return true;
}

static void saveEncryptedNvs(const char *ns, const char *key, const char *plain)
{
  Serial.printf("[NVS] saveEncrypted ns=%s key=%s\n", ns, key);
  EncryptedCredential ec;
  if (!encryptString(plain, ec))
  {
    Serial.println("[NVS] saveEncrypted FAILED (encrypt error)");
    return;
  }
  prefs.begin(ns, false);
  prefs.putBytes(key, &ec, sizeof(EncryptedCredential));
  prefs.end();
  Serial.println("[NVS] saveEncrypted done.");
}

static bool loadEncryptedNvs(const char *ns, const char *key, char *plain, size_t maxLen)
{
  Serial.printf("[NVS] loadEncrypted ns=%s key=%s\n", ns, key);
  prefs.begin(ns, true);
  size_t len = prefs.getBytesLength(key);
  if (len != sizeof(EncryptedCredential))
  {
    prefs.end();
    Serial.printf("[NVS] loadEncrypted FAILED: len mismatch (%d vs %d)\n", len, sizeof(EncryptedCredential));
    return false;
  }
  EncryptedCredential ec;
  prefs.getBytes(key, &ec, sizeof(EncryptedCredential));
  prefs.end();
  bool ok = decryptString(ec, plain, maxLen);
  Serial.printf("[NVS] loadEncrypted result=%d\n", ok);
  return ok;
}

struct WifiCredential
{
  char ssid[32];
  char pass[64];
};

static WifiCredential wifiCreds[3];
// GAP-16: keputusan sadar - satu API key statis dipertahankan untuk seluruh device,
// sesuai kontrak backend saat ini yang belum mendukung validasi API key per-device.
static char apiKey[48] = "";

static char apiBaseUrl[80] = "https://presensi.zedlabs.id";

static void loadCredentials()
{
  Serial.println("[CFG] Loading credentials from NVS...");
  loadEncryptedNvs(NVS_NS_CONFIG, NVS_KEY_SSID1, wifiCreds[0].ssid, sizeof(wifiCreds[0].ssid));
  loadEncryptedNvs(NVS_NS_CONFIG, NVS_KEY_PASS1, wifiCreds[0].pass, sizeof(wifiCreds[0].pass));
  loadEncryptedNvs(NVS_NS_CONFIG, NVS_KEY_SSID2, wifiCreds[1].ssid, sizeof(wifiCreds[1].ssid));
  loadEncryptedNvs(NVS_NS_CONFIG, NVS_KEY_PASS2, wifiCreds[1].pass, sizeof(wifiCreds[1].pass));
  loadEncryptedNvs(NVS_NS_CONFIG, NVS_KEY_SSID3, wifiCreds[2].ssid, sizeof(wifiCreds[2].ssid));
  loadEncryptedNvs(NVS_NS_CONFIG, NVS_KEY_PASS3, wifiCreds[2].pass, sizeof(wifiCreds[2].pass));
  loadEncryptedNvs(NVS_NS_CONFIG, NVS_KEY_APIKEY, apiKey, sizeof(apiKey));
  loadEncryptedNvs(NVS_NS_CONFIG, NVS_KEY_DEVNAME, deviceName, sizeof(deviceName));

  char tmpUrl[80] = "";
  if (loadEncryptedNvs(NVS_NS_CONFIG, NVS_KEY_APIURL, tmpUrl, sizeof(tmpUrl)))
  {
    if (strlen(tmpUrl) > 0)
    {
      int ul = strlen(tmpUrl);
      while (ul > 0 && tmpUrl[ul - 1] == '/')
        tmpUrl[--ul] = '\0';
      strncpy(apiBaseUrl, tmpUrl, sizeof(apiBaseUrl) - 1);
      apiBaseUrl[sizeof(apiBaseUrl) - 1] = '\0';
    }
  }

  prefs.begin(NVS_NS_CONFIG, true);
  int slpS = prefs.getInt(NVS_KEY_CFG_SLP_S, SLEEP_START_HOUR_DEFAULT);
  int slpE = prefs.getInt(NVS_KEY_CFG_SLP_E, SLEEP_END_HOUR_DEFAULT);
  int dimS = prefs.getInt(NVS_KEY_CFG_DIM_S, OLED_DIM_START_HOUR_DEFAULT);
  int dimE = prefs.getInt(NVS_KEY_CFG_DIM_E, OLED_DIM_END_HOUR_DEFAULT);
  // GAP-18: baca sync_iv/ota_iv yang sebelumnya sudah punya key NVS tapi tidak pernah dibaca.
  unsigned long syncIv = prefs.getULong(NVS_KEY_CFG_SYNCIV, SYNC_INTERVAL);
  unsigned long otaIv = prefs.getULong(NVS_KEY_CFG_OTAIV, OTA_CHECK_INTERVAL);
  prefs.end();

  auto clampHour = [](int h, int def)
  {
    return (h >= 0 && h <= 23) ? h : def;
  };
  // GAP-18 ASUMSI: interval minimum 5000ms untuk mencegah nilai 0/absurd dari NVS
  // yang berpotensi menyebabkan polling terlalu agresif.
  auto clampInterval = [](unsigned long v, unsigned long def)
  {
    return (v >= 5000UL) ? v : def;
  };
  rtCfg.sleepStartHour = clampHour(slpS, SLEEP_START_HOUR_DEFAULT);
  rtCfg.sleepEndHour = clampHour(slpE, SLEEP_END_HOUR_DEFAULT);
  rtCfg.dimStartHour = clampHour(dimS, OLED_DIM_START_HOUR_DEFAULT);
  rtCfg.dimEndHour = clampHour(dimE, OLED_DIM_END_HOUR_DEFAULT);
  rtCfg.syncIntervalMs = clampInterval(syncIv, SYNC_INTERVAL);
  rtCfg.otaCheckIntervalMs = clampInterval(otaIv, OTA_CHECK_INTERVAL);

  Serial.printf("[CFG] apiBaseUrl: %s\n", apiBaseUrl);
  Serial.printf("[CFG] sleep: %d-%d, dim: %d-%d\n",
                rtCfg.sleepStartHour, rtCfg.sleepEndHour,
                rtCfg.dimStartHour, rtCfg.dimEndHour);
  Serial.printf("[CFG] sync_iv=%lu ota_iv=%lu\n", rtCfg.syncIntervalMs, rtCfg.otaCheckIntervalMs); // GAP-18
  Serial.println("[CFG] Credentials loaded.");
}

// GAP-12: snapshot rtCfg dengan proteksi mutex, mencegah race condition antara
// fetchRemoteConfig() (penulis) dengan taskSync/taskDisplay/loop() (pembaca).
RuntimeConfig getRuntimeConfigSnapshot()
{
  RuntimeConfig snap;
  if (xConfigMutex && xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(200)) == pdTRUE)
  {
    snap = rtCfg;
    xSemaphoreGive(xConfigMutex);
  }
  else
  {
    Serial.println("[CFG] PERINGATAN: gagal ambil xConfigMutex, snapshot rtCfg mungkin tidak konsisten.");
    snap = rtCfg;
  }
  return snap;
}

// GAP-18: persist rtCfg ke NVS agar bertahan melewati restart/deep sleep,
// memakai key NVS_KEY_CFG_SYNCIV/NVS_KEY_CFG_OTAIV yang sudah ada di #define
// namun sebelumnya tidak pernah dipakai untuk menulis.
static void persistRuntimeConfigToNvs(const RuntimeConfig &cfg)
{
  prefs.begin(NVS_NS_CONFIG, false);
  prefs.putInt(NVS_KEY_CFG_SLP_S, cfg.sleepStartHour);
  prefs.putInt(NVS_KEY_CFG_SLP_E, cfg.sleepEndHour);
  prefs.putInt(NVS_KEY_CFG_DIM_S, cfg.dimStartHour);
  prefs.putInt(NVS_KEY_CFG_DIM_E, cfg.dimEndHour);
  prefs.putULong(NVS_KEY_CFG_SYNCIV, cfg.syncIntervalMs);
  prefs.putULong(NVS_KEY_CFG_OTAIV, cfg.otaCheckIntervalMs);
  prefs.end();
  Serial.println("[CFG] rtCfg dipersist ke NVS.");
}

static void saveCredential(const char *key, const char *val)
{
  saveEncryptedNvs(NVS_NS_CONFIG, key, val);
}

static void markProvisioned()
{
  Serial.println("[PROV] Marking device as provisioned...");
  prefs.begin(NVS_NS_CONFIG, false);
  prefs.putBool(NVS_KEY_PROVISIONED, true);
  prefs.end();
  isProvisioned = true;
  Serial.println("[PROV] Device marked provisioned.");
}

static bool checkProvisioned()
{
  prefs.begin(NVS_NS_CONFIG, true);
  bool v = prefs.getBool(NVS_KEY_PROVISIONED, false);
  prefs.end();
  Serial.printf("[PROV] checkProvisioned=%d\n", v);
  return v;
}

static WiFiClientSecure _httpClient;

static WiFiClientSecure &getHttpClient()
{
  // GAP-03: keputusan sadar - verifikasi sertifikat TLS tetap dinonaktifkan.
  // Risiko diterima; tidak diubah pada iterasi ini per keputusan pemilik proyek.
  _httpClient.setInsecure();
  _httpClient.setHandshakeTimeout(10);
  return _httpClient;
}

inline bool acquireSD(TickType_t timeout = pdMS_TO_TICKS(SD_MUTEX_TIMEOUT_MS))
{
  return xSemaphoreTake(xSdMutex, timeout) == pdTRUE;
}

inline void releaseSD()
{
  xSemaphoreGive(xSdMutex);
}

inline void selectSD()
{
  digitalWrite(PIN_RFID_SS, HIGH);
  digitalWrite(PIN_SD_CS, LOW);
}

inline void deselectSD()
{
  digitalWrite(PIN_SD_CS, HIGH);
}

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

void extendWdtForSync()
{
  if (!hTaskRfid && !hTaskSync && !hTaskDisplay)
  {
    Serial.println("[WDT] Tasks not ready, skip extend.");
    return;
  }
  portENTER_CRITICAL(&wdtMux);
  if (wdtExtended)
  {
    portEXIT_CRITICAL(&wdtMux);
    return;
  }
  wdtExtended = true;
  portEXIT_CRITICAL(&wdtMux);

  if (hTaskLoop)
    esp_task_wdt_delete(hTaskLoop);
  if (hTaskRfid)
    esp_task_wdt_delete(hTaskRfid);
  if (hTaskSync)
    esp_task_wdt_delete(hTaskSync);
  if (hTaskDisplay)
    esp_task_wdt_delete(hTaskDisplay);

  TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCore(0);
  if (idle0)
    esp_task_wdt_delete(idle0);

  esp_task_wdt_deinit();

  const esp_task_wdt_config_t cfg = {
      .timeout_ms = (uint32_t)WDT_SYNC_TIMEOUT_MS,
      .idle_core_mask = 0,
      .trigger_panic = true};
  esp_task_wdt_init(&cfg);

  if (hTaskLoop)
    esp_task_wdt_add(hTaskLoop);
  if (hTaskRfid)
    esp_task_wdt_add(hTaskRfid);
  if (hTaskSync)
    esp_task_wdt_add(hTaskSync);
  if (hTaskDisplay)
    esp_task_wdt_add(hTaskDisplay);

  Serial.println("[WDT] Extended to 180s for sync/OTA.");
}

void restoreWdtNormal()
{
  if (!hTaskRfid && !hTaskSync && !hTaskDisplay)
  {
    Serial.println("[WDT] Tasks not ready, skip restore.");
    return;
  }
  portENTER_CRITICAL(&wdtMux);
  if (!wdtExtended)
  {
    portEXIT_CRITICAL(&wdtMux);
    return;
  }
  wdtExtended = false;
  portEXIT_CRITICAL(&wdtMux);

  if (hTaskLoop)
    esp_task_wdt_delete(hTaskLoop);
  if (hTaskRfid)
    esp_task_wdt_delete(hTaskRfid);
  if (hTaskSync)
    esp_task_wdt_delete(hTaskSync);
  if (hTaskDisplay)
    esp_task_wdt_delete(hTaskDisplay);

  TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCore(0);
  if (idle0)
    esp_task_wdt_delete(idle0);

  esp_task_wdt_deinit();

  const esp_task_wdt_config_t cfg = {
      .timeout_ms = WDT_NORMAL_TIMEOUT_MS,
      .idle_core_mask = 0,
      .trigger_panic = true};
  esp_task_wdt_init(&cfg);

  if (hTaskLoop)
    esp_task_wdt_add(hTaskLoop);
  if (hTaskRfid)
    esp_task_wdt_add(hTaskRfid);
  if (hTaskSync)
    esp_task_wdt_add(hTaskSync);
  if (hTaskDisplay)
    esp_task_wdt_add(hTaskDisplay);

  Serial.println("[WDT] Restored to 90s normal.");
}

void turnOffOLED()
{
  if (!oledIsOn)
    return;
  if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    oledIsOn = false;
    xSemaphoreGive(xDisplayMutex);
  }
}

void turnOnOLED()
{
  if (oledIsOn)
    return;
  if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    display.ssd1306_command(SSD1306_DISPLAYON);
    oledIsOn = true;
    memset(previousDisplay.time, 0xFF, sizeof(previousDisplay.time));
    previousDisplay.pendingRecords = -1;
    previousDisplay.wifiSignal = -1;
    previousDisplay.isOnline = !currentDisplay.isOnline;
    xSemaphoreGive(xDisplayMutex);
  }
}

void showOLED(const __FlashStringHelper *l1, const char *l2)
{
  if (!oledIsOn)
    return;
  if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(200)) != pdTRUE)
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
  xSemaphoreGive(xDisplayMutex);
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
  if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(200)) != pdTRUE)
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
  xSemaphoreGive(xDisplayMutex);
  for (int i = 0; i <= total; i += step)
  {
    esp_task_wdt_reset();
    if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
      display.fillRect(startX, 40, i, 4, WHITE);
      display.display();
      xSemaphoreGive(xDisplayMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(perStep));
  }
  esp_task_wdt_reset();
  vTaskDelay(pdMS_TO_TICKS(300));
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
  static const int mel[] = {2500, 3000, 2500, 3000};
  for (int i = 0; i < 4; i++)
  {
    tone(PIN_BUZZER, mel[i], 100);
    delay(150);
  }
  noTone(PIN_BUZZER);
}

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

void nvsBumpScanCount()
{
  prefs.begin(NVS_NAMESPACE, false);
  struct tm ti;
  time_t now = time(nullptr);
  localtime_r(&now, &ti);
  char today[9];
  snprintf(today, sizeof(today), "%04d%02d%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  char stored[9];
  strncpy(stored, prefs.getString(NVS_KEY_SCAN_DATE, "").c_str(), 8);
  stored[8] = '\0';
  int cnt = strcmp(stored, today) == 0 ? prefs.getInt(NVS_KEY_SCAN_COUNT, 0) : 0;
  prefs.putString(NVS_KEY_SCAN_DATE, today);
  prefs.putInt(NVS_KEY_SCAN_COUNT, cnt + 1);
  prefs.end();
}

int nvsGetScanCount()
{
  prefs.begin(NVS_NAMESPACE, true);
  int c = prefs.getInt(NVS_KEY_SCAN_COUNT, 0);
  prefs.end();
  return c;
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

void nvsSaveLastScan(const char *rfid, unsigned long t)
{
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(NVS_KEY_LAST_RFID, rfid);
  prefs.putULong(NVS_KEY_LAST_SCAN_T, t);
  prefs.end();
}

bool nvsIsRecentScan(const char *rfid, unsigned long t)
{
  prefs.begin(NVS_NAMESPACE, true);
  String storedRfid = prefs.getString(NVS_KEY_LAST_RFID, "");
  unsigned long storedT = prefs.getULong(NVS_KEY_LAST_SCAN_T, 0);
  prefs.end();
  if (storedRfid.length() == 0 || storedT == 0)
    return false;
  if (storedRfid != String(rfid))
    return false;
  return (t >= storedT && (t - storedT) < MIN_REPEAT_INTERVAL);
}

void clearRfidCache()
{
  memset(rfidCacheFlat, 0, sizeof(rfidCacheFlat));
  rfidCacheCount = 0;
  rfidCacheLoaded = false;
  rfidDbValid = false;
}

bool loadRfidCacheFromFileLocked()
{
  Serial.println("[RFID] Loading RFID cache from file...");
  clearRfidCache();
  if (!sd.exists(RFID_DB_FILE))
  {
    Serial.println("[RFID] rfid_db.txt not found.");
    return false;
  }
  FsFile f;
  if (!f.open(RFID_DB_FILE, O_RDONLY))
  {
    Serial.println("[RFID] Failed to open rfid_db.txt.");
    return false;
  }
  char line[12];
  int idx = 0;
  while (f.fgets(line, sizeof(line)) > 0 && idx < RFID_CACHE_MAX)
  {
    esp_task_wdt_reset();
    taskYIELD();
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
    memcpy(rfidCacheFlat[idx], line, 10);
    rfidCacheFlat[idx][10] = '\0';
    idx++;
  }
  // GAP-11: deteksi entri yang terpotong karena melebihi RFID_CACHE_MAX.
  if (idx >= RFID_CACHE_MAX)
  {
    long discarded = 0;
    while (f.fgets(line, sizeof(line)) > 0)
    {
      esp_task_wdt_reset();
      taskYIELD();
      int len = strlen(line);
      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
      if (len == 10)
        discarded++;
    }
    if (discarded > 0)
      Serial.printf("[RFID] PERINGATAN: %ld entri RFID terpotong (melebihi RFID_CACHE_MAX=%d).\n",
                    discarded, RFID_CACHE_MAX);
  }
  f.close();
  rfidCacheCount = idx;
  rfidCacheLoaded = (idx > 0);
  rfidDbValid = (idx > 0);
  Serial.printf("[RFID] Cache loaded: %d entries, valid=%d\n", rfidCacheCount, rfidDbValid);
  return rfidDbValid;
}

bool loadRfidCacheFromFile()
{
  if (!sdCardAvailable)
    return false;
  if (!acquireSD())
    return false;
  selectSD();
  bool ok = loadRfidCacheFromFileLocked();
  deselectSD();
  releaseSD();
  return ok;
}

bool isRfidInCache(const char *rfid)
{
  if (!rfidDbValid || !rfidCacheLoaded || rfidCacheCount == 0)
    return false;
  for (int i = 0; i < rfidCacheCount; i++)
    if (strcmp(rfidCacheFlat[i], rfid) == 0)
      return true;
  return false;
}

void loadAdminRfidList()
{
  Serial.println("[ADMIN] Loading admin RFID list...");
  adminRfidCount = 0;
  if (!sdCardAvailable)
  {
    Serial.println("[ADMIN] SD not available, skip.");
    return;
  }
  if (!acquireSD())
  {
    Serial.println("[ADMIN] SD mutex timeout.");
    return;
  }
  selectSD();
  if (!sd.exists(ADMIN_RFID_FILE))
  {
    Serial.println("[ADMIN] admin_rfid.txt not found.");
    deselectSD();
    releaseSD();
    return;
  }
  FsFile f;
  if (!f.open(ADMIN_RFID_FILE, O_RDONLY))
  {
    Serial.println("[ADMIN] Failed to open admin_rfid.txt.");
    deselectSD();
    releaseSD();
    return;
  }
  char line[12];
  while (f.fgets(line, sizeof(line)) > 0 && adminRfidCount < 5)
  {
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    if (len == 10)
    {
      memcpy(adminRfidList[adminRfidCount], line, 11);
      adminRfidCount++;
    }
  }
  f.close();
  deselectSD();
  releaseSD();
  Serial.printf("[ADMIN] Admin RFID loaded: %d entries\n", adminRfidCount);
}

bool isAdminRfid(const char *rfid)
{
  for (int i = 0; i < adminRfidCount; i++)
    if (strcmp(adminRfidList[i], rfid) == 0)
      return true;
  return false;
}

void handleAdminScan(const char *rfid)
{
  Serial.printf("[ADMIN] Admin scan: rfid=%s\n", rfid);
  (void)rfid;
  showOLED(F("ADMIN MODE"), "SYNC + STATUS");
  playToneNotify();
  char buf[24];
  snprintf(buf, sizeof(buf), "Q:%d SC:%d", cachedPendingRecords, nvsGetScanCount());
  showOLED(F("STATUS"), buf);
  delay(2000);
  if (isWifiConnected())
  {
    Serial.println("[ADMIN] Triggering manual sync...");
    pendingCacheDirty = true;
    syncState.inProgress = false;
    syncState.currentFile = 0;
  }
}

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
  Serial.println("[NTP] Syncing time...");
  if (isSignalCritical())
  {
    Serial.println("[NTP] Signal critical, abort.");
    return false;
  }
  const char *servers[] = {NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3};
  for (int i = 0; i < 3; i++)
  {
    char srv[32];
    strcpy_P(srv, servers[i]);
    Serial.printf("[NTP] Trying server: %s\n", srv);
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
        Serial.printf("[NTP] Sync OK: %s\n", buf);
        showOLED(F("WAKTU TERSYNC"), buf);
        delay(1000);
        return true;
      }
      delay(100);
    }
    Serial.printf("[NTP] Server %s timeout.\n", srv);
  }
  Serial.println("[NTP] All NTP servers failed.");
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

void checkOLEDSchedule()
{
  if (millis() - timers.lastOLEDScheduleCheck < OLED_SCHEDULE_CHECK_INTERVAL)
    return;
  timers.lastOLEDScheduleCheck = millis();
  struct tm ti;
  if (!getTimeWithFallback(&ti))
    return;
  RuntimeConfig cfg = getRuntimeConfigSnapshot(); // GAP-12
  int h = ti.tm_hour;
  if (h >= cfg.dimStartHour && h < cfg.dimEndHour)
    turnOffOLED();
  else
    turnOnOLED();
}

void appendFailedLogToSD(const char *rfid, const char *ts, const char *reason)
{
  if (!sdCardAvailable)
    return;
  if (!acquireSD(pdMS_TO_TICKS(2000)))
    return;
  selectSD();
  int lineCount = 0;
  if (sd.exists("/failed_log.csv"))
  {
    FsFile countFile;
    if (countFile.open("/failed_log.csv", O_RDONLY))
    {
      char ln[2];
      while (countFile.fgets(ln, sizeof(ln)) > 0)
        lineCount++;
      countFile.close();
    }
  }
  if (lineCount < FAILED_LOG_MAX_LINES)
  {
    FsFile logFile;
    if (logFile.open("/failed_log.csv", O_WRONLY | O_CREAT | O_APPEND))
    {
      if (logFile.size() == 0)
        logFile.println(F("rfid,timestamp,reason"));
      logFile.print(rfid);
      logFile.print(',');
      logFile.print(ts);
      logFile.print(',');
      logFile.println(reason);
      logFile.sync();
      logFile.close();
      Serial.printf("[LOG] Failed log written: rfid=%s reason=%s\n", rfid, reason);
    }
    else
    {
      Serial.println("[LOG] Failed to open failed_log.csv for append.");
    }
  }
  else
  {
    Serial.println("[LOG] failed_log.csv at max lines, skipping.");
  }
  deselectSD();
  releaseSD();
}

void getQueueFileName(int idx, char *buf, size_t sz)
{
  snprintf(buf, sz, "/queue_%d.csv", idx);
}

int countRecordsInFileLocked(const char *filename)
{
  if (!file.open(filename, O_RDONLY))
    return 0;
  int cnt = 0;
  char line[128];
  if (file.available())
    file.fgets(line, sizeof(line));
  while (file.fgets(line, sizeof(line)) > 0)
  {
    esp_task_wdt_reset();
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    if (len > 10)
      cnt++;
  }
  file.close();
  return cnt;
}

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

bool reinitSDCard()
{
  Serial.println("[SD] Re-initializing SD card...");
  if (file.isOpen())
    file.close();
  sd.end();
  delay(100);
  selectSD();
  delay(10);
  bool ok = sd.begin(PIN_SD_CS, SD_SCK_MHZ(10));
  deselectSD();
  Serial.printf("[SD] reinit result=%d\n", ok);
  return ok;
}

void checkSDHealth()
{
  if (millis() - timers.lastSDRedetect < SD_REDETECT_INTERVAL)
    return;
  timers.lastSDRedetect = millis();
  if (!sdCardAvailable)
  {
    Serial.println("[SD] SD not available, attempting re-detect...");
    if (!acquireSD(pdMS_TO_TICKS(1000)))
      return;
    bool ok = reinitSDCard();
    releaseSD();
    if (ok)
    {
      Serial.println("[SD] SD card re-detected OK.");
      sdCardAvailable = true;
      pendingCacheDirty = true;
      showOLED(F("SD CARD"), "TERBACA KEMBALI");
      playToneSuccess();
      delay(800);
      loadRfidCacheFromFile();
      loadAdminRfidList();
    }
    else
    {
      Serial.println("[SD] SD card still not available.");
    }
    return;
  }
  if (!acquireSD(pdMS_TO_TICKS(500)))
    return;
  selectSD();
  bool healthy = sd.vol()->fatType() > 0;
  deselectSD();
  releaseSD();
  if (!healthy)
  {
    Serial.println("[SD] SD health check FAILED — card removed?");
    sdCardAvailable = false;
    clearRfidCache();
    showOLED(F("SD CARD"), "TERLEPAS!");
    playToneError();
    delay(800);
  }
}

bool flushAllFiles()
{
  if (!sdCardAvailable)
    return true;
  if (!acquireSD(pdMS_TO_TICKS(3000)))
    return false;
  if (file.isOpen())
  {
    file.sync();
    file.close();
  }
  releaseSD();
  return true;
}

bool fileHasValidRecords(const char *fn)
{
  if (!file.open(fn, O_RDONLY))
    return false;
  time_t now = time(nullptr);
  char line[144];
  if (file.available())
    file.fgets(line, sizeof(line));
  bool found = false;
  while (file.available())
  {
    esp_task_wdt_reset();
    if (file.fgets(line, sizeof(line)) <= 0)
      break;
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    char *c1 = strchr(line, ',');
    char *c2 = c1 ? strchr(c1 + 1, ',') : nullptr;
    char *c3 = c2 ? strchr(c2 + 1, ',') : nullptr;
    if (!c3)
      continue;
    unsigned long recT = strtoul(c3 + 1, nullptr, 10);
    if (recT > 0 && (unsigned long)now - recT <= MAX_OFFLINE_AGE)
    {
      found = true;
      break;
    }
  }
  file.close();
  return found;
}

int countValidRecordsInFileLocked(const char *fn)
{
  if (!file.open(fn, O_RDONLY))
    return 0;
  time_t now = time(nullptr);
  int cnt = 0;
  char line[144];
  if (file.available())
    file.fgets(line, sizeof(line));
  while (file.available())
  {
    esp_task_wdt_reset();
    taskYIELD();
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
    if (!c3)
      continue;
    unsigned long recT = strtoul(c3 + 1, nullptr, 10);
    if (recT > 0 && (unsigned long)now - recT <= MAX_OFFLINE_AGE)
      cnt++;
  }
  file.close();
  return cnt;
}

int countAllOfflineRecords()
{
  if (!sdCardAvailable)
    return 0;
  int total = 0;
  cachedQueueFileCount = 0;
  char fn[20];
  if (!acquireSD())
    return 0;
  selectSD();
  int emptyStreak = 0;
  for (int i = 0; i < MAX_QUEUE_FILES; i++)
  {
    esp_task_wdt_reset();
    taskYIELD();
    getQueueFileName(i, fn, sizeof(fn));
    if (!sd.exists(fn))
    {
      emptyStreak++;
      if (emptyStreak >= 50)
      {
        Serial.println("[COUNT] 50 consecutive empty slots, stopping count.");
        break;
      }
      continue;
    }
    emptyStreak = 0;
    int validCnt = countValidRecordsInFileLocked(fn);
    if (validCnt == 0)
    {
      Serial.printf("[COUNT] File %s empty/expired, removing.\n", fn);
      sd.remove(fn);
      continue;
    }
    cachedQueueFileCount++;
    total += validCnt;
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
  if (cachedPendingRecords < 0)
    cachedPendingRecords = 0;
  pendingCacheDirty = false;
  if (!acquireSD(pdMS_TO_TICKS(1000)))
    return;
  saveMetadataLocked();
  releaseSD();
}

bool isDuplicateLocked(const char *rfid, unsigned long t)
{
  char fn[20];
  for (int offset = 0; offset < MAX_DUPLICATE_CHECK_FILES; offset++)
  {
    esp_task_wdt_reset();
    taskYIELD();
    int idx = (currentQueueFile - offset + MAX_QUEUE_FILES) % MAX_QUEUE_FILES;
    getQueueFileName(idx, fn, sizeof(fn));
    if (!sd.exists(fn))
      continue;
    if (!file.open(fn, O_RDONLY))
      continue;
    char line[128];
    if (file.available())
      file.fgets(line, sizeof(line));
    int read = 0;
    bool found = false;
    while (read < MAX_DUPLICATE_CHECK_LINES && file.fgets(line, sizeof(line)) > 0)
    {
      esp_task_wdt_reset();
      int len = strlen(line);
      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
      char tmp[128];
      strncpy(tmp, line, sizeof(tmp) - 1);
      tmp[sizeof(tmp) - 1] = '\0';
      char *c1 = strchr(tmp, ',');
      if (!c1)
      {
        read++;
        continue;
      }
      char *c2 = strchr(c1 + 1, ',');
      if (!c2)
      {
        read++;
        continue;
      }
      char *c3 = strchr(c2 + 1, ',');
      if (!c3)
      {
        read++;
        continue;
      }
      char *c4 = strchr(c3 + 1, ',');
      *c1 = '\0';
      unsigned long ft;
      if (c4)
      {
        *c4 = '\0';
        ft = strtoul(c3 + 1, nullptr, 10);
      }
      else
      {
        ft = strtoul(c3 + 1, nullptr, 10);
      }
      Serial.printf("dupCheck: t=%lu ft=%lu diff=%ld interval=%lu\n", t, ft, (long)(t - ft), MIN_REPEAT_INTERVAL);
      if (strcmp(tmp, rfid) == 0 && ft > 0 && t >= ft && (t - ft) < MIN_REPEAT_INTERVAL)
      {
        Serial.println("dupCheck: DUPLIKAT DITEMUKAN!");
        found = true;
        break;
      }
      read++;
    }
    file.close();
    if (found)
      return true;
  }
  return false;
}

bool initSDCard()
{
  Serial.println("[SD] Initializing SD card...");
  pinMode(PIN_SD_CS, OUTPUT);
  pinMode(PIN_RFID_SS, OUTPUT);
  deselectSD();
  digitalWrite(PIN_RFID_SS, HIGH);
  selectSD();
  delay(10);
  if (!sd.begin(PIN_SD_CS, SD_SCK_MHZ(10)))
  {
    deselectSD();
    Serial.println("[SD] sd.begin() FAILED.");
    return false;
  }
  loadMetadataLocked();
  pendingCacheDirty = true;
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
          file.println(F("rfid,timestamp,device_id,unix_time,crc8"));
          file.close();
          currentQueueFile = i;
          Serial.printf("[SD] Created new queue file: %s\n", fn);
          break;
        }
      }
      else
      {
        int cnt = countValidRecordsInFileLocked(fn);
        if (cnt == 0)
        {

          Serial.printf("[SD] File %s all expired, reusing slot.\n", fn);
          sd.remove(fn);
          if (file.open(fn, O_WRONLY | O_CREAT))
          {
            file.println(F("rfid,timestamp,device_id,unix_time,crc8"));
            file.close();
            currentQueueFile = i;
            break;
          }
        }
        else if (cnt < MAX_RECORDS_PER_FILE)
        {
          currentQueueFile = i;
          Serial.printf("[SD] Resuming queue file: %s (validCnt=%d)\n", fn, cnt);
          break;
        }
      }
    }
    if (currentQueueFile == -1)
    {
      currentQueueFile = 0;
      Serial.println("[SD] WARNING: no suitable queue file found, defaulting to 0.");
    }
    rtcQueueFileValid = true;
  }
  deselectSD();
  Serial.printf("[SD] SD init OK. currentQueueFile=%d\n", currentQueueFile);
  return true;
}

// GAP-09: mencari slot antrian kosong/dapat dipakai ulang mulai dari startIdx,
// mengganti logika lama yang hanya memeriksa satu file berikutnya lalu langsung
// menyatakan QUEUE FULL. Harus dipanggil saat SD sudah diselect dan mutex sudah diambil.
bool findAvailableQueueSlotLocked(int startIdx, int *outIdx)
{
  char fn[20];
  for (int offset = 0; offset < QUEUE_SLOT_SEARCH_LIMIT; offset++)
  {
    esp_task_wdt_reset();
    taskYIELD();
    int idx = (startIdx + offset) % MAX_QUEUE_FILES;
    getQueueFileName(idx, fn, sizeof(fn));
    if (!sd.exists(fn))
    {
      *outIdx = idx;
      return true;
    }
    int cnt = countValidRecordsInFileLocked(fn);
    if (cnt == 0)
    {
      Serial.printf("[QUEUE] Slot %s kosong/kadaluwarsa, dipakai ulang.\n", fn);
      sd.remove(fn);
      *outIdx = idx;
      return true;
    }
    if (cnt < MAX_RECORDS_PER_FILE)
    {
      *outIdx = idx;
      return true;
    }
  }
  return false;
}

SaveResult saveToQueue(const char *rfid, const char *ts, unsigned long t)
{
  Serial.printf("[QUEUE] saveToQueue: rfid=%s ts=%s t=%lu\n", rfid, ts, t);
  if (!sdCardAvailable)
  {
    Serial.println("[QUEUE] SD not available.");
    return SAVE_SD_ERROR;
  }
  if (!acquireSD())
  {
    Serial.println("[QUEUE] SD mutex timeout.");
    return SAVE_SD_ERROR;
  }
  selectSD();

  bool dup = isDuplicateLocked(rfid, t);
  Serial.printf("saveToQueue: rfid=%s t=%lu isDup=%d currentFile=%d\n", rfid, t, dup, currentQueueFile);
  if (dup)
  {
    Serial.println("[QUEUE] Duplicate detected, not saving.");
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
    Serial.printf("[QUEUE] Creating new queue file: %s\n", curFn);
    if (file.open(curFn, O_WRONLY | O_CREAT))
    {
      file.println(F("rfid,timestamp,device_id,unix_time,crc8"));
      file.close();
    }
  }
  int curCnt = countRecordsInFileLocked(curFn);
  Serial.printf("[QUEUE] Current file %s has %d records.\n", curFn, curCnt);
  if (curCnt >= MAX_RECORDS_PER_FILE)
  {
    // GAP-09: pencarian slot antrian bertingkat, tidak lagi hanya memeriksa satu file berikutnya.
    int nextIdx;
    int startSearch = (currentQueueFile + 1) % MAX_QUEUE_FILES;
    if (!findAvailableQueueSlotLocked(startSearch, &nextIdx))
    {
      Serial.printf("[QUEUE] Tidak ditemukan slot kosong dalam %d percobaan sejak file %d, QUEUE FULL.\n",
                    QUEUE_SLOT_SEARCH_LIMIT, currentQueueFile);
      deselectSD();
      releaseSD();
      return SAVE_QUEUE_FULL;
    }
    currentQueueFile = nextIdx;
    getQueueFileName(currentQueueFile, curFn, sizeof(curFn));
    Serial.printf("[QUEUE] Rolling to queue file: %s\n", curFn);
    if (!file.open(curFn, O_WRONLY | O_CREAT))
    {
      Serial.println("[QUEUE] Failed to open next queue file.");
      deselectSD();
      releaseSD();
      return SAVE_SD_ERROR;
    }
    file.println(F("rfid,timestamp,device_id,unix_time,crc8"));
    file.close();
  }
  if (!file.open(curFn, O_WRONLY | O_APPEND))
  {
    Serial.printf("[QUEUE] Failed to open %s for append.\n", curFn);
    deselectSD();
    releaseSD();
    return SAVE_SD_ERROR;
  }
  uint8_t crc = recordCrc8(rfid, t);
  file.print(rfid);
  file.print(',');
  file.print(ts);
  file.print(',');
  file.print(deviceId);
  file.print(',');
  file.print(t);
  file.print(',');
  char crcBuf[3];
  snprintf(crcBuf, sizeof(crcBuf), "%02X", crc);
  file.println(crcBuf);
  file.sync();
  file.close();
  deselectSD();

  cachedPendingRecords++;
  if (cachedPendingRecords < 0)
    cachedPendingRecords = 0;
  pendingCacheDirty = false;
  saveMetadataLocked();
  releaseSD();
  Serial.printf("[QUEUE] Record saved. cachedPending=%d\n", cachedPendingRecords);
  return SAVE_OK;
}

bool nvsSyncToServer()
{
  int cnt = nvsGetCount();
  Serial.printf("[NVS_SYNC] Starting NVS sync: %d records...\n", cnt);
  if (cnt == 0)
  {
    Serial.println("[NVS_SYNC] Nothing to sync.");
    return true;
  }
  if (isSignalCritical())
  {
    Serial.println("[NVS_SYNC] Signal critical, abort.");
    return false;
  }
  HTTPClient http;
  http.setTimeout(30000);
  http.setConnectTimeout(10000);
  char url[80];
  strcpy(url, apiBaseUrl);
  strcat(url, "/api/presensi/sync-bulk");
  if (!http.begin(getHttpClient(), url))
  {
    Serial.println("[NVS_SYNC] http.begin failed.");
    return false;
  }
  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("X-API-KEY"), apiKey);
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
  taskYIELD();
  Serial.printf("[NVS_SYNC] HTTP POST result: %d\n", code);
  if (code == 200)
  {
    String body = http.getString();
    esp_task_wdt_reset();
    http.end();
    DynamicJsonDocument res(512 + (size_t)cnt * 128);
    if (deserializeJson(res, body) == DeserializationError::Ok)
      for (JsonObject item : res["data"].as<JsonArray>())
      {
        const char *st = item["status"] | "error";
        if (strcmp(st, "error") == 0)
        {
          Serial.printf("[NVS_SYNC] Server error for rfid=%s: %s\n", item["rfid"] | "?", item["message"] | "?");
          appendFailedLogToSD(item["rfid"] | "unknown", item["timestamp"] | "unknown", item["message"] | "UNKNOWN");
        }
      }
    nvsSetCount(0);
    for (int i = 0; i < cnt; i++)
      nvsDeleteRecord(i);
    Serial.println("[NVS_SYNC] NVS sync complete, buffer cleared.");
    return true;
  }
  http.end();
  Serial.printf("[NVS_SYNC] Sync FAILED, HTTP code=%d\n", code);
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
  strcpy(url, apiBaseUrl);
  strcat(url, "/api/presensi/rfid-list/version");
  if (!http.begin(getHttpClient(), url))
    return 0;
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
  Serial.println("[RFID_DB] Downloading RFID DB...");
  if (isSignalWeak() || !sdCardAvailable)
  {
    Serial.println("[RFID_DB] Signal weak or no SD, abort.");
    return false;
  }
  showOLED(F("RFID DB"), "MENGUNDUH...");
  HTTPClient http;
  http.setTimeout(30000);
  http.setConnectTimeout(10000);
  char url[80];
  strcpy(url, apiBaseUrl);
  strcat(url, "/api/presensi/rfid-list");
  if (!http.begin(getHttpClient(), url))
  {
    Serial.println("[RFID_DB] http.begin failed.");
    return false;
  }
  http.addHeader(F("X-API-KEY"), apiKey);
  int code = http.GET();
  Serial.printf("[RFID_DB] HTTP GET result: %d\n", code);
  if (code != 200)
  {
    http.end();
    Serial.println("[RFID_DB] Download FAILED.");
    showOLED(F("RFID DB"), "GAGAL UNDUH");
    playToneError();
    delay(800);
    return false;
  }
  if (!acquireSD())
  {
    http.end();
    return false;
  }
  selectSD();
  const char *tmpPath = "/rfid_db.tmp";
  if (sd.exists(tmpPath))
    sd.remove(tmpPath);
  FsFile dbf;
  if (!dbf.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC))
  {
    Serial.println("[RFID_DB] Failed to open temp file.");
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
  bool lineTruncated = false; // GAP-15
  int truncatedLineCount = 0; // GAP-15
  uint8_t chunk[256];
  while (http.connected() && (total < 0 || written < total))
  {
    esp_task_wdt_reset();
    taskYIELD();
    int avail = stream->available();
    if (!avail)
    {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    int rd = stream->readBytes(chunk, min(avail, (int)sizeof(chunk)));
    for (int i = 0; i < rd; i++)
    {
      char c = (char)chunk[i];
      if (c == '\r')
        continue;
      if (c == '\n')
      {
        lineBuf[lbPos] = '\0';
        lbPos = 0;
        if (lineTruncated)
        {
          truncatedLineCount++;
          lineTruncated = false;
        }
        if (firstLine)
        {
          firstLine = false;
          if (strncmp(lineBuf, "ver:", 4) == 0)
          {
            serverVer = strtoul(lineBuf + 4, nullptr, 10);
            Serial.printf("[RFID_DB] Server DB version: %lu\n", serverVer);
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
        else
          lineTruncated = true; // GAP-15: karakter berikutnya dibuang, tandai baris ini terpotong
      }
    }
  }
  http.end();
  dbf.sync();
  dbf.close();
  if (sd.exists(RFID_DB_FILE))
    sd.remove(RFID_DB_FILE);
  sd.rename(tmpPath, RFID_DB_FILE);
  loadRfidCacheFromFileLocked();
  deselectSD();
  releaseSD();
  if (serverVer > 0)
    nvsSetRfidDbVer(serverVer);
  // GAP-15: laporkan jika ada baris yang terpotong melebihi buffer saat unduh.
  if (truncatedLineCount > 0)
    Serial.printf("[RFID_DB] PERINGATAN: %d baris terpotong melebihi buffer %d byte saat unduh.\n",
                  truncatedLineCount, (int)sizeof(lineBuf));
  Serial.printf("[RFID_DB] Download complete: %d entries written, ver=%lu\n", written, serverVer);
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
  unsigned long local = nvsGetRfidDbVer(), server = checkRfidDbVersion();
  Serial.printf("[RFID_DB] Version check: local=%lu server=%lu\n", local, server);
  if (server == 0 || server <= local)
  {
    Serial.println("[RFID_DB] DB up to date, skip download.");
    return;
  }
  downloadRfidDb();
}

void sendTelemetry()
{
  if (isSignalWeak())
    return;
  if (millis() - timers.lastTelemetry < TELEMETRY_INTERVAL)
    return;
  timers.lastTelemetry = millis();
  Serial.println("[TELE] Sending telemetry heartbeat...");
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  char url[80];
  strcpy(url, apiBaseUrl);
  strcat(url, "/api/presensi/heartbeat");
  if (!http.begin(getHttpClient(), url))
  {
    Serial.println("[TELE] http.begin failed.");
    return;
  }
  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("X-API-KEY"), apiKey);
  DynamicJsonDocument doc(512);
  doc["device_id"] = deviceId;
  doc["device_name"] = deviceName;
  doc["firmware"] = FIRMWARE_VERSION;
  doc["uptime_sec"] = millis() / 1000;
  doc["heap_free"] = esp_get_free_heap_size();
  doc["pending_records"] = cachedPendingRecords + nvsGetCount();
  doc["scan_today"] = nvsGetScanCount();
  doc["rssi"] = isWifiConnected() ? (int)WiFi.RSSI() : 0;
  doc["sd_ok"] = sdCardAvailable;
  doc["rfid_db_entries"] = rfidCacheCount;
  doc["online"] = isOnline;
  String payload;
  serializeJson(doc, payload);
  int code = http.POST(payload);
  http.end();
  Serial.printf("[TELE] Heartbeat sent, HTTP=%d\n", code);
}

void fetchRemoteConfig()
{
  if (isSignalWeak())
    return;
  if (millis() - timers.lastRemoteConfig < REMOTE_CONFIG_INTERVAL)
    return;
  timers.lastRemoteConfig = millis();
  Serial.println("[CFG] Fetching remote config...");
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  char url[100];
  snprintf(url, sizeof(url), "%s/api/presensi/config?device_id=%s", apiBaseUrl, deviceId);
  if (!http.begin(getHttpClient(), url))
  {
    Serial.println("[CFG] http.begin failed.");
    return;
  }
  http.addHeader(F("X-API-KEY"), apiKey);
  int code = http.GET();
  Serial.printf("[CFG] Remote config HTTP=%d\n", code);
  if (code != 200)
  {
    http.end();
    return;
  }
  String body = http.getString();
  http.end();
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, body) != DeserializationError::Ok)
  {
    Serial.println("[CFG] JSON parse error.");
    return;
  }

  bool changed = false;     // GAP-18
  RuntimeConfig snapshot{}; // GAP-18

  if (xConfigMutex && xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(500)) == pdTRUE)
  {
    if (doc.containsKey("sleep_start"))
    {
      rtCfg.sleepStartHour = doc["sleep_start"];
      changed = true;
      Serial.printf("[CFG] sleep_start=%d\n", rtCfg.sleepStartHour);
    }
    if (doc.containsKey("sleep_end"))
    {
      rtCfg.sleepEndHour = doc["sleep_end"];
      changed = true;
      Serial.printf("[CFG] sleep_end=%d\n", rtCfg.sleepEndHour);
    }
    if (doc.containsKey("oled_dim_start"))
    {
      rtCfg.dimStartHour = doc["oled_dim_start"];
      changed = true;
      Serial.printf("[CFG] dim_start=%d\n", rtCfg.dimStartHour);
    }
    if (doc.containsKey("oled_dim_end"))
    {
      rtCfg.dimEndHour = doc["oled_dim_end"];
      changed = true;
      Serial.printf("[CFG] dim_end=%d\n", rtCfg.dimEndHour);
    }
    if (doc.containsKey("sync_interval_ms"))
    {
      rtCfg.syncIntervalMs = doc["sync_interval_ms"];
      changed = true;
      Serial.printf("[CFG] sync_iv=%lu\n", rtCfg.syncIntervalMs);
    }
    if (doc.containsKey("ota_check_interval_ms"))
    {
      rtCfg.otaCheckIntervalMs = doc["ota_check_interval_ms"];
      changed = true;
      Serial.printf("[CFG] ota_iv=%lu\n", rtCfg.otaCheckIntervalMs);
    }
    snapshot = rtCfg; // GAP-18
    xSemaphoreGive(xConfigMutex);
    Serial.println("[CFG] Remote config applied.");
  }
  else
  {
    Serial.println("[CFG] PERINGATAN: gagal ambil xConfigMutex, remote config TIDAK diterapkan siklus ini.");
    return;
  }

  // GAP-18: persist di luar mutex agar penulisan flash NVS tidak menahan task lain.
  if (changed)
    persistRuntimeConfigToNvs(snapshot);
}

// GAP-07: pembanding versi semantik (major.minor.patch), menggantikan strcmp leksikal
// yang salah untuk kasus seperti "2.10.0" vs "2.9.0".
// TODO: GAP-07 ASUMSI - format versi selalu "X.Y.Z" numerik dipisah titik, maksimum 3 komponen.
// Mengembalikan: <0 jika a < b, 0 jika sama, >0 jika a > b.
static int compareFirmwareVersion(const char *a, const char *b)
{
  int aMaj = 0, aMin = 0, aPat = 0;
  int bMaj = 0, bMin = 0, bPat = 0;
  sscanf(a, "%d.%d.%d", &aMaj, &aMin, &aPat);
  sscanf(b, "%d.%d.%d", &bMaj, &bMin, &bPat);
  if (aMaj != bMaj)
    return (aMaj < bMaj) ? -1 : 1;
  if (aMin != bMin)
    return (aMin < bMin) ? -1 : 1;
  if (aPat != bPat)
    return (aPat < bPat) ? -1 : 1;
  return 0;
}

void checkOtaUpdate()
{
  if (isSignalWeak())
    return;
  // GAP-12: snapshot rtCfg, hindari race dengan fetchRemoteConfig() pada task yang sama.
  RuntimeConfig cfg = getRuntimeConfigSnapshot();
  if (millis() - timers.lastOtaCheck < cfg.otaCheckIntervalMs)
    return;
  timers.lastOtaCheck = millis();
  Serial.println("[OTA] Checking for firmware update...");
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  char url[80];
  strcpy(url, apiBaseUrl);
  strcat(url, "/api/presensi/firmware/check");
  if (!http.begin(getHttpClient(), url))
  {
    Serial.println("[OTA] http.begin failed.");
    return;
  }
  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("X-API-KEY"), apiKey);
  char payload[80];
  snprintf(payload, sizeof(payload), "{\"version\":\"%s\",\"device_id\":\"%s\"}", FIRMWARE_VERSION, deviceId);
  int code = http.POST(payload);
  Serial.printf("[OTA] Check HTTP=%d\n", code);
  if (code != 200)
  {
    http.end();
    return;
  }
  String body = http.getString();
  http.end();
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, body) != DeserializationError::Ok)
  {
    Serial.println("[OTA] JSON parse error.");
    return;
  }
  bool hasUpdate = doc["update"] | false;
  const char *ver = doc["version"] | "";
  const char *burl = doc["url"] | "";
  const char *md5 = doc["md5"] | "";
  Serial.printf("[OTA] hasUpdate=%d ver=%s\n", hasUpdate, ver);
  if (!hasUpdate || !strlen(ver) || !strlen(burl))
    return;
  // GAP-07: pembanding versi semantik menggantikan strcmp leksikal.
  if (compareFirmwareVersion(ver, FIRMWARE_VERSION) <= 0)
  {
    Serial.printf("[OTA] Versi server (%s) tidak lebih baru dari versi lokal (%s).\n", ver, FIRMWARE_VERSION);
    return;
  }
  strncpy(otaState.version, ver, sizeof(otaState.version) - 1);
  strncpy(otaState.url, burl, sizeof(otaState.url) - 1);
  strncpy(otaState.md5, md5, sizeof(otaState.md5) - 1);
  otaState.updateAvailable = true;
  Serial.printf("[OTA] Update available: v%s url=%s\n", otaState.version, otaState.url);
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
  Serial.printf("[OTA] Starting OTA update to v%s...\n", otaState.version);
  char buf[20];
  snprintf(buf, sizeof(buf), "v%s", otaState.version);
  showOLED(F("UPDATE OTA"), buf);
  delay(500);
  showOLED(F("MENGUNDUH"), "MOHON TUNGGU...");
  extendWdtForSync();
  WiFiClientSecure otaClient;
  otaClient.setInsecure();
  otaClient.setHandshakeTimeout(10);
  HTTPClient http;
  http.begin(otaClient, otaState.url);
  http.addHeader(F("X-API-KEY"), apiKey);
  http.setTimeout(60000);
  int code = http.GET();
  Serial.printf("[OTA] Download HTTP=%d\n", code);
  if (code != 200)
  {
    snprintf(buf, sizeof(buf), "HTTP ERR %d", code);
    Serial.printf("[OTA] Download FAILED: %s\n", buf);
    showOLED(F("UPDATE GAGAL"), buf);
    playToneError();
    http.end();
    otaState.updateAvailable = false;
    restoreWdtNormal();
    return;
  }
  int total = http.getSize();
  Serial.printf("[OTA] Firmware size: %d bytes\n", total);
  WiFiClient *stream = http.getStreamPtr();

  // GAP-08: total dapat -1 jika server tidak mengirim Content-Length (chunked transfer).
  // Sebelumnya (size_t)(-1) menjadi nilai raksasa dan dikirim langsung ke Update.begin().
  // TODO: verifikasi konstanta UPDATE_SIZE_UNKNOWN terhadap Update.h versi ESP32 core 3.3.10 terpasang.
  size_t updateSize;
  if (total <= 0)
  {
    Serial.println("[OTA] Content-Length tidak tersedia, menggunakan UPDATE_SIZE_UNKNOWN.");
    updateSize = UPDATE_SIZE_UNKNOWN;
  }
  else
  {
    updateSize = (size_t)total;
  }

  if (!Update.begin(updateSize))
  {
    Serial.println("[OTA] Update.begin FAILED — not enough space.");
    showOLED(F("UPDATE GAGAL"), "NO SPACE");
    playToneError();
    http.end();
    otaState.updateAvailable = false;
    restoreWdtNormal();
    return;
  }
  if (strlen(otaState.md5) > 0)
  {
    Update.setMD5(otaState.md5);
    Serial.printf("[OTA] Expected MD5: %s\n", otaState.md5);
  }
  uint8_t buff[1024];
  int written = 0;
  // GAP-08: kondisi loop diperbaiki agar tetap membaca stream saat total tidak diketahui (<=0).
  // Sebelumnya "written < total" dengan total=-1 membuat loop TIDAK PERNAH BERJALAN (0 < -1 = false),
  // sehingga OTA dengan respons chunked akan gagal total secara diam-diam.
  // TODO: ASUMSI - penanganan HTTPClient terhadap Transfer-Encoding: chunked belum diverifikasi
  // di lapangan untuk kasus total<=0; perlu pengujian langsung terhadap endpoint OTA backend.
  while (http.connected() && (total <= 0 || written < total))
  {
    int avail = stream->available();
    if (avail)
    {
      int rd = stream->readBytes(buff, min((int)sizeof(buff), avail));
      Update.write(buff, rd);
      written += rd;
    }
    esp_task_wdt_reset();
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  http.end();
  Serial.printf("[OTA] Written: %d / %d bytes\n", written, total);
  if (Update.end() && Update.isFinished())
  {
    Serial.println("[OTA] Update finished successfully. Restarting...");
    showOLED(F("UPDATE OK"), "RESTART...");
    playToneSuccess();
    delay(2000);
    restoreWdtNormal();
    ESP.restart();
  }
  else
  {
    Serial.printf("[OTA] Update.end FAILED, error=%d\n", Update.getError());
    snprintf(buf, sizeof(buf), "ERR %d", Update.getError());
    showOLED(F("UPDATE GAGAL"), buf);
    playToneError();
    otaState.updateAvailable = false;
  }
  restoreWdtNormal();
  memset(previousDisplay.time, 0xFF, sizeof(previousDisplay.time));
  previousDisplay.pendingRecords = -1;
}

bool readQueueFileLocked(const char *fn, OfflineRecord *recs, int *cnt, int maxCnt)
{
  if (!file.open(fn, O_RDONLY))
    return false;
  *cnt = 0;
  time_t now = time(nullptr);
  char line[144];
  if (file.available())
    file.fgets(line, sizeof(line));
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
    char *c4 = c3 ? strchr(c3 + 1, ',') : nullptr;
    if (!c1 || !c2 || !c3)
      continue;
    int rl = c1 - line, tl = c2 - c1 - 1, dl = c3 - c2 - 1;
    if (rl <= 0 || tl <= 0)
      continue;
    unsigned long recT = strtoul(c3 + 1, nullptr, 10);
    if (c4)
    {
      char crcStr[4];
      strncpy(crcStr, c4 + 1, 3);
      crcStr[3] = '\0';
      char rfidTmp[11];
      memcpy(rfidTmp, line, min(rl, 10));
      rfidTmp[min(rl, 10)] = '\0';
      uint8_t expected = recordCrc8(rfidTmp, recT);
      char expectedStr[3];
      snprintf(expectedStr, sizeof(expectedStr), "%02X", expected);
      if (strcmp(crcStr, expectedStr) != 0)
        continue;
    }
    strncpy(recs[*cnt].rfid, line, min(rl, 10));
    recs[*cnt].rfid[min(rl, 10)] = '\0';
    strncpy(recs[*cnt].timestamp, c1 + 1, min(tl, 19));
    recs[*cnt].timestamp[min(tl, 19)] = '\0';
    strncpy(recs[*cnt].deviceId, c2 + 1, min(dl, 19));
    recs[*cnt].deviceId[min(dl, 19)] = '\0';
    recs[*cnt].unixTime = recT;
    if (recs[*cnt].timestamp[0] == '\0')
      appendFailedLogToSD(recs[*cnt].rfid, "empty", "TIMESTAMP_KOSONG");
    else if ((unsigned long)now - recs[*cnt].unixTime <= MAX_OFFLINE_AGE)
      (*cnt)++;
  }
  file.close();
  return *cnt > 0;
}

SyncFileResult syncQueueFile(const char *fn)
{
  Serial.printf("[SYNC] Syncing file: %s\n", fn);
  if (!sdCardAvailable || !isWifiConnected())
  {
    Serial.println("[SYNC] No SD or no WiFi, abort.");
    return SYNC_FILE_NO_WIFI;
  }
  OfflineRecord recs[MAX_RECORDS_PER_FILE];
  int validCnt = 0;
  if (!acquireSD())
    return SYNC_FILE_HTTP_FAIL;
  selectSD();
  bool hasData = readQueueFileLocked(fn, recs, &validCnt, MAX_RECORDS_PER_FILE);
  Serial.printf("[SYNC] File %s: hasData=%d validCnt=%d\n", fn, hasData, validCnt);
  if (!hasData || validCnt == 0)
  {
    if (sd.exists(fn))
      sd.remove(fn);
    deselectSD();
    releaseSD();
    pendingCacheDirty = true;
    Serial.println("[SYNC] File empty, removed.");
    return SYNC_FILE_EMPTY;
  }
  deselectSD();
  releaseSD();
  HTTPClient http;
  http.setTimeout(45000);
  http.setConnectTimeout(15000);
  char url[80];
  strcpy(url, apiBaseUrl);
  strcat(url, "/api/presensi/sync-bulk");
  if (!http.begin(getHttpClient(), url))
  {
    Serial.println("[SYNC] http.begin failed.");
    return SYNC_FILE_HTTP_FAIL;
  }
  http.addHeader(F("Content-Type"), F("application/json"));
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
  taskYIELD();
  Serial.printf("[SYNC] HTTP POST result: %d\n", code);
  if (code == 200)
  {
    String body = http.getString();
    esp_task_wdt_reset();
    http.end();
    DynamicJsonDocument res(512 + (size_t)validCnt * 128);
    if (deserializeJson(res, body) == DeserializationError::Ok)
      for (JsonObject item : res["data"].as<JsonArray>())
      {
        const char *st = item["status"] | "error";
        if (strcmp(st, "error") == 0)
        {
          Serial.printf("[SYNC] Server error for rfid=%s: %s\n", item["rfid"] | "?", item["message"] | "?");
          appendFailedLogToSD(item["rfid"] | "unknown", item["timestamp"] | "unknown", item["message"] | "UNKNOWN");
        }
      }
    if (!acquireSD())
      return SYNC_FILE_HTTP_FAIL;
    selectSD();
    sd.remove(fn);
    deselectSD();
    releaseSD();
    pendingCacheDirty = true;
    if (cachedPendingRecords >= validCnt)
      cachedPendingRecords -= validCnt;
    else
      cachedPendingRecords = 0;
    Serial.printf("[SYNC] File %s synced OK. pendingRecords=%d\n", fn, cachedPendingRecords);
    return SYNC_FILE_OK;
  }
  http.end();
  if (!isWifiConnected())
  {
    Serial.println("[SYNC] WiFi lost during sync.");
    syncState.inProgress = false;
    return SYNC_FILE_NO_WIFI;
  }
  Serial.printf("[SYNC] HTTP FAIL code=%d\n", code);
  return SYNC_FILE_HTTP_FAIL;
}

bool syncQueueFileWithRetry(const char *fn)
{
  for (int attempt = 0; attempt <= MAX_SYNC_RETRIES; attempt++)
  {
    Serial.printf("[SYNC] Attempt %d/%d for %s\n", attempt + 1, MAX_SYNC_RETRIES + 1, fn);
    esp_task_wdt_reset();
    if (!isWifiConnected())
    {
      Serial.println("[SYNC] WiFi lost, abort retry.");
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
      Serial.printf("[SYNC] Retry %d/%d for %s\n", attempt + 1, MAX_SYNC_RETRIES, fn);
      showOLED(F("SYNC ULANG"), buf);
      unsigned long end = millis() + SYNC_RETRY_DELAY_MS * (1UL << attempt);
      while (millis() < end)
      {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
  }
  Serial.printf("[SYNC] All retries failed for %s\n", fn);
  return false;
}

void chunkedSync()
{
  if (!sdCardAvailable || !isWifiConnected())
  {
    Serial.println("[SYNC] chunkedSync: no SD or no WiFi.");
    syncState.inProgress = false;
    return;
  }
  extendWdtForSync();
  if (!syncState.inProgress)
  {
    Serial.println("[SYNC] Starting new chunked sync session...");
    syncState.inProgress = true;
    syncState.currentFile = 0;
    syncState.startTime = millis();
    syncState.filesProcessed = 0;
    syncState.filesSucceeded = 0;
  }
  char fn[20];
  int emptyStreak = 0;
  while (syncState.currentFile < MAX_QUEUE_FILES && syncState.filesProcessed < MAX_SYNC_FILES_PER_CYCLE)
  {
    if (!isWifiConnected())
    {
      Serial.println("[SYNC] WiFi lost during chunked sync.");
      syncState.inProgress = false;
      break;
    }
    esp_task_wdt_reset();
    taskYIELD();
    getQueueFileName(syncState.currentFile, fn, sizeof(fn));
    if (!acquireSD(pdMS_TO_TICKS(1000)))
    {
      syncState.currentFile++;
      continue;
    }
    bool exists = sd.exists(fn);
    if (!exists)
    {
      releaseSD();
      syncState.currentFile++;
      emptyStreak++;
      if (emptyStreak >= 20)
      {
        Serial.println("[SYNC] 20 consecutive empty slots, ending sync.");
        syncState.currentFile = MAX_QUEUE_FILES;
        break;
      }
      continue;
    }
    emptyStreak = 0;

    int nRecs = countValidRecordsInFileLocked(fn);
    if (nRecs == 0)
    {

      Serial.printf("[SYNC] File %s empty/all expired, removing.\n", fn);
      sd.remove(fn);
      pendingCacheDirty = true;
      releaseSD();
      syncState.currentFile++;
      syncState.filesProcessed++;
      continue;
    }
    releaseSD();

    char buf[24];
    snprintf(buf, sizeof(buf), "FILE %d (%d rec)", syncState.currentFile, nRecs);
    Serial.printf("[SYNC] Processing file %d with %d valid records.\n", syncState.currentFile, nRecs);
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
    syncState.currentFile++;
    taskYIELD();
    esp_task_wdt_reset();
  }
  if (syncState.currentFile >= MAX_QUEUE_FILES)
  {
    Serial.printf("[SYNC] Chunked sync done. processed=%d succeeded=%d pending=%d\n",
                  syncState.filesProcessed, syncState.filesSucceeded, cachedPendingRecords);
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

bool connectToWifi(int ssidIdx)
{
  Serial.printf("[WIFI] Connecting to SSID[%d]: '%s'\n", ssidIdx, wifiCreds[ssidIdx].ssid);
  if (strlen(wifiCreds[ssidIdx].ssid) == 0)
  {
    Serial.println("[WIFI] SSID empty, skip.");
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.enableIPv6(false);
  WiFi.disconnect(true);
  delay(100);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setSleep(WIFI_PS_MAX_MODEM);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifiCreds[ssidIdx].ssid, wifiCreds[ssidIdx].pass);
  for (int i = 0; i < 20 && !isWifiConnected(); i++)
  {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(300));
    if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor((SCREEN_WIDTH - (int)strlen(wifiCreds[ssidIdx].ssid) * 6) / 2, 10);
      display.println(wifiCreds[ssidIdx].ssid);
      display.setCursor(35, 30);
      display.print(F("CONNECTING"));
      for (int j = 0; j < (i % 4); j++)
        display.print('.');
      display.display();
      xSemaphoreGive(xDisplayMutex);
    }
  }
  if (isWifiConnected())
  {
    Serial.printf("[WIFI] Connected! IP=%s RSSI=%ld dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    char buf[20];
    snprintf(buf, sizeof(buf), "RSSI: %ld dBm", WiFi.RSSI());
    showOLED(F("WIFI OK"), buf);
    isOnline = true;
    currentSsidIdx = ssidIdx;
    delay(1500);
    return true;
  }
  Serial.printf("[WIFI] Failed to connect to SSID[%d].\n", ssidIdx);
  isOnline = false;
  return false;
}

bool connectToWiFi()
{
  Serial.println("[WIFI] Trying all SSIDs...");
  for (int i = 0; i < 3; i++)
    if (connectToWifi(i))
      return true;
  Serial.println("[WIFI] All SSIDs failed.");
  return false;
}

bool pingAPI()
{
  Serial.println("=== PING START ===");
  Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
  Serial.printf("WiFi status: %d\n", WiFi.status());
  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  if (isSignalCritical())
  {
    Serial.println("PING: signal critical, abort");
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
  Serial.println("PING: delay done, starting HTTP...");
  HTTPClient http;
  http.setTimeout(15000);
  http.setConnectTimeout(10000);
  char url[80];
  strcpy(url, apiBaseUrl);
  strcat(url, "/api/presensi/ping");
  Serial.printf("PING: URL = %s\n", url);
  if (!http.begin(getHttpClient(), url))
  {
    Serial.println("PING: http.begin FAILED");
    return false;
  }
  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("X-API-KEY"), apiKey);
  Serial.println("PING: calling http.GET...");
  esp_task_wdt_reset();
  int code = http.GET();
  esp_task_wdt_reset();
  Serial.printf("PING: code = %d\n", code);
  Serial.printf("PING: error = %s\n", http.errorToString(code).c_str());
  if (code > 0)
  {
    String body = http.getString();
    Serial.printf("PING: body = %s\n", body.c_str());
  }
  http.end();
  isOnline = (code == 200);
  Serial.printf("PING: isOnline = %d\n", isOnline);
  Serial.println("=== PING END ===");
  return isOnline;
}

void processReconnect()
{

  switch (reconnectState)
  {
  case RECONNECT_IDLE:
    if (!isWifiConnected())
    {
      unsigned long elapsed = millis() - timers.lastReconnect;
      if (elapsed >= RECONNECT_INTERVAL)
      {
        Serial.printf("[RECONNECT] Memulai reconnect (elapsed=%lums)...\n", elapsed);
        timers.lastReconnect = millis();
        reconnectState = RECONNECT_INIT;
      }
    }
    else
    {
      if (!isOnline)
      {
        isOnline = true;
        Serial.println("[RECONNECT] WiFi terdeteksi online dari IDLE.");
      }
    }
    break;

  case RECONNECT_INIT:
    WiFi.disconnect(true);
    delay(100);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setSleep(WIFI_PS_MAX_MODEM);
    {
      int nextIdx = (currentSsidIdx + 1) % 3;
      for (int i = 0; i < 3; i++)
      {
        int idx = (nextIdx + i) % 3;
        if (strlen(wifiCreds[idx].ssid) > 0)
        {
          WiFi.enableIPv6(false);
          WiFi.begin(wifiCreds[idx].ssid, wifiCreds[idx].pass);
          currentSsidIdx = idx;
          break;
        }
      }
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

void uidToString(uint8_t *uid, uint8_t len, char *out)
{
  // GAP-06: keputusan sadar - encoding 4-byte-pertama-UID dipertahankan karena format
  // RFID di lapangan memang 10 digit desimal (contoh: "9586235215"), konsisten dengan
  // kartu yang dipakai saat ini. Tidak diubah agar tidak memutus data presensi existing.
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
  Serial.printf("[HTTP] kirimLangsung: rfid=%s ts=%s\n", rfid, ts);
  if (!isWifiConnected())
  {
    Serial.println("[HTTP] No WiFi.");
    return false;
  }
  HTTPClient http;
  http.setTimeout(4000);
  http.setConnectTimeout(2000);
  char url[80];
  strcpy(url, apiBaseUrl);
  strcat(url, "/api/presensi");
  if (!http.begin(getHttpClient(), url))
  {
    Serial.println("[HTTP] http.begin failed.");
    return false;
  }
  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("X-API-KEY"), apiKey);
  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"rfid\":\"%s\",\"timestamp\":\"%s\",\"device_id\":\"%s\",\"sync_mode\":false}",
           rfid, ts, deviceId);
  int code = http.POST(payload);
  http.end();
  Serial.printf("[HTTP] kirimLangsung code=%d\n", code);
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
  if (code == 403)
  {
    strcpy(msg, "HARI LIBUR!");
    return false;
  }
  if (code == 404)
  {
    strcpy(msg, "RFID NONAKTIF");
    return false;
  }
  snprintf(msg, 32, "SERVER ERR %d", code);
  return false;
}

// GAP-10: pemeriksaan scan berulang terpadu berbasis NVS-last-scan, dipakai di seluruh
// jalur (SD, direct HTTP, offline buffer) agar perilaku konsisten. Sebelumnya hanya
// dipanggil pada jalur SD, sehingga jalur direct-HTTP-fallback dan offline-buffer
// tidak memiliki proteksi duplikasi lokal sama sekali.
bool isDuplicateScanRecent(const char *rfid, unsigned long t, const char *sourceTag)
{
  bool dup = nvsIsRecentScan(rfid, t);
  if (dup)
    Serial.printf("[DEDUP] Duplikat terdeteksi via NVS-last-scan (sumber=%s) rfid=%s\n", sourceTag, rfid);
  return dup;
}

bool kirimPresensi(const char *rfid, char *msg)
{
  Serial.printf("kirimPresensi: rfid=%s sdAvail=%d wifiConn=%d\n", rfid, sdCardAvailable, isWifiConnected());
  if (!isTimeValid())
  {
    Serial.println("[PRESENSI] Waktu tidak valid.");
    strcpy(msg, "WAKTU INVALID");
    return false;
  }
  char ts[20];
  getFormattedTimestamp(ts, sizeof(ts));
  time_t now = time(nullptr);

  // GAP-10: pemeriksaan duplikasi terpadu, berlaku untuk seluruh jalur penyimpanan
  // (sebelumnya hanya diperiksa jika SD tersedia).
  const char *pathTag = sdCardAvailable ? "SD" : (isWifiConnected() ? "DIRECT_HTTP" : "OFFLINE_BUFFER");
  if (isDuplicateScanRecent(rfid, (unsigned long)now, pathTag))
  {
    strcpy(msg, "CUKUP SEKALI!");
    return false;
  }

  if (sdCardAvailable)
  {
    if (!isRfidInCache(rfid))
    {
      Serial.printf("[PRESENSI] rfid=%s not in cache.\n", rfid);
      strcpy(msg, "RFID NONAKTIF");
      return false;
    }
    SaveResult r = saveToQueue(rfid, ts, (unsigned long)now);
    Serial.printf("kirimPresensi: saveResult=%d\n", r);
    switch (r)
    {
    case SAVE_OK:
      nvsBumpScanCount();
      nvsSaveLastScan(rfid, (unsigned long)now);
      Serial.printf("[PRESENSI] Saved to queue. queueWarn=%d\n", cachedQueueFileCount >= QUEUE_WARN_THRESHOLD);
      strcpy(msg, cachedQueueFileCount >= QUEUE_WARN_THRESHOLD ? "QUEUE HAMPIR PENUH!" : "DATA TERSIMPAN");
      return true;
    case SAVE_DUPLICATE:
      // GAP-10: lapis kedua ini berbasis isi file antrian SD (independen dari cache
      // NVS-last-scan di atas), disengaja sebagai pertahanan tambahan, bukan duplikasi logika mati.
      Serial.println("[PRESENSI] Duplicate in queue (deteksi lapis SD).");
      strcpy(msg, "CUKUP SEKALI!");
      return false;
    case SAVE_QUEUE_FULL:
      Serial.println("[PRESENSI] Queue full.");
      strcpy(msg, "QUEUE PENUH!");
      return false;
    default:
      Serial.println("[PRESENSI] SD error.");
      strcpy(msg, "SD CARD ERROR");
      return false;
    }
  }

  if (isWifiConnected())
  {
    Serial.println("[PRESENSI] No SD, trying direct HTTP...");
    if (kirimLangsung(rfid, ts, msg))
    {
      nvsBumpScanCount();
      nvsSaveLastScan(rfid, (unsigned long)now);
      return true;
    }

    if (strcmp(msg, "CUKUP SEKALI!") == 0 || strcmp(msg, "RFID NONAKTIF") == 0 || strcmp(msg, "HARI LIBUR!") == 0)
    {
      Serial.printf("[PRESENSI] Server rejected: %s\n", msg);
      return false;
    }
    Serial.println("[PRESENSI] HTTP fail, saving to NVS buffer...");
    // GAP-10: nvsIsDuplicate() sebelumnya tidak pernah dipanggil di manapun (dead code).
    // Sekarang dipakai agar jalur fallback buffer NVS juga terlindungi dari duplikasi.
    if (nvsIsDuplicate(rfid, (unsigned long)now))
    {
      Serial.printf("[DEDUP] Duplikat terdeteksi via NVS-buffer-scan (sumber=DIRECT_HTTP_FALLBACK) rfid=%s\n", rfid);
      strcpy(msg, "CUKUP SEKALI!");
      return false;
    }
    if (nvsSaveToBuffer(rfid, ts, (unsigned long)now))
    {
      nvsBumpScanCount();
      nvsSaveLastScan(rfid, (unsigned long)now);
      Serial.printf("[PRESENSI] Saved to NVS buffer: %d/%d\n", nvsGetCount(), NVS_MAX_RECORDS);
      snprintf(msg, 32, "BUFFER %d/%d", nvsGetCount(), NVS_MAX_RECORDS);
      return true;
    }
    Serial.println("[PRESENSI] NVS buffer full.");
    strcpy(msg, "BUFFER PENUH!");
    return false;
  }

  Serial.println("[PRESENSI] Offline, saving to NVS buffer...");
  // GAP-10: pemeriksaan duplikasi buffer NVS juga diterapkan pada jalur offline murni.
  if (nvsIsDuplicate(rfid, (unsigned long)now))
  {
    Serial.printf("[DEDUP] Duplikat terdeteksi via NVS-buffer-scan (sumber=OFFLINE_BUFFER) rfid=%s\n", rfid);
    strcpy(msg, "CUKUP SEKALI!");
    return false;
  }
  if (nvsSaveToBuffer(rfid, ts, (unsigned long)now))
  {
    nvsBumpScanCount();
    nvsSaveLastScan(rfid, (unsigned long)now);
    Serial.printf("[PRESENSI] Offline NVS saved: %d/%d\n", nvsGetCount(), NVS_MAX_RECORDS);
    snprintf(msg, 32, "BUFFER %d/%d", nvsGetCount(), NVS_MAX_RECORDS);
    return true;
  }
  Serial.println("[PRESENSI] NVS buffer full in offline mode.");
  strcpy(msg, "BUFFER PENUH!");
  return false;
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
    snprintf(currentDisplay.time, sizeof(currentDisplay.time), "%02d:%02d", ti.tm_hour, ti.tm_min);
  if (pendingCacheDirty)
    refreshPendingCache();
  currentDisplay.pendingRecords = cachedPendingRecords + nvsGetCount();
  if (currentDisplay.pendingRecords < 0)
    currentDisplay.pendingRecords = 0;
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
  if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(50)) != pdTRUE)
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
      int bh = 2 + i * 2, bx = SCREEN_WIDTH - 18 + i * 5;
      if (i < currentDisplay.wifiSignal)
        display.fillRect(bx, 10 - bh, 3, bh, WHITE);
      else
        display.drawRect(bx, 10 - bh, 3, bh, WHITE);
    }
  }
  display.display();
  xSemaphoreGive(xDisplayMutex);
  memcpy(&previousDisplay, &currentDisplay, sizeof(DisplayState));
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

void checkFactoryReset()
{
  if (digitalRead(PIN_BOOT) != LOW)
    return;
  esp_task_wdt_reset(); // GAP-17
  vTaskDelay(pdMS_TO_TICKS(100));
  if (digitalRead(PIN_BOOT) != LOW)
    return;
  esp_task_wdt_reset(); // GAP-17
  vTaskDelay(pdMS_TO_TICKS(100));
  if (digitalRead(PIN_BOOT) != LOW)
    return;
  esp_task_wdt_reset(); // GAP-17
  unsigned long held = millis();
  showOLED(F("TAHAN UNTUK"), "FACTORY RESET");
  while (digitalRead(PIN_BOOT) == LOW)
  {
    esp_task_wdt_reset();
    if (millis() - held >= FACTORY_RESET_HOLD_MS)
    {
      Serial.println("[RESET] Factory reset triggered!");
      showOLED(F("FACTORY RESET"), "MENGHAPUS...");
      playToneError();
      delay(500);
      prefs.begin(NVS_NS_CONFIG, false);
      prefs.clear();
      prefs.end();
      prefs.begin(NVS_NAMESPACE, false);
      prefs.clear();
      prefs.end();
      if (sdCardAvailable)
      {
        if (acquireSD(pdMS_TO_TICKS(3000)))
        {
          selectSD();
          sd.remove(RFID_DB_FILE);
          sd.remove(METADATA_FILE);
          sd.remove("/failed_log.csv");
          deselectSD();
          releaseSD();
        }
      }
      showOLED(F("RESET SELESAI"), "RESTART...");
      delay(2000);
      ESP.restart();
    }
    delay(100);
  }
  memset(previousDisplay.time, 0xFF, sizeof(previousDisplay.time));
}

static String provHtmlPage()
{
  String html;
  html.reserve(7500);
  html += F("<!DOCTYPE html><html lang='id'><head>"
            "<meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>Device Setup - Attendance Machine</title>"
            "<style>"
            "*{box-sizing:border-box;margin:0;padding:0}"
            "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
            "background:#0a0f1e;min-height:100vh;display:flex;flex-direction:column;"
            "align-items:center;justify-content:center;padding:16px}"
            ".card{background:#111827;border:1px solid #1f2937;border-radius:20px;"
            "width:100%;max-width:480px;overflow:hidden;box-shadow:0 25px 50px rgba(0,0,0,.5)}"
            ".header{padding:32px 32px 28px;border-bottom:1px solid #1f2937;"
            "background:linear-gradient(160deg,#111827 0%,#0f172a 100%)}"
            ".header-title h1{font-size:26px;font-weight:800;letter-spacing:-.5px;"
            "background:linear-gradient(90deg,#f8fafc,#94a3b8);"
            "-webkit-background-clip:text;-webkit-text-fill-color:transparent}"
            ".header-sub{color:#475569;font-size:12px;letter-spacing:2px;"
            "text-transform:uppercase;font-weight:500;margin-top:4px}"
            ".body{padding:28px 32px}"
            ".section{margin-bottom:22px}"
            ".section-label{color:#374151;font-size:10px;font-weight:700;"
            "letter-spacing:2px;text-transform:uppercase;margin-bottom:12px;"
            "display:flex;align-items:center;gap:10px}"
            ".section-label::after{content:'';flex:1;height:1px;background:#1f2937}"
            ".field{margin-bottom:10px}"
            "label{display:block;color:#6b7280;font-size:12px;font-weight:500;margin-bottom:5px}"
            "input,select{width:100%;background:#0a0f1e;border:1px solid #1f2937;border-radius:8px;"
            "color:#e5e7eb;font-size:14px;padding:10px 14px;outline:none;"
            "transition:border .2s,box-shadow .2s}"
            "input:focus,select:focus{border-color:#1d4ed8;box-shadow:0 0 0 3px rgba(29,78,216,.15)}"
            "input::placeholder{color:#374151}"
            "select option{background:#111827}"
            ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
            ".row4{display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:8px}"
            ".hint{color:#4b5563;font-size:11px;margin-top:4px}"
            ".btn{width:100%;background:#1d4ed8;color:#fff;border:none;border-radius:10px;"
            "padding:13px;font-size:14px;font-weight:600;cursor:pointer;margin-top:4px;"
            "letter-spacing:.5px;transition:background .2s,transform .1s;"
            "display:flex;align-items:center;justify-content:center;gap:8px}"
            ".btn:hover{background:#2563eb}.btn:active{transform:scale(.99)}"
            ".footer{text-align:center;padding:16px 32px;border-top:1px solid #1f2937}"
            ".footer p{color:#374151;font-size:11px;line-height:1.8}"
            ".footer strong{color:#4b5563}"
            "</style></head><body>"
            "<div class='card'>"
            "<div class='header'>"
            "<div class='header-title'><h1>DEVICE SETUP</h1></div>"
            "<div class='header-sub'>Attendance Machine</div>"
            "</div>"
            "<div class='body'><form method='POST' action='/save'>"
            "<div class='section'><div class='section-label'>WiFi Utama</div>"
            "<div class='row'>"
            "<div class='field'><label>SSID</label>"
            "<input name='ssid1' placeholder='Nama Jaringan' required></div>"
            "<div class='field'><label>Password</label>"
            "<input type='password' name='pass1' placeholder='&#9679;&#9679;&#9679;&#9679;&#9679;&#9679;'></div>"
            "</div></div>"
            "<div class='section'><div class='section-label'>WiFi Cadangan 1</div>"
            "<div class='row'>"
            "<div class='field'><label>SSID</label>"
            "<input name='ssid2' placeholder='Nama Jaringan'></div>"
            "<div class='field'><label>Password</label>"
            "<input type='password' name='pass2' placeholder='&#9679;&#9679;&#9679;&#9679;&#9679;&#9679;'></div>"
            "</div></div>"
            "<div class='section'><div class='section-label'>WiFi Cadangan 2</div>"
            "<div class='row'>"
            "<div class='field'><label>SSID</label>"
            "<input name='ssid3' placeholder='Nama Jaringan'></div>"
            "<div class='field'><label>Password</label>"
            "<input type='password' name='pass3' placeholder='&#9679;&#9679;&#9679;&#9679;&#9679;&#9679;'></div>"
            "</div></div>"
            "<div class='section'><div class='section-label'>Konfigurasi Perangkat</div>"
            "<div class='field'><label>API URL Backend</label>"
            "<input name='apiurl' placeholder='https://domain.sch.id' value='https://presensi.zedlabs.id' required></div>"
            "<div class='hint'>Tanpa trailing slash. Contoh: https://presensi.sekolah.sch.id</div>"
            "<div class='field' style='margin-top:10px'><label>API Key</label>"
            "<input name='apikey' placeholder='Masukkan API Key' required></div>"
            "<div class='field'><label>Nama Perangkat</label>"
            "<input name='devname' placeholder='Contoh: GERBANG UTAMA' maxlength='31'></div>"
            "</div>"
            "<div class='section'><div class='section-label'>Jadwal Sleep Mode</div>"
            "<div class='row'>"
            "<div class='field'><label>Mulai Sleep (jam)</label>"
            "<select name='slp_s'>");
  for (int h = 0; h <= 23; h++)
  {
    html += "<option value='";
    html += h;
    html += "'";
    if (h == SLEEP_START_HOUR_DEFAULT)
      html += " selected";
    html += ">";
    if (h < 10)
      html += "0";
    html += h;
    html += ":00</option>";
  }

  html += F("</select></div>"
            "<div class='field'><label>Selesai Sleep (jam)</label>"
            "<select name='slp_e'>");

  for (int h = 0; h <= 23; h++)
  {
    html += "<option value='";
    html += h;
    html += "'";
    if (h == SLEEP_END_HOUR_DEFAULT)
      html += " selected";
    html += ">";
    if (h < 10)
      html += "0";
    html += h;
    html += ":00</option>";
  }

  html += F("</select></div>"
            "</div>"
            "<p class='hint'>Perangkat akan deep sleep dari jam Mulai hingga jam Selesai.</p>"
            "</div>"
            "<div class='section'><div class='section-label'>Jadwal Dim OLED</div>"
            "<div class='row'>"
            "<div class='field'><label>Mulai Dim (jam)</label>"
            "<select name='dim_s'>");

  for (int h = 0; h <= 23; h++)
  {
    html += "<option value='";
    html += h;
    html += "'";
    if (h == OLED_DIM_START_HOUR_DEFAULT)
      html += " selected";
    html += ">";
    if (h < 10)
      html += "0";
    html += h;
    html += ":00</option>";
  }

  html += F("</select></div>"
            "<div class='field'><label>Selesai Dim (jam)</label>"
            "<select name='dim_e'>");

  for (int h = 0; h <= 23; h++)
  {
    html += "<option value='";
    html += h;
    html += "'";
    if (h == OLED_DIM_END_HOUR_DEFAULT)
      html += " selected";
    html += ">";
    if (h < 10)
      html += "0";
    html += h;
    html += ":00</option>";
  }

  html += F("</select></div>"
            "</div>"
            "<p class='hint'>OLED akan dimatikan sementara pada rentang jam tersebut.</p>"
            "</div>"

            "<button type='submit' class='btn'>Simpan &amp; Restart</button>"
            "</form></div>"
            "<div class='footer'>"
            "<p><strong>&copy; 2022 - <script>document.write(new Date().getFullYear())</script>"
            " ZEDLABS TEKNOLOGI INDONESIA</strong></p>"
            "<p>Attendance Machine <strong>v" FIRMWARE_VERSION "</strong></p>"
            "</div></div></body></html>");

  return html;
}

// GAP-02: password AP diturunkan dari MAC address per-device, menggantikan
// password statis yang sebelumnya sama untuk seluruh unit di lapangan.
static void deriveProvisioningPassword(char *out, size_t outSz)
{
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  // TODO: GAP-02 ASUMSI - format "ATN-XXXXXX" (6 hex dari 3 byte terakhir MAC).
  // Panjang 10 karakter memenuhi syarat minimum WPA2 (8 karakter) dan cukup ringkas
  // untuk dibaca dan diketik ulang oleh admin dari layar OLED 128x64.
  snprintf(out, outSz, "ATN-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void startProvisioningMode()
{
  Serial.println("[PROV] Entering provisioning mode...");
  deriveProvisioningPassword(provApPassword, sizeof(provApPassword)); // GAP-02
  showOLED(F("PROVISIONING"), PROV_AP_SSID);
  delay(1500);
  showOLED(F("AP PASSWORD"), provApPassword); // GAP-02: tampilkan password ke admin lapangan
  delay(2500);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(PROV_AP_SSID, provApPassword);
  Serial.printf("[PROV] AP started: %s, IP: %s\n", PROV_AP_SSID, WiFi.softAPIP().toString().c_str());
  Serial.printf("[PROV] AP password (per-device): %s\n", provApPassword);
  dnsServer.start(PROV_DNS_PORT, "*", WiFi.softAPIP());
  provServer.on("/", HTTP_GET, []()
                { provServer.send(200, "text/html", provHtmlPage()); });
  // GAP-14: keputusan sadar - halaman /save tetap dilayani via HTTP polos (bukan HTTPS).
  // Risiko diterima karena AP hanya aktif sesaat saat setup dan cakupan sinyal terbatas.
  provServer.on("/save", HTTP_POST, []()
                {
    String s1 = provServer.arg("ssid1"), p1 = provServer.arg("pass1");
    String s2 = provServer.arg("ssid2"), p2 = provServer.arg("pass2");
    String s3 = provServer.arg("ssid3"), p3 = provServer.arg("pass3");
    String ak = provServer.arg("apikey");
    String dn = provServer.arg("devname");
    String aurl = provServer.arg("apiurl");
    String slpS = provServer.arg("slp_s");
    String slpE = provServer.arg("slp_e");
    String dimS = provServer.arg("dim_s");
    String dimE = provServer.arg("dim_e");

    if (s1.length() == 0 || ak.length() == 0 || aurl.length() == 0) {
      provServer.send(400, "text/plain", "SSID 1, API Key, dan API URL wajib diisi.");
      return;
    }
    if (!aurl.startsWith("http://") && !aurl.startsWith("https://")) {
      provServer.send(400, "text/plain", "API URL harus diawali http:// atau https://");
      return;
    }
    while (aurl.endsWith("/")) aurl.remove(aurl.length() - 1);
    if (aurl.length() > 79) {
      provServer.send(400, "text/plain", "API URL terlalu panjang (max 79 karakter).");
      return;
    }
    if (dn.length() > DEVICE_NAME_MAX_LEN) {
      Serial.printf("[PROV] Device name too long (%d), truncating.\n", dn.length());
      dn = dn.substring(0, DEVICE_NAME_MAX_LEN);
    }

    auto parseHour = [](String s, int def) -> int {
      if (s.length() == 0) return def;
      int v = s.toInt();
      return (v >= 0 && v <= 23) ? v : def;
    };
    int iSlpS = parseHour(slpS, SLEEP_START_HOUR_DEFAULT);
    int iSlpE = parseHour(slpE, SLEEP_END_HOUR_DEFAULT);
    int iDimS = parseHour(dimS, OLED_DIM_START_HOUR_DEFAULT);
    int iDimE = parseHour(dimE, OLED_DIM_END_HOUR_DEFAULT);

    Serial.printf("[PROV] Saving: ssid1=%s apiurl=%s devname=%s\n", s1.c_str(), aurl.c_str(), dn.c_str());
    Serial.printf("[PROV] sleep=%d-%d dim=%d-%d\n", iSlpS, iSlpE, iDimS, iDimE);

    saveCredential(NVS_KEY_SSID1, s1.c_str());
    saveCredential(NVS_KEY_PASS1, p1.c_str());
    saveCredential(NVS_KEY_SSID2, s2.c_str());
    saveCredential(NVS_KEY_PASS2, p2.c_str());
    saveCredential(NVS_KEY_SSID3, s3.c_str());
    saveCredential(NVS_KEY_PASS3, p3.c_str());
    saveCredential(NVS_KEY_APIKEY, ak.c_str());
    saveCredential(NVS_KEY_DEVNAME, dn.c_str());
    saveCredential(NVS_KEY_APIURL, aurl.c_str());

    prefs.begin(NVS_NS_CONFIG, false);
    prefs.putInt(NVS_KEY_CFG_SLP_S, iSlpS);
    prefs.putInt(NVS_KEY_CFG_SLP_E, iSlpE);
    prefs.putInt(NVS_KEY_CFG_DIM_S, iDimS);
    prefs.putInt(NVS_KEY_CFG_DIM_E, iDimE);
    prefs.end();

    markProvisioned();
    Serial.println("[PROV] Credentials saved. Restarting...");
    provServer.send(200, "text/html",
                    "<html><body style='font-family:sans-serif;text-align:center;padding:40px'>"
                    "<h2>&#10003; Tersimpan!</h2><p>Device akan restart dalam 2 detik...</p>"
                    "</body></html>");
    delay(2000);
    ESP.restart(); });

  provServer.onNotFound([]()
                        { provServer.send(200, "text/html", provHtmlPage()); });
  provServer.begin();
  unsigned long t0 = millis();
  while (millis() - t0 < PROVISIONING_TIMEOUT_MS)
  {
    dnsServer.processNextRequest();
    provServer.handleClient();
    esp_task_wdt_reset();
    delay(10);
  }
  Serial.println("[PROV] Provisioning timeout. Restarting...");
  showOLED(F("TIMEOUT"), "RESTART...");
  delay(2000);
  ESP.restart();
}

void taskRfid(void *param)
{
  (void)param;
  esp_task_wdt_add(nullptr);
  Serial.println("[TASK] taskRfid started.");
  for (;;)
  {
    esp_task_wdt_reset();
    if (sleepRequested)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    RfidScanEvent ev;
    if (xQueueReceive(xRfidQueue, &ev, pdMS_TO_TICKS(10)) == pdTRUE)
    {
      char rfidBuf[11];
      uidToString(ev.uid, ev.uidLen, rfidBuf);
      if (strcmp(rfidBuf, lastUID) == 0 && millis() - timers.lastScan < DEBOUNCE_TIME)
      {
        Serial.printf("[RFID] Debounced: %s\n", rfidBuf);
        continue;
      }
      strcpy(lastUID, rfidBuf);
      timers.lastScan = millis();
      Serial.printf("[RFID] Card scanned: %s\n", rfidBuf);
      bool wasOff = !oledIsOn;
      if (wasOff)
        turnOnOLED();
      if (isAdminRfid(rfidBuf))
      {
        Serial.printf("[RFID] Admin card detected: %s\n", rfidBuf);
        handleAdminScan(rfidBuf);
      }
      else
      {
        showOLED(F("RFID"), rfidBuf);
        playToneNotify();
        char msg[32];
        bool ok = kirimPresensi(rfidBuf, msg);
        Serial.printf("[RFID] kirimPresensi result=%d msg=%s\n", ok, msg);
        showOLED(ok ? F("BERHASIL") : F("INFO"), msg);
        ok ? playToneSuccess() : playToneError();
      }
      rfidFeedback = {true, millis(), wasOff};
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void taskSync(void *param)
{
  (void)param;
  esp_task_wdt_add(nullptr);
  Serial.println("[TASK] taskSync started.");
  for (;;)
  {
    esp_task_wdt_reset();
    if (sleepRequested)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    unsigned long now = millis();
    RuntimeConfig cfg = getRuntimeConfigSnapshot(); // GAP-12
    processReconnect();
    checkSDHealth();
    if (isWifiConnected())
    {
      if (!isSignalWeak())
      {
        if (now - timers.lastOtaCheck >= cfg.otaCheckIntervalMs)
          checkOtaUpdate();
        checkAndUpdateRfidDb();
        if (otaState.updateAvailable && !rfidFeedback.active)
          performOtaUpdate();
        sendTelemetry();
        fetchRemoteConfig();
      }
      if (nvsGetCount() > 0 && now - timers.lastNvsSync >= cfg.syncIntervalMs)
      {
        timers.lastNvsSync = now;
        Serial.println("[TASK] Triggering NVS sync...");
        nvsSyncToServer();
      }
      if (sdCardAvailable)
      {
        if (syncState.inProgress)
          chunkedSync();
        else if (now - timers.lastSync >= cfg.syncIntervalMs)
        {
          refreshPendingCache();
          timers.lastSync = now;
          if (cachedPendingRecords > 0)
          {
            Serial.printf("[TASK] Triggering SD sync: %d pending\n", cachedPendingRecords);
            chunkedSync();
          }
        }
      }
    }
    periodicTimeSync();
    vTaskDelay(pdMS_TO_TICKS(PERIODIC_CHECK_INTERVAL));
  }
}

void taskDisplay(void *param)
{
  (void)param;
  esp_task_wdt_add(nullptr);
  Serial.println("[TASK] taskDisplay started.");
  for (;;)
  {
    esp_task_wdt_reset();
    if (sleepRequested)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    unsigned long now = millis();
    if (rfidFeedback.active && now - rfidFeedback.shownAt >= RFID_FEEDBACK_DISPLAY_MS)
    {
      rfidFeedback.active = false;
      if (rfidFeedback.wasOledOff)
        checkOLEDSchedule();
      memset(previousDisplay.time, 0xFF, sizeof(previousDisplay.time));
      previousDisplay.pendingRecords = -1;
      previousDisplay.isOnline = !currentDisplay.isOnline;
    }
    checkOLEDSchedule();
    if (now - timers.lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL)
    {
      timers.lastDisplayUpdate = now;
      updateCurrentDisplayState();
      updateStandbyDisplay();
    }
    checkFactoryReset();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== BOOT ===");
  Serial.printf("[SETUP] Firmware: v%s\n", FIRMWARE_VERSION);

  esp_task_wdt_deinit();
  const esp_task_wdt_config_t wdtCfg = {
      .timeout_ms = WDT_NORMAL_TIMEOUT_MS,
      .idle_core_mask = 0,
      .trigger_panic = true};
  esp_task_wdt_init(&wdtCfg);
  esp_task_wdt_add(nullptr);
  Serial.println("[SETUP] WDT initialized (60s).");

  esp_ota_mark_app_valid_cancel_rollback();
  Serial.println("[SETUP] OTA rollback cancelled.");

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  Serial.println("[SETUP] OLED initialized.");
  showStartupAnimation();
  playStartupMelody();
  esp_task_wdt_reset();

  xSdMutex = xSemaphoreCreateMutex();
  xDisplayMutex = xSemaphoreCreateMutex();
  xConfigMutex = xSemaphoreCreateMutex(); // GAP-12
  xRfidQueue = xQueueCreate(RFID_QUEUE_LEN, sizeof(RfidScanEvent));
  Serial.println("[SETUP] FreeRTOS objects created.");

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(deviceId, sizeof(deviceId), "ESP32_%02X%02X", mac[4], mac[5]);
  for (int i = 0; deviceId[i]; i++)
    deviceId[i] = toupper(deviceId[i]);
  Serial.printf("[SETUP] Device ID: %s\n", deviceId);

  if (timeWasSynced && lastValidTime > 0 && sleepDurationSeconds > 0)
  {
    lastValidTime += (time_t)sleepDurationSeconds;
    nvsSaveLastTime(lastValidTime);
    bootTime = millis();
    bootTimeSet = true;
    sleepDurationSeconds = 0;
    Serial.printf("[SETUP] Adjusted time after sleep: %lu\n", (unsigned long)lastValidTime);
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
      Serial.printf("[SETUP] Restored last valid time from NVS: %lu\n", (unsigned long)lastValidTime);
    }
    else
    {
      Serial.println("[SETUP] No saved time in NVS.");
    }
  }

  isProvisioned = checkProvisioned();
  if (!isProvisioned)
  {
    Serial.println("[SETUP] Not provisioned, entering provisioning mode.");
    showOLED(F("BELUM DIKONFIGURASI"), "MASUK SETUP MODE");
    delay(2000);
    startProvisioningMode();
    return;
  }

  loadCredentials();
  Serial.printf("API Key: '%s'\n", apiKey);
  Serial.printf("API URL: '%s'\n", apiBaseUrl);
  Serial.printf("WiFi SSID: '%s'\n", wifiCreds[0].ssid);
  Serial.printf("Device Name: '%s'\n", deviceName);
  Serial.printf("[DEBUG] NVS count at boot: %d\n", nvsGetCount());

  if (strlen(apiKey) == 0 || strlen(wifiCreds[0].ssid) == 0)
  {
    Serial.println("[SETUP] Config incomplete, entering provisioning mode.");
    showOLED(F("CONFIG ERROR"), "MASUK SETUP MODE");
    delay(2000);
    startProvisioningMode();
    return;
  }

  if (strlen(deviceName) > 0)
  {
    if (strlen(deviceName) > DEVICE_NAME_MAX_LEN)
    {
      Serial.printf("[SETUP] WARNING: deviceName too long (%d chars), truncating.\n", strlen(deviceName));
      deviceName[DEVICE_NAME_MAX_LEN] = '\0';
    }
    snprintf(deviceId, sizeof(deviceId), "%s", deviceName);
    Serial.printf("[SETUP] Device name override: %s\n", deviceId);
  }

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
  Serial.println("[SETUP] SPI initialized.");

  showProgress(F("INIT SD CARD"), 1500);
  sdCardAvailable = initSDCard();
  if (sdCardAvailable)
  {
    Serial.println("[SETUP] SD card OK.");
    showOLED(F("SD CARD"), "TERSEDIA");
    playToneSuccess();
    delay(800);
    refreshPendingCache();
    if (cachedPendingRecords > 0)
    {
      Serial.printf("[SETUP] Pending SD records: %d\n", cachedPendingRecords);
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
    loadAdminRfidList();
  }
  else
  {
    Serial.println("[SETUP] SD card NOT available.");
    showOLED(F("SD CARD"), "TIDAK ADA");
    playToneError();
    delay(1000);
    int nc = nvsGetCount();
    if (nc > 0)
    {
      Serial.printf("[SETUP] NVS buffer has %d records.\n", nc);
      char buf[20];
      snprintf(buf, sizeof(buf), "%d TERSISA", nc);
      showOLED(F("NVS BUFFER"), buf);
      delay(1000);
    }
  }

  if (sdCardAvailable)
  {
    if (acquireSD())
    {
      selectSD();
      char fn[20];
      for (int i = 0; i < 100; i++)
      {
        getQueueFileName(i, fn, sizeof(fn));
        if (sd.exists(fn))
        {
          Serial.printf("[DEBUG] Found queue file: %s\n", fn);
        }
      }
      deselectSD();
      releaseSD();
    }
  }

  showProgress(F("CONNECTING WIFI"), 1500);
  bool wifiOk = connectToWiFi();
  esp_task_wdt_reset();

  if (!wifiOk)
  {
    Serial.println("[SETUP] WiFi FAILED, going offline.");
    showOLED(F("NO WIFI"), "OFFLINE MODE");
    playToneError();
    delay(1500);
  }
  else
  {
    Serial.println("[SETUP] WiFi connected, syncing time...");
    showOLED(F("SYNCING TIME"), "MOHON TUNGGU...");
    syncTimeWithFallback();
    esp_task_wdt_reset();
    showProgress(F("PING API"), 500);
    int apiRetry = 0;
    while (!pingAPI() && apiRetry < 3)
    {
      apiRetry++;
      Serial.printf("[SETUP] API ping failed, retry %d/3\n", apiRetry);
      char buf[12];
      snprintf(buf, sizeof(buf), "Retry %d/3", apiRetry);
      showOLED(F("API GAGAL"), buf);
      playToneError();
      delay(800);
      esp_task_wdt_reset();
    }
    if (isOnline && !isSignalCritical())
    {
      Serial.println("[SETUP] API online.");
      showOLED(F("API OK"), "ONLINE");
      playToneSuccess();
      delay(500);
      esp_task_wdt_reset();
      int nc = nvsGetCount();
      if (nc > 0)
      {
        Serial.printf("[SETUP] Syncing %d NVS records...\n", nc);
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
          Serial.printf("[SETUP] Syncing %d SD records on boot...\n", cachedPendingRecords);
          char buf[20];
          snprintf(buf, sizeof(buf), "%d records", cachedPendingRecords);
          showOLED(F("SYNC DATA"), buf);
          delay(1000);
          chunkedSync();
          esp_task_wdt_reset();
        }
        showProgress(F("SYNC RFID DB"), 500);
        unsigned long lv = nvsGetRfidDbVer(), sv = checkRfidDbVersion();
        Serial.printf("[SETUP] RFID DB: local=%lu server=%lu\n", lv, sv);
        if (sv > lv)
          downloadRfidDb();
        else
        {
          Serial.println("[SETUP] RFID DB up to date.");
          showOLED(F("RFID DB"), "UP TO DATE");
        }
        delay(600);
        esp_task_wdt_reset();
      }
    }
    else
    {
      Serial.println("[SETUP] API unreachable, going offline.");
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
  Serial.printf("[SETUP] RC522 version register: 0x%02X\n", ver);
  if (ver == 0x00 || ver == 0xFF)
  {
    Serial.println("[SETUP] RC522 not detected! Restarting...");
    showOLED(F("RC522 GAGAL"), "RESTART...");
    playToneError();
    delay(3000);
    ESP.restart();
  }
  Serial.println("[SETUP] RC522 OK.");

  showOLED(F("SISTEM SIAP"), isOnline ? "ONLINE" : "OFFLINE");
  playToneSuccess();
  Serial.printf("[SETUP] System ready. Online=%d\n", isOnline);

  if (!bootTimeSet)
  {
    bootTime = millis();
    bootTimeSet = true;
  }
  unsigned long now = millis();
  timers.lastSync = now;
  timers.lastTimeSync = now;
  timers.lastReconnect = isOnline ? now : 0;
  timers.lastDisplayUpdate = now;
  timers.lastPeriodicCheck = now;
  timers.lastOLEDScheduleCheck = now;
  timers.lastSDRedetect = now;
  timers.lastNvsSync = now;
  timers.lastOtaCheck = 0;
  timers.lastRfidDbCheck = now;
  timers.lastTelemetry = now;
  timers.lastRemoteConfig = now;
  // timers.lastFactoryCheck = now;
  delay(1000);
  checkOLEDSchedule();

  hTaskLoop = xTaskGetCurrentTaskHandle();
  Serial.println("[SETUP] Starting FreeRTOS tasks...");
  xTaskCreatePinnedToCore(taskRfid, "rfid", TASK_RFID_STACK, nullptr, TASK_RFID_PRIORITY, &hTaskRfid, 0);
  xTaskCreatePinnedToCore(taskSync, "sync", TASK_SYNC_STACK, nullptr, TASK_SYNC_PRIORITY, &hTaskSync, 0);
  xTaskCreatePinnedToCore(taskDisplay, "disp", TASK_DISPLAY_STACK, nullptr, TASK_DISPLAY_PRIORITY, &hTaskDisplay, 0);
  Serial.println("[SETUP] All tasks started. Entering loop.");
}

void loop()
{
  esp_task_wdt_reset();

  if (rfidReader.PICC_IsNewCardPresent() && rfidReader.PICC_ReadCardSerial())
  {
    Serial.printf("[LOOP] New card detected, UID len=%d\n", rfidReader.uid.size);
    RfidScanEvent ev;
    memcpy(ev.uid, rfidReader.uid.uidByte, rfidReader.uid.size);
    ev.uidLen = rfidReader.uid.size;
    xQueueSend(xRfidQueue, &ev, 0);
    rfidReader.PICC_HaltA();
    rfidReader.PCD_StopCrypto1();
  }

  struct tm ti;
  if (getTimeWithFallback(&ti))
  {
    RuntimeConfig cfg = getRuntimeConfigSnapshot(); // GAP-12
    int h = ti.tm_hour;
    if (h >= cfg.sleepStartHour || h < cfg.sleepEndHour)
    {
      if (syncState.inProgress)
        return;

      Serial.printf("[SLEEP] Entering deep sleep at %02d:%02d\n", ti.tm_hour, ti.tm_min);

      sleepRequested = true;
      Serial.println("[SLEEP] Signaling tasks to idle...");
      unsigned long waitStart = millis();
      while (millis() - waitStart < DEEP_SLEEP_TASK_WAIT_MS)
      {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      Serial.println("[SLEEP] Task wait done, flushing files...");

      flushAllFiles();
      showOLED(F("SLEEP MODE"), "...");
      delay(1000);

      int nowSec = ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec;
      int endSec = (h >= cfg.sleepStartHour)
                       ? cfg.sleepEndHour * 3600 + 86400
                       : cfg.sleepEndHour * 3600;
      int sleepSec = endSec - nowSec;
      if (sleepSec < 60)
        sleepSec = 60;
      if (sleepSec > 43200)
        sleepSec = 43200;

      Serial.printf("[SLEEP] Sleep duration: %d seconds (%dj %dm)\n",
                    sleepSec, sleepSec / 3600, (sleepSec % 3600) / 60);
      char buf[24];
      snprintf(buf, sizeof(buf), "%dj %dm", sleepSec / 3600, (sleepSec % 3600) / 60);
      showOLED(F("SLEEP FOR"), buf);
      delay(2000);

      if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(500)) == pdTRUE)
      {
        display.clearDisplay();
        display.display();
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        xSemaphoreGive(xDisplayMutex);
      }

      sleepDurationSeconds = (uint64_t)sleepSec;
      restoreWdtNormal();

      if (hTaskLoop)
        esp_task_wdt_delete(hTaskLoop);
      if (hTaskRfid)
        esp_task_wdt_delete(hTaskRfid);
      if (hTaskSync)
        esp_task_wdt_delete(hTaskSync);
      if (hTaskDisplay)
        esp_task_wdt_delete(hTaskDisplay);
      esp_task_wdt_deinit();

      esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
      Serial.println("[SLEEP] Going to deep sleep now.");
      esp_deep_sleep_start();
    }
  }

  vTaskDelay(pdMS_TO_TICKS(10));
}
