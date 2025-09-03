#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

#define SDA_PIN 21
#define SCL_PIN 22
#define BUZZER_PIN 25
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DEBOUNCE_TIME 300

const char *WIFI_SSIDS[] = {"ZEDLABS", "LINE", "WORKSHOP"};
const char *WIFI_PASSWORDS[] = {"18012000", "18012000", "18012000"};
const int WIFI_COUNT = sizeof(WIFI_SSIDS) / sizeof(WIFI_SSIDS[0]);
const String API_BASE_URL = "https://presensi.mtsn1pandeglang.sch.id/api";
const String API_SECRET = "P@ndegl@ng_14012000*";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);

String lastUid = "";
unsigned long lastScanTime = 0;

void setup()
{
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(BUZZER_PIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    fatalError("OLED GAGAL");
  showStartupAnimation();
  playStartupMelody();

  if (!connectToWiFi())
    fatalError("WIFI GAGAL");
  showProgress("PING API", 2000);
  while (!pingAPI())
  {
    showOLED("API GAGAL", "MENGULANG...");
    playToneError();
    delay(2000);
  }

  showOLED("API TERHUBUNG", "TEMPELKAN KARTU");
  playToneSuccess();
  delay(2000);

  nfc.begin();
  if (!nfc.getFirmwareVersion())
    fatalError("PN532 ERROR");
  nfc.SAMConfig();
}

void loop()
{
  uint8_t uid[7];
  uint8_t uidLength;

  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength))
  {
    String rfid = uidToString(uid, uidLength);
    if (rfid == lastUid && millis() - lastScanTime < DEBOUNCE_TIME)
      return;

    lastUid = rfid;
    lastScanTime = millis();

    Serial.println("\xF0\x9F\x93\xB1 RFID: " + rfid);
    showOLED("RFID TERDETEKSI", rfid);
    playToneNotify();
    delay(50); // Lebih singkat dari sebelumnya

    String pesan, nama, status, waktu;
    bool sukses = kirimPresensi(rfid, pesan, nama, status, waktu);

    showOLED(sukses ? "BERHASIL" : "GAGAL", pesan);
    if (sukses)
      playToneSuccess();
    else
      playToneError();

    delay(150); // Menyempatkan pesan terbaca tapi cepat kembali standby
  }

  showStandbySignal();
  delay(80);
}

void showStandbySignal()
{
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
  if (rssi > -67)
    bars = 4;
  else if (rssi > -70)
    bars = 3;
  else if (rssi > -80)
    bars = 2;
  else if (rssi > -90)
    bars = 1;
  else
    bars = 0;

  int baseX = SCREEN_WIDTH - 18;
  int barWidth = 3;
  int spacing = 2;
  for (int i = 0; i < 4; i++)
  {
    int barHeight = 2 + i * 2;
    int x = baseX + i * (barWidth + spacing);
    int y = 10 - barHeight;
    if (i < bars)
    {
      display.fillRect(x, y, barWidth, barHeight, WHITE);
    }
    else
    {
      display.drawRect(x, y, barWidth, barHeight, WHITE);
    }
  }

  display.display();
}

void fatalError(String pesan)
{
  showOLED(pesan, "PERIKSA PERANGKAT");
  playToneError();
  while (true)
    ;
}

bool connectToWiFi()
{
  for (int i = 0; i < WIFI_COUNT; i++)
  {
    WiFi.begin(WIFI_SSIDS[i], WIFI_PASSWORDS[i]);
    for (int retry = 0; retry < 20 && WiFi.status() != WL_CONNECTED; retry++)
    {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);

      String ssid = WIFI_SSIDS[i];
      display.setCursor((SCREEN_WIDTH - ssid.length() * 6) / 2, 10);
      display.println(ssid);

      int dots = retry % 4;
      String loading = "MENGHUBUNGKAN";
      display.setCursor((SCREEN_WIDTH - loading.length() * 6) / 2, 30);
      display.print(loading);
      for (int d = 0; d < dots; d++)
        display.print(".");
      display.display();
      delay(300);
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      showOLED("WIFI TERHUBUNG", WiFi.localIP().toString());
      delay(2000);
      return true;
    }
  }
  return false;
}

bool pingAPI()
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  HTTPClient http;
  http.begin(API_BASE_URL + "/presensi/ping");
  http.addHeader("X-API-KEY", API_SECRET);
  int code = http.GET();
  http.end();
  return code == 200;
}

String uidToString(uint8_t *uid, uint8_t len)
{
  char buffer[11];
  if (len >= 4)
  {
    uint32_t val = (uid[3] << 24) | (uid[2] << 16) | (uid[1] << 8) | uid[0];
    sprintf(buffer, "%010lu", val);
  }
  else
  {
    sprintf(buffer, "%02X%02X", uid[0], uid[1]);
  }
  return String(buffer);
}

bool kirimPresensi(String rfid, String &pesanOut, String &nama, String &status, String &waktu)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    pesanOut = "TIDAK ADA WIFI";
    return false;
  }

  HTTPClient http;
  http.begin(API_BASE_URL + "/presensi/rfid");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("X-API-KEY", API_SECRET);

  String payload = "{\"rfid\":\"" + rfid + "\"}";
  int code = http.POST(payload);
  String response = http.getString();
  http.end();

  Serial.println("RESPON API:\n" + response);

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, response))
  {
    pesanOut = "RESPON TIDAK VALID (JSON)";
    return false;
  }

  pesanOut = doc["message"] | "TERJADI KESALAHAN";

  if (code == 200 && doc.containsKey("data"))
  {
    JsonObject data = doc["data"];
    nama = data["nama"] | "-";
    waktu = data["waktu"] | "-";
    status = data["statusPulang"] | data["status"] | "-";
    return true;
  }

  return false;
}

void showOLED(String line1, String line2)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  line1.toUpperCase();
  line2.toUpperCase();

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

void playToneSuccess()
{
  for (int i = 0; i < 2; i++)
  {
    tone(BUZZER_PIN, 1000, 100);
    delay(150); // jeda antar beep
  }
  noTone(BUZZER_PIN);
}

void playToneError()
{
  for (int i = 0; i < 3; i++)
  {
    tone(BUZZER_PIN, 1000, 100);
    delay(150); // jeda antar beep
  }
  noTone(BUZZER_PIN);
}

void playToneNotify()
{
  tone(BUZZER_PIN, 1000, 100); // hanya 1 beep
  delay(120);
  noTone(BUZZER_PIN);
}

void playStartupMelody()
{
  tone(BUZZER_PIN, 1000, 100);
  delay(150);
  tone(BUZZER_PIN, 1000, 100);
  delay(150);
  noTone(BUZZER_PIN);
}

void showProgress(String message, int totalDelay)
{
  int step = 8;
  int delayPerStep = totalDelay / (SCREEN_WIDTH / step);
  int x = (SCREEN_WIDTH - 80) / 2;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor((SCREEN_WIDTH - message.length() * 6) / 2, 20);
  display.println(message);
  display.display();

  for (int i = 0; i <= 80; i += step)
  {
    display.fillRect(x, 40, i, 4, WHITE);
    display.display();
    delay(delayPerStep);
  }
  delay(500);
}

void showStartupAnimation()
{
  display.clearDisplay();
  display.setTextColor(WHITE);

  String judul = "ZEDLABS";
  String subjudul = "INNOVATE BEYOND";
  String subjudul2 = "LIMITS";
  String teksLoading = "Starting";

  int x_judul = (SCREEN_WIDTH - (judul.length() * 12)) / 2;
  int x_subjudul = (SCREEN_WIDTH - (subjudul.length() * 6)) / 2;
  int x_subjudul2 = (SCREEN_WIDTH - (subjudul2.length() * 6)) / 2;
  int x_loading = (SCREEN_WIDTH - (teksLoading.length() * 6)) / 2;

  for (int x = -80; x <= x_judul; x += 4)
  {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(x, 5);
    display.println(judul);

    display.setTextSize(1);
    display.setCursor(x_subjudul, 30);
    display.println(subjudul);
    display.setCursor(x_subjudul2, 40);
    display.println(subjudul2);

    display.display();
    delay(30);
  }

  delay(300);
  display.setTextSize(1);
  display.setCursor(x_loading, 55);
  display.print(teksLoading);
  display.display();

  for (int i = 0; i < 3; i++)
  {
    delay(300);
    display.print(".");
    display.display();
  }

  showProgress("MENYIAPKAN", 2000);

  delay(1000);
  display.clearDisplay();
  display.display();
}
