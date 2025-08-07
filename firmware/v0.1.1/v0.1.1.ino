#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>
#define RST_PIN 3
#define SS_PIN 7
#define SDA_PIN 8
#define SCL_PIN 9
#define BUZZER_PIN 10
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DEBOUNCE_TIME 300
#define SCK 4
#define MISO 5
#define MOSI 6
const char *WIFI_SSIDS[] = { "ssid1", "ssid2" };
const char *WIFI_PASSWORDS[] = { "pass1", "pass2" };
const int WIFI_COUNT = sizeof(WIFI_SSIDS) / sizeof(WIFI_SSIDS[0]);
const String API_BASE_URL = "https://example.com/api";
const String API_SECRET = "SecretTokenApi";
const char *ntpServers[] = {
  "pool.ntp.org",
  "time.google.com",
  "id.pool.ntp.org",
  "time.nist.gov",
  "time.cloudflare.com"
};
const int NTP_SERVER_COUNT = 5;
const int NTP_TIMEOUT = 8000;
const int MAX_NTP_RETRIES = 2;
const int START_SLEEP = 18;
const int END_SLEEP = 1;
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;
// RTC backup variables - tersimpan saat deep sleep
RTC_DATA_ATTR time_t lastValidTime = 0;
RTC_DATA_ATTR bool timeWasSet = false;
RTC_DATA_ATTR unsigned long bootTime = 0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 rfid(SS_PIN, RST_PIN);
String lastUid = "";
unsigned long lastScanTime = 0;

// Fungsi untuk sync waktu dengan multiple fallback
bool syncTimeWithFallback() {
  Serial.println("=== MEMULAI SINKRONISASI WAKTU ===");

  for (int serverIdx = 0; serverIdx < NTP_SERVER_COUNT; serverIdx++) {
    Serial.printf("Mencoba server [%d/%d]: %s\n", serverIdx + 1, NTP_SERVER_COUNT, ntpServers[serverIdx]);

    // Set konfigurasi NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServers[serverIdx]);

    // Retry untuk server saat ini
    for (int retry = 0; retry < MAX_NTP_RETRIES; retry++) {
      Serial.printf("  Percobaan %d/%d...\n", retry + 1, MAX_NTP_RETRIES);

      // Update display
      showOLED("SYNC WAKTU", "Server " + String(serverIdx + 1) + "/" + String(NTP_SERVER_COUNT));

      struct tm timeinfo;
      unsigned long startTime = millis();
      bool timeReceived = false;

      // Tunggu dengan timeout
      while ((millis() - startTime) < NTP_TIMEOUT) {
        if (getLocalTime(&timeinfo)) {
          // Validasi waktu - tahun harus > 2020
          if (timeinfo.tm_year >= 120) {
            timeReceived = true;
            break;
          }
        }
        delay(100);

        // Update progress setiap detik
        if ((millis() - startTime) % 1000 == 0) {
          int progress = ((millis() - startTime) * 100) / NTP_TIMEOUT;
          Serial.printf("    Progress: %d%%\n", progress);
        }
      }

      if (timeReceived) {
        // Sukses mendapatkan waktu valid
        lastValidTime = mktime(&timeinfo);
        timeWasSet = true;
        bootTime = millis();

        Serial.printf("✓ WAKTU BERHASIL DISYNC!\n");
        Serial.printf("  Tanggal: %04d-%02d-%02d\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        Serial.printf("  Waktu: %02d:%02d:%02d\n",
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        Serial.printf("  Server: %s\n", ntpServers[serverIdx]);

        showOLED("WAKTU TERSYNC", String(timeinfo.tm_hour) + ":" + (timeinfo.tm_min < 10 ? "0" : "") + String(timeinfo.tm_min));
        delay(1500);
        return true;
      }

      Serial.printf("  ✗ Timeout setelah %d detik\n", NTP_TIMEOUT / 1000);
      delay(500);  // Jeda sebelum retry
    }

    Serial.printf("✗ Server %s gagal setelah %d percobaan\n",
                  ntpServers[serverIdx], MAX_NTP_RETRIES);
  }

  Serial.println("✗ SEMUA SERVER NTP GAGAL!");
  return false;
}

// Fungsi untuk mendapatkan waktu dengan fallback ke estimasi
bool getTimeWithFallback(struct tm* timeinfo) {
  // Coba ambil waktu dari sistem dulu
  if (getLocalTime(timeinfo)) {
    if (timeinfo->tm_year >= 120) {  // Validasi tahun > 2020
      return true;
    }
  }

  // Jika gagal dan ada waktu backup, gunakan estimasi
  if (timeWasSet && lastValidTime > 0) {
    Serial.println("Menggunakan estimasi waktu dari backup RTC");
    unsigned long elapsedSeconds = (millis() - bootTime) / 1000;
    time_t estimatedTime = lastValidTime + elapsedSeconds;
    *timeinfo = *localtime(&estimatedTime);

    Serial.printf("Estimasi waktu: %02d:%02d:%02d\n",
                  timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return true;
  }

  Serial.println("Tidak ada waktu backup tersedia");
  return false;
}

// Periodic time sync - panggil setiap 1 jam
void periodicTimeSync() {
  static unsigned long lastSyncAttempt = 0;
  const unsigned long SYNC_INTERVAL = 3600000;  // 1 jam = 3600000 ms

  if (millis() - lastSyncAttempt > SYNC_INTERVAL) {
    lastSyncAttempt = millis();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n=== PERIODIC TIME SYNC ===");
      if (syncTimeWithFallback()) {
        Serial.println("Periodic sync berhasil");
      } else {
        Serial.println("Periodic sync gagal, menggunakan estimasi");
      }
    } else {
      Serial.println("Periodic sync dilewati - WiFi tidak terhubung");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SISTEM PRESENSI RFID ===");
  Serial.println("ESP32-C3 Super Mini Version");

  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(BUZZER_PIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED gagal diinisialisasi!");
    fatalError("OLED GAGAL");
  }

  showStartupAnimation();
  playStartupMelody();

  // Koneksi WiFi
  if (!connectToWiFi()) {
    Serial.println("WiFi gagal terhubung!");
    fatalError("WIFI GAGAL");
  }

  // Test API
  showProgress("PING API", 2000);
  int apiRetryCount = 0;
  while (!pingAPI()) {
    apiRetryCount++;
    Serial.printf("API ping gagal (percobaan %d)\n", apiRetryCount);
    showOLED("API GAGAL", "PERCOBAAN " + String(apiRetryCount));
    playToneError();
    delay(2000);

    if (apiRetryCount >= 5) {
      Serial.println("API tidak dapat dijangkau setelah 5 percobaan");
      fatalError("API TIDAK TERSEDIA");
    }
  }

  showOLED("API TERHUBUNG", "SINKRONISASI WAKTU");
  playToneSuccess();

  // Sinkronisasi waktu dengan fallback
  Serial.println("\n=== INISIALISASI WAKTU ===");
  if (!syncTimeWithFallback()) {
    // Jika NTP gagal, cek apakah ada backup time
    struct tm timeinfo;
    if (!getTimeWithFallback(&timeinfo)) {
      Serial.println("PERINGATAN: Tidak ada waktu yang tersedia!");
      showOLED("PERINGATAN", "WAKTU TIDAK TERSEDIA");
      playToneError();
      delay(3000);

      // Sistem tetap jalan tapi tanpa fitur sleep otomatis
      showOLED("MODE MANUAL", "TANPA AUTO SLEEP");
      delay(2000);
    } else {
      Serial.println("Menggunakan waktu estimasi dari backup");
      showOLED("WAKTU ESTIMASI", "AKURASI TERBATAS");
      playToneError();
      delay(2000);
    }
  }

  // Inisialisasi RFID
  Serial.println("\n=== INISIALISASI RFID ===");
  SPI.begin(SCK, MISO, MOSI, SS_PIN);
  rfid.PCD_Init();
  delay(100);

  // Test RFID module
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  Serial.printf("MFRC522 Version: 0x%02X\n", version);

  if (version == 0x00 || version == 0xFF) {
    Serial.println("MFRC522 tidak terdeteksi!");
    fatalError("RC522 TIDAK TERDETEKSI");
  }

  Serial.println("MFRC522 berhasil diinisialisasi");

  // Sistem siap
  showOLED("SISTEM SIAP", "TEMPELKAN KARTU");
  playToneSuccess();
  Serial.println("\n=== SISTEM SIAP DIGUNAKAN ===\n");

  // Simpan waktu boot untuk estimasi
  bootTime = millis();
}

void loop() {
  // Periodic time sync setiap 1 jam
  periodicTimeSync();

  // Deteksi kartu RFID
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String rfidStr = uidToString(rfid.uid.uidByte, rfid.uid.size);

    // Debounce protection
    if (rfidStr == lastUid && millis() - lastScanTime < DEBOUNCE_TIME) {
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      return;
    }

    lastUid = rfidStr;
    lastScanTime = millis();

    Serial.println("🏷️  RFID Terdeteksi: " + rfidStr);
    showOLED("RFID TERDETEKSI", rfidStr);
    playToneNotify();
    delay(50);

    // Kirim ke server
    String pesan, nama, status, waktu;
    bool sukses = kirimPresensi(rfidStr, pesan, nama, status, waktu);

    // Tampilkan hasil
    showOLED(sukses ? "BERHASIL" : "GAGAL", pesan);

    if (sukses) {
      Serial.println("✓ Presensi berhasil: " + pesan);
      playToneSuccess();
    } else {
      Serial.println("✗ Presensi gagal: " + pesan);
      playToneError();
    }

    delay(150);

    // Reset RFID
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // Tampilkan standby screen
  showStandbySignal();
  delay(80);

  // Auto sleep mode (hanya jika waktu tersedia)
  struct tm timeinfo;
  if (getTimeWithFallback(&timeinfo)) {
    int jam = timeinfo.tm_hour;

    // Sleep mode jam 18:00 - 05:00
    if (jam >= START_SLEEP || jam < END_SLEEP) {
      Serial.printf("Memasuki sleep mode pada jam %02d:%02d\n",
                    timeinfo.tm_hour, timeinfo.tm_min);

      showOLED("SLEEP MODE", "SAMPAI PAGI");
      delay(2000);

      // Hitung durasi sleep
      int detikSisa;
      if (jam >= START_SLEEP) {
        // Tidur sampai jam 5 pagi
        detikSisa = ((24 - jam + END_SLEEP) * 3600) - (timeinfo.tm_min * 60 + timeinfo.tm_sec);
      } else {
        // Tidur sampai jam 5 (jika sekarang masih dini hari)
        detikSisa = ((END_SLEEP - jam) * 3600) - (timeinfo.tm_min * 60 + timeinfo.tm_sec);
      }

      Serial.printf("Sleep duration: %d detik (%d jam %d menit)\n",
                    detikSisa, detikSisa / 3600, (detikSisa % 3600) / 60);

      uint64_t sleepDuration = (uint64_t)detikSisa * 1000000ULL;
      esp_sleep_enable_timer_wakeup(sleepDuration);

      Serial.println("Memasuki deep sleep...");
      esp_deep_sleep_start();
    }
  }
}

void showStandbySignal() {
  display.clearDisplay();
  String title = "TEMPELKAN KARTU";
  int16_t x1, y1;
  uint16_t w1, h1;
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.getTextBounds(title, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 20);
  display.println(title);

  // Tampilkan waktu jika tersedia
  struct tm timeinfo;
  if (getTimeWithFallback(&timeinfo)) {
    String timeStr = String(timeinfo.tm_hour) + ":" + (timeinfo.tm_min < 10 ? "0" : "") + String(timeinfo.tm_min);
    display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w1, &h1);
    display.setCursor((SCREEN_WIDTH - w1) / 2, 35);
    display.println(timeStr);
  }

  // WiFi signal indicator
  if (WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    int bars = 0;
    if (rssi > -67) bars = 4;
    else if (rssi > -70) bars = 3;
    else if (rssi > -80) bars = 2;
    else if (rssi > -90) bars = 1;
    else bars = 0;

    int baseX = SCREEN_WIDTH - 18, barWidth = 3, spacing = 2;
    for (int i = 0; i < 4; i++) {
      int barHeight = 2 + i * 2, x = baseX + i * (barWidth + spacing), y = 10 - barHeight;
      if (i < bars) display.fillRect(x, y, barWidth, barHeight, WHITE);
      else display.drawRect(x, y, barWidth, barHeight, WHITE);
    }
  } else {
    // WiFi disconnected indicator
    display.setCursor(SCREEN_WIDTH - 20, 2);
    display.println("X");
  }

  display.display();
}

void fatalError(String p) {
  Serial.println("FATAL ERROR: " + p);
  showOLED(p, "RESTART...");
  playToneError();
  delay(3000);
  ESP.restart();
}

bool connectToWiFi() {
  Serial.println("=== KONEKSI WIFI ===");
  WiFi.mode(WIFI_STA);

  for (int i = 0; i < WIFI_COUNT; i++) {
    Serial.printf("Mencoba WiFi [%d/%d]: %s\n", i + 1, WIFI_COUNT, WIFI_SSIDS[i]);
    WiFi.begin(WIFI_SSIDS[i], WIFI_PASSWORDS[i]);

    for (int r = 0; r < 20 && WiFi.status() != WL_CONNECTED; r++) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);

      String s = WIFI_SSIDS[i];
      display.setCursor((SCREEN_WIDTH - s.length() * 6) / 2, 10);
      display.println(s);

      int d = r % 4;
      String l = "MENGHUBUNGKAN";
      display.setCursor((SCREEN_WIDTH - l.length() * 6) / 2, 30);
      display.print(l);
      for (int j = 0; j < d; j++) display.print(".");

      display.display();
      delay(300);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("✓ WiFi terhubung ke: %s\n", WIFI_SSIDS[i]);
      Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());

      showOLED("WIFI TERHUBUNG", WiFi.localIP().toString());
      delay(2000);
      return true;
    }

    Serial.printf("✗ Gagal terhubung ke: %s\n", WIFI_SSIDS[i]);
  }

  Serial.println("✗ Semua WiFi gagal!");
  return false;
}

bool pingAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terhubung untuk ping API");
    return false;
  }

  HTTPClient http;
  http.setTimeout(10000);  // 10 detik timeout
  http.begin(API_BASE_URL + "/presensi/ping");
  http.addHeader("X-API-KEY", API_SECRET);

  Serial.println("Ping API: " + API_BASE_URL + "/presensi/ping");
  int code = http.GET();
  String response = http.getString();
  http.end();

  Serial.printf("API Response: %d - %s\n", code, response.c_str());
  return code == 200;
}

String uidToString(uint8_t* uid, uint8_t len) {
  char b[11];
  if (len >= 4) {
    uint32_t v = (uid[3] << 24) | (uid[2] << 16) | (uid[1] << 8) | uid[0];
    sprintf(b, "%010lu", v);
  } else {
    sprintf(b, "%02X%02X", uid[0], uid[1]);
  }
  return String(b);
}

bool kirimPresensi(String rfid, String& pesan, String& nama, String& status, String& waktu) {
  if (WiFi.status() != WL_CONNECTED) {
    pesan = "TIDAK ADA WIFI";
    Serial.println("Gagal kirim presensi: WiFi tidak terhubung");
    return false;
  }

  HTTPClient http;
  http.setTimeout(15000);  // 15 detik timeout
  http.begin(API_BASE_URL + "/presensi/rfid");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("X-API-KEY", API_SECRET);

  String payload = "{\"rfid\":\"" + rfid + "\"}";
  Serial.println("Mengirim payload: " + payload);

  int code = http.POST(payload);
  String res = http.getString();
  http.end();

  Serial.printf("API Response [%d]:\n%s\n", code, res.c_str());

  // Parse JSON response
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, res);

  if (error) {
    pesan = "RESPON TIDAK VALID";
    Serial.println("JSON parsing error: " + String(error.c_str()));
    return false;
  }

  pesan = doc["message"] | "TERJADI KESALAHAN";

  if (code == 200 && doc.containsKey("data")) {
    JsonObject d = doc["data"];
    nama = d["nama"] | "-";
    waktu = d["waktu"] | "-";
    status = d["statusPulang"] | d["status"] | "-";

    Serial.println("Presensi berhasil - Nama: " + nama + ", Status: " + status);
    return true;
  }

  Serial.println("Presensi gagal: " + pesan);
  return false;
}

void showOLED(String l1, String l2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  l1.toUpperCase();
  l2.toUpperCase();

  int16_t x1, y1;
  uint16_t w1, h1;

  display.getTextBounds(l1, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 10);
  display.println(l1);

  display.getTextBounds(l2, 0, 0, &x1, &y1, &w1, &h1);
  display.setCursor((SCREEN_WIDTH - w1) / 2, 30);
  display.println(l2);

  display.display();
}

void playToneSuccess() {
  for (int i = 0; i < 2; i++) {
    tone(BUZZER_PIN, 1000, 100);
    delay(150);
  }
  noTone(BUZZER_PIN);
}

void playToneError() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 800, 150);
    delay(200);
  }
  noTone(BUZZER_PIN);
}

void playToneNotify() {
  tone(BUZZER_PIN, 1200, 100);
  delay(120);
  noTone(BUZZER_PIN);
}

void playStartupMelody() {
  int melody[] = { 1000, 1200, 1000, 1200 };
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, melody[i], 100);
    delay(150);
  }
  noTone(BUZZER_PIN);
}

void showProgress(String m, int t) {
  int step = 8, dps = t / (SCREEN_WIDTH / step), x = (SCREEN_WIDTH - 80) / 2;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor((SCREEN_WIDTH - m.length() * 6) / 2, 20);
  display.println(m);
  display.display();

  for (int i = 0; i <= 80; i += step) {
    display.fillRect(x, 40, i, 4, WHITE);
    display.display();
    delay(dps);
  }
  delay(500);
}

void showStartupAnimation() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  String j = "ZEDLABS", s = "INNOVATE BEYOND", s2 = "LIMITS", l = "Starting";
  int xj = (SCREEN_WIDTH - (j.length() * 12)) / 2, xs = (SCREEN_WIDTH - (s.length() * 6)) / 2,
      xs2 = (SCREEN_WIDTH - (s2.length() * 6)) / 2, xl = (SCREEN_WIDTH - (l.length() * 6)) / 2;

  for (int x = -80; x <= xj; x += 4) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(x, 5);
    display.println(j);

    display.setTextSize(1);
    display.setCursor(xs, 30);
    display.println(s);
    display.setCursor(xs2, 40);
    display.println(s2);

    display.display();
    delay(30);
  }

  delay(300);
  display.setTextSize(1);
  display.setCursor(xl, 55);
  display.print(l);
  display.display();

  for (int i = 0; i < 3; i++) {
    delay(300);
    display.print(".");
    display.display();
  }

  showProgress("MENYIAPKAN", 2000);
  delay(1000);
  display.clearDisplay();
  display.display();
}
