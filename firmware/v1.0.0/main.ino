/*
 * ======================================================================================
 * IDENTITAS PROYEK: SISTEM PRESENSI PINTAR (RFID)
 * ======================================================================================
 * Perangkat    : ESP32-C3 Super Mini
 * Penulis      : Yahya Zulfikri
 * Dibuat       : Juli 2025
 * Diperbarui   : Desember 2025
 * Versi        : 1.0.0 (Stabil)
 * 
 * PEMETAAN PERANGKAT KERAS (ESP32-C3 Super Mini):
 * ---------------------------------------
 * [RFID RC522]          [OLED SSD1306]         [BUZZER]
 * - SDA (SS) : GPIO 7   - SDA : GPIO 8         - (+) : GPIO 10
 * - SCK      : GPIO 4   - SCL : GPIO 9         - (-) : GND
 * - MOSI     : GPIO 6   - VCC : 3.3V
 * - MISO     : GPIO 5   - GND : GND
 * - RST      : GPIO 3
 * 
 * FITUR:
 * - Sinkronisasi Waktu Otomatis (NTP Multi-Server)
 * - Mode Tidur Dalam (Otomatis ON/OFF berdasarkan jadwal)
 * - Koneksi API HTTPS dengan Keamanan Kunci API
 * - Animasi OLED & Indikator Kekuatan Sinyal
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

// ========================================
// DEFINISI PIN
// ========================================
// RFID RC522
#define PIN_RFID_SS         7
#define PIN_RFID_SCK        4
#define PIN_RFID_MOSI       6
#define PIN_RFID_MISO       5
#define PIN_RFID_RST        3

// OLED SSD1306
#define PIN_OLED_SDA        8
#define PIN_OLED_SCL        9

// BUZZER
#define PIN_BUZZER          10

// Pengaturan Tampilan
#define SCREEN_WIDTH        128
#define SCREEN_HEIGHT       64
#define DEBOUNCE_TIME       300

// ========================================
// KONFIGURASI JARINGAN
// ========================================
const char WIFI_SSID_1[] PROGMEM        = "SSID_WIFI_1";
const char WIFI_SSID_2[] PROGMEM        = "SSID_WIFI_2";
const char WIFI_PASSWORD_1[] PROGMEM    = "PasswordWifi1";
const char WIFI_PASSWORD_2[] PROGMEM    = "PasswordWifi1";

const char API_BASE_URL[] PROGMEM       = "https://zedlabs.id";
const char API_SECRET_KEY[] PROGMEM     = "SecretAPIToken";

// Server NTP
const char NTP_SERVER_1[] PROGMEM       = "pool.ntp.org";
const char NTP_SERVER_2[] PROGMEM       = "time.google.com";
const char NTP_SERVER_3[] PROGMEM       = "id.pool.ntp.org";
const char NTP_SERVER_4[] PROGMEM       = "time.nist.gov";
const char NTP_SERVER_5[] PROGMEM       = "time.cloudflare.com";

// ========================================
// KONSTANTA SISTEM
// ========================================
const int WIFI_RETRY_COUNT          = 2;
const int NTP_SERVER_COUNT          = 5;
const int NTP_TIMEOUT_MS            = 8000;
const int NTP_MAX_RETRIES           = 2;

// Jadwal Mode Tidur (Format 24-jam)
const int SLEEP_START_HOUR          = 18;  // 18:00 (6 Malam)
const int SLEEP_END_HOUR            = 5;   // 05:00 (5 Pagi)

// Pengaturan Waktu
const long GMT_OFFSET_SEC           = 25200;  // GMT+7 (WIB)
const int DAYLIGHT_OFFSET_SEC       = 0;

// ========================================
// VARIABEL MEMORI RTC
// ========================================
RTC_DATA_ATTR time_t lastValidTime = 0;
RTC_DATA_ATTR bool timeWasSynced = false;
RTC_DATA_ATTR unsigned long bootTime = 0;

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
char messageBuffer[64];

// ========================================
// FUNGSI MANAJEMEN WAKTU
// ========================================

/**
 * Sinkronisasi waktu dengan server NTP menggunakan mekanisme cadangan
 * Mencoba beberapa server NTP dengan percobaan ulang
 */
bool syncTimeWithFallback() {
  for (int serverIndex = 0; serverIndex < NTP_SERVER_COUNT; serverIndex++) {
    char ntpServer[32];
    
    // Pilih server NTP
    switch (serverIndex) {
      case 0: strcpy_P(ntpServer, NTP_SERVER_1); break;
      case 1: strcpy_P(ntpServer, NTP_SERVER_2); break;
      case 2: strcpy_P(ntpServer, NTP_SERVER_3); break;
      case 3: strcpy_P(ntpServer, NTP_SERVER_4); break;
      case 4: strcpy_P(ntpServer, NTP_SERVER_5); break;
    }
    
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, ntpServer);
    
    // Mekanisme percobaan ulang untuk setiap server
    for (int retry = 0; retry < NTP_MAX_RETRIES; retry++) {
      snprintf_P(messageBuffer, sizeof(messageBuffer), PSTR("Server %d/%d"), 
                 serverIndex + 1, NTP_SERVER_COUNT);
      showOLED(F("SYNC WAKTU"), messageBuffer);
      
      struct tm timeInfo;
      unsigned long startTime = millis();
      bool timeReceived = false;
      
      // Tunggu sinkronisasi waktu dengan batas waktu
      while ((millis() - startTime) < NTP_TIMEOUT_MS) {
        if (getLocalTime(&timeInfo)) {
          if (timeInfo.tm_year >= 120) {  // Tahun 2020 atau lebih baru
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

/**
 * Dapatkan waktu saat ini dengan cadangan ke waktu estimasi jika NTP gagal
 */
bool getTimeWithFallback(struct tm *timeInfo) {
  // Coba dapatkan waktu dari NTP
  if (getLocalTime(timeInfo)) {
    if (timeInfo->tm_year >= 120) {
      return true;
    }
  }
  
  // Cadangan ke estimasi waktu berdasarkan sinkronisasi terakhir
  if (timeWasSynced && lastValidTime > 0) {
    unsigned long elapsedSeconds = (millis() - bootTime) / 1000;
    time_t estimatedTime = lastValidTime + elapsedSeconds;
    *timeInfo = *localtime(&estimatedTime);
    return true;
  }
  
  return false;
}

/**
 * Sinkronisasi waktu secara berkala setiap jam
 */
inline void periodicTimeSync() {
  static unsigned long lastSyncAttempt = 0;
  const unsigned long SYNC_INTERVAL_MS = 3600000UL;  // 1 jam
  
  if (millis() - lastSyncAttempt > SYNC_INTERVAL_MS) {
    lastSyncAttempt = millis();
    if (WiFi.status() == WL_CONNECTED) {
      syncTimeWithFallback();
    }
  }
}

// ========================================
// FUNGSI JARINGAN
// ========================================

/**
 * Hubungkan ke WiFi dengan cadangan multi-SSID
 */
bool connectToWiFi() {
  WiFi.mode(WIFI_STA);
  
  for (int attempt = 0; attempt < WIFI_RETRY_COUNT; attempt++) {
    char ssid[16], password[16];
    
    // Pilih kredensial WiFi
    if (attempt == 0) {
      strcpy_P(ssid, WIFI_SSID_1);
      strcpy_P(password, WIFI_PASSWORD_1);
    } else {
      strcpy_P(ssid, WIFI_SSID_2);
      strcpy_P(password, WIFI_PASSWORD_2);
    }
    
    WiFi.begin(ssid, password);
    
    // Tunggu koneksi dengan tampilan animasi
    for (int retry = 0; retry < 20 && WiFi.status() != WL_CONNECTED; retry++) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      
      // Tampilkan SSID
      display.setCursor((SCREEN_WIDTH - strlen(ssid) * 6) / 2, 10);
      display.println(ssid);
      
      // Pesan koneksi animasi
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
      delay(2000);
      return true;
    }
  }
  
  return false;
}

/**
 * Ping endpoint API untuk memeriksa konektivitas
 */
bool pingAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  HTTPClient http;
  http.setTimeout(10000);
  
  // Buat URL
  char url[80];
  strcpy_P(url, API_BASE_URL);
  strcat_P(url, PSTR("/api/presensi/ping"));
  
  http.begin(url);
  
  // Tambahkan header kunci API
  char apiKey[32];
  strcpy_P(apiKey, API_SECRET_KEY);
  http.addHeader(F("X-API-KEY"), apiKey);
  
  int responseCode = http.GET();
  http.end();
  
  return responseCode == 200;
}

// ========================================
// FUNGSI RFID
// ========================================

/**
 * Konversi UID RFID ke format string
 */
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

/**
 * Kirim data kehadiran ke server
 */
bool kirimPresensi(const char *rfidUID, char *message, char *name, 
                   char *status, char *time) {
  if (WiFi.status() != WL_CONNECTED) {
    strcpy_P(message, PSTR("TIDAK ADA WIFI"));
    return false;
  }
  
  HTTPClient http;
  http.setTimeout(30000);
  
  // Buat URL
  char url[80];
  strcpy_P(url, API_BASE_URL);
  strcat_P(url, PSTR("/api/presensi/rfid"));
  
  http.begin(url);
  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("Accept"), F("application/json"));
  
  // Tambahkan kunci API
  char apiKey[32];
  strcpy_P(apiKey, API_SECRET_KEY);
  http.addHeader(F("X-API-KEY"), apiKey);
  
  // Buat payload JSON
  char payload[64];
  snprintf_P(payload, sizeof(payload), PSTR("{\"rfid\":\"%s\"}"), rfidUID);
  
  int responseCode = http.POST(payload);
  String response = http.getString();
  http.end();
  
  // Urai respons JSON
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, response);
  
  if (error) {
    strcpy_P(message, PSTR("ULANGI !"));
    return false;
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
    strncpy(time, w, 15);
    time[15] = '\0';
    strncpy(status, s, 15);
    status[15] = '\0';
    
    return true;
  }
  
  return false;
}

// ========================================
// FUNGSI TAMPILAN
// ========================================

/**
 * Tampilkan dua baris teks di tengah pada OLED
 */
void showOLED(const __FlashStringHelper *line1, const char *line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  int16_t x1, y1;
  uint16_t w1, h1;
  
  // Tampilkan baris pertama
  display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 10);
  display.println(line1);
  
  // Tampilkan baris kedua
  display.getTextBounds(line2, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 30);
  display.println(line2);
  
  display.display();
}

/**
 * Tampilkan dua baris teks (keduanya dari memori flash)
 */
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

/**
 * Tampilkan layar siaga dengan waktu dan sinyal WiFi
 */
void showStandbySignal() {
  display.clearDisplay();
  
  // Judul
  const __FlashStringHelper *title = F("TEMPELKAN KARTU");
  int16_t x1, y1;
  uint16_t w1, h1;
  
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.getTextBounds(title, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 20);
  display.println(title);
  
  // Waktu saat ini
  struct tm timeInfo;
  if (getTimeWithFallback(&timeInfo)) {
    snprintf(messageBuffer, sizeof(messageBuffer), "%02d:%02d", 
             timeInfo.tm_hour, timeInfo.tm_min);
    display.getTextBounds(messageBuffer, 0, 0, &x1, &y1, &w1, &h1);
    display.setCursor((SCREEN_WIDTH - w1) / 2, 35);
    display.println(messageBuffer);
  }
  
  // Indikator kekuatan sinyal WiFi
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

/**
 * Tampilkan animasi bilah progres
 */
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

/**
 * Tampilkan animasi startup dengan logo
 */
void showStartupAnimation() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  
  const char title[] PROGMEM = "ZEDLABS";
  const char subtitle1[] PROGMEM = "INNOVATE BEYOND";
  const char subtitle2[] PROGMEM = "LIMITS";
  const char loading[] PROGMEM = "STARTING v1.0.0";
  
  const int titleLength = 7;
  const int titleX = (SCREEN_WIDTH - (titleLength * 12)) / 2;
  const int sub1X = (SCREEN_WIDTH - 15 * 6) / 2;
  const int sub2X = (SCREEN_WIDTH - 6 * 6) / 2;
  const int loadX = (SCREEN_WIDTH - 15 * 6) / 2;
  
  // Animasi geser masuk
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
  
  // Teks memuat
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
  showOLED(errorMessage, F("RESTART..."));
  playToneError();
  delay(3000);
  ESP.restart();
}

// ========================================
// FUNGSI UTAMA
// ========================================

void setup() {
  // Inisialisasi I2C untuk OLED
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  pinMode(PIN_BUZZER, OUTPUT);
  
  // Inisialisasi tampilan
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  showStartupAnimation();
  playStartupMelody();
  
  // Hubungkan ke WiFi
  if (!connectToWiFi()) {
    fatalError(F("WIFI GAGAL"));
  }
  
  // Uji koneksi API
  showProgress(F("PING API"), 2000);
  int apiRetryCount = 0;
  while (!pingAPI()) {
    apiRetryCount++;
    snprintf_P(messageBuffer, sizeof(messageBuffer), PSTR("PERCOBAAN %d"), apiRetryCount);
    showOLED(F("API GAGAL"), messageBuffer);
    playToneError();
    delay(2000);
    
    if (apiRetryCount >= 5) {
      fatalError(F("API TIDAK TERSEDIA"));
    }
  }
  
  showOLED(F("API TERHUBUNG"), F("SINKRONISASI WAKTU"));
  playToneSuccess();
  
  // Sinkronisasi waktu
  if (!syncTimeWithFallback()) {
    struct tm timeInfo;
    if (!getTimeWithFallback(&timeInfo)) {
      showOLED(F("PERINGATAN"), F("WAKTU TIDAK TERSEDIA"));
      playToneError();
      delay(3000);
      showOLED(F("MODE MANUAL"), F("TANPA AUTO SLEEP"));
      delay(2000);
    } else {
      showOLED(F("WAKTU ESTIMASI"), F("AKURASI TERBATAS"));
      playToneError();
      delay(2000);
    }
  }
  
  // Inisialisasi SPI dan RFID
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
  rfidReader.PCD_Init();
  delay(100);
  
  // Verifikasi modul RFID
  byte version = rfidReader.PCD_ReadRegister(rfidReader.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    fatalError(F("RC522 TIDAK TERDETEKSI"));
  }
  
  showOLED(F("SISTEM SIAP"), F("TEMPELKAN KARTU"));
  playToneSuccess();
  bootTime = millis();
}

void loop() {
  // Sinkronisasi waktu berkala
  periodicTimeSync();
  
  // Periksa jadwal tidur
  struct tm timeInfo;
  if (getTimeWithFallback(&timeInfo)) {
    int currentHour = timeInfo.tm_hour;
    
    if (currentHour >= SLEEP_START_HOUR || currentHour < SLEEP_END_HOUR) {
      showOLED(F("SLEEP MODE"), F("SAMPAI PAGI"));
      delay(2000);
      
      // Hitung durasi tidur
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
  
  // Periksa kartu RFID
  if (rfidReader.PICC_IsNewCardPresent() && rfidReader.PICC_ReadCardSerial()) {
    uidToString(rfidReader.uid.uidByte, rfidReader.uid.size, messageBuffer);
    
    // Pemeriksaan debounce
    if (strcmp(messageBuffer, lastUID) == 0 && 
        millis() - lastScanTime < DEBOUNCE_TIME) {
      rfidReader.PICC_HaltA();
      rfidReader.PCD_StopCrypto1();
      return;
    }
    
    strcpy(lastUID, messageBuffer);
    lastScanTime = millis();
    
    showOLED(F("RFID TERDETEKSI"), messageBuffer);
    playToneNotify();
    delay(50);
    
    // Kirim data kehadiran
    char message[32], name[32], status[16], time[16];
    bool success = kirimPresensi(messageBuffer, message, name, status, time);
    
    showOLED(success ? F("BERHASIL") : F("INFO"), message);
    
    if (success) {
      playToneSuccess();
    } else {
      playToneError();
    }
    
    delay(150);
    
    rfidReader.PICC_HaltA();
    rfidReader.PCD_StopCrypto1();
  }
  
  showStandbySignal();
  delay(80);
}