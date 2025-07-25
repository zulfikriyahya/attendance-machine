#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include "config.h"
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 rfid(SS_PIN, RST_PIN);
String lastUid = "";
unsigned long lastScanTime = 0;
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) fatalError("OLED GAGAL");
  showStartupAnimation();
  playStartupMelody();
  if (!connectToWiFi()) fatalError("WIFI GAGAL");
  showProgress("PING API", 2000);
  while (!pingAPI()) {
    showOLED("API GAGAL", "MENGULANG...");
    playToneError();
    delay(2000);
  }
  showOLED("API TERHUBUNG", "TEMPELKAN KARTU");
  playToneSuccess();
  delay(2000);
  SPI.begin(SCK, MISO, MOSI, SS_PIN);
  rfid.PCD_Init();
  delay(4);
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  if (version == 0x00 || version == 0xFF) fatalError("RC522 TIDAK TERDETEKSI");
}
void loop() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String rfidStr = uidToString(rfid.uid.uidByte, rfid.uid.size);
    if (rfidStr == lastUid && millis() - lastScanTime < DEBOUNCE_TIME) return;
    lastUid = rfidStr;
    lastScanTime = millis();
    Serial.println("\xF0\x9F\x93\xB1 RFID: " + rfidStr);
    showOLED("RFID TERDETEKSI", rfidStr);
    playToneNotify();
    delay(50);
    String pesan, nama, status, waktu;
    bool sukses = kirimPresensi(rfidStr, pesan, nama, status, waktu);
    showOLED(sukses ? "BERHASIL" : "GAGAL", pesan);
    if (sukses) playToneSuccess();
    else playToneError();
    delay(150);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
  showStandbySignal();
  delay(80);
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
  display.display();
}
void fatalError(String p) {
  showOLED(p, "RESTART...");
  delay(3000);
  ESP.restart();
}
bool connectToWiFi() {
  for (int i = 0; i < WIFI_COUNT; i++) {
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
      showOLED("WIFI TERHUBUNG", WiFi.localIP().toString());
      delay(2000);
      return true;
    }
  }
  return false;
}
bool pingAPI() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin(API_BASE_URL + "/presensi/ping");
  http.addHeader("X-API-KEY", API_SECRET);
  int code = http.GET();
  http.end();
  return code == 200;
}
String uidToString(uint8_t* uid, uint8_t len) {
  char b[11];
  if (len >= 4) {
    uint32_t v = (uid[3] << 24) | (uid[2] << 16) | (uid[1] << 8) | uid[0];
    sprintf(b, "%010lu", v);
  } else sprintf(b, "%02X%02X", uid[0], uid[1]);
  return String(b);
}
bool kirimPresensi(String rfid, String& pesan, String& nama, String& status, String& waktu) {
  if (WiFi.status() != WL_CONNECTED) {
    pesan = "TIDAK ADA WIFI";
    return false;
  }
  HTTPClient http;
  http.begin(API_BASE_URL + "/presensi/rfid");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("X-API-KEY", API_SECRET);
  String payload = "{\"rfid\":\"" + rfid + "\"}";
  int code = http.POST(payload);
  String res = http.getString();
  http.end();
  Serial.println("RESPON API:\n" + res);
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, res)) {
    pesan = "RESPON TIDAK VALID (JSON)";
    return false;
  }
  pesan = doc["message"] | "TERJADI KESALAHAN";
  if (code == 200 && doc.containsKey("data")) {
    JsonObject d = doc["data"];
    nama = d["nama"] | "-";
    waktu = d["waktu"] | "-";
    status = d["statusPulang"] | d["status"] | "-";
    return true;
  }
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
    tone(BUZZER_PIN, 1000, 100);
    delay(150);
  }
  noTone(BUZZER_PIN);
}
void playToneNotify() {
  tone(BUZZER_PIN, 1000, 100);
  delay(120);
  noTone(BUZZER_PIN);
}
void playStartupMelody() {
  tone(BUZZER_PIN, 1000, 100);
  delay(150);
  tone(BUZZER_PIN, 1000, 100);
  delay(150);
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
  int xj = (SCREEN_WIDTH - (j.length() * 12)) / 2, xs = (SCREEN_WIDTH - (s.length() * 6)) / 2, xs2 = (SCREEN_WIDTH - (s2.length() * 6)) / 2, xl = (SCREEN_WIDTH - (l.length() * 6)) / 2;
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