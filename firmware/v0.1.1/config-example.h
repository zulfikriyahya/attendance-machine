#ifndef CONFIG_H
#define CONFIG_H
#define RST_PIN 3
#define SS_PIN 7
#define SD_CS 1
#define SDA_PIN 8
#define SCL_PIN 9
#define BUZZER_PIN 10
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DEBOUNCE_TIME 300
#define SCK 4
#define MISO 5
#define MOSI 6
const char *WIFI_SSIDS[] = {"ZEDLABS", "ZULFIKRI YAHYA"};
const char *WIFI_PASSWORDS[] = {"password123", "password123#"};
const int WIFI_COUNT = sizeof(WIFI_SSIDS) / sizeof(WIFI_SSIDS[0]);
const String API_BASE_URL = "https://example.com/api";
const String API_SECRET = "YourSecretApiToken";
// Multiple NTP servers untuk fallback
const char *ntpServers[] = {
    "id.pool.ntp.org",    // Server Indonesia (prioritas)
    "time.google.com",    // Google time server
    "pool.ntp.org",       // Pool NTP
    "time.nist.gov",      // NIST time server
    "time.cloudflare.com" // Cloudflare time server
};
const int NTP_SERVER_COUNT = 5;
const int NTP_TIMEOUT = 8000;  // 8 detik timeout per server
const int MAX_NTP_RETRIES = 2; // 2 retry per server
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;
#endif