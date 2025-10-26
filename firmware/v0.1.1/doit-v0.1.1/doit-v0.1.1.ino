#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>

#define RST 16
#define SS 5
#define SCK 18
#define MSI 23
#define MSO 19
#define CS 15
#define SDA 21
#define SCL 22
#define BUZ 4

#define SW 128
#define SH 64
#define DEB 300

const char WS1[] PROGMEM = "ZEDLABS";                             // SSID Wifi 1
const char WS2[] PROGMEM = "ZULFIKRIYAHYA";                       // SSID Wifi 2 (Opsional)
const char WP1[] PROGMEM = "Password1";                           // Password Wifi 1
const char WP2[] PROGMEM = "Password2";                           // Password Wifi 2 (Opsional)
const char API[] PROGMEM = "https://presensi.example.sch.id/api"; // Sesuaikan dengan APP_URL pada.env
const char KEY[] PROGMEM = "SecretApi";                           // Sesuaikan dengan API_SECRET pada.env
const char NT1[] PROGMEM = "pool.ntp.org";
const char NT2[] PROGMEM = "time.google.com";
const char NT3[] PROGMEM = "id.pool.ntp.org";
const char NT4[] PROGMEM = "time.nist.gov";
const char NT5[] PROGMEM = "time.cloudflare.com";

const int WCT = 2;
const int NCT = 5;
const int NTO = 8000;
const int MNR = 2;
const int SST = 18; // Mulai Sleep Mode
const int EST = 5;  // Selesai Sleep Mode
const long GMT = 25200;
const int DST = 0;

RTC_DATA_ATTR time_t lvt = 0;
RTC_DATA_ATTR bool tws = false;
RTC_DATA_ATTR unsigned long bt = 0;

Adafruit_SSD1306 dsp(SW, SH, &Wire, -1);
MFRC522 rfd(SS, RST);

char luid[11] = "";
unsigned long lst = 0;
char buf[64];

bool syncTimeWithFallback()
{
  for (int si = 0; si < NCT; si++)
  {
    char nts[32];
    switch (si)
    {
    case 0:
      strcpy_P(nts, NT1);
      break;
    case 1:
      strcpy_P(nts, NT2);
      break;
    case 2:
      strcpy_P(nts, NT3);
      break;
    case 3:
      strcpy_P(nts, NT4);
      break;
    case 4:
      strcpy_P(nts, NT5);
      break;
    }
    configTime(GMT, DST, nts);
    for (int r = 0; r < MNR; r++)
    {
      snprintf_P(buf, sizeof(buf), PSTR("Server %d/%d"), si + 1, NCT);
      showOLED(F("SYNC WAKTU"), buf);
      struct tm ti;
      unsigned long st = millis();
      bool tr = false;
      while ((millis() - st) < NTO)
      {
        if (getLocalTime(&ti))
        {
          if (ti.tm_year >= 120)
          {
            tr = true;
            break;
          }
        }
        delay(100);
      }
      if (tr)
      {
        lvt = mktime(&ti);
        tws = true;
        bt = millis();
        snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
        showOLED(F("WAKTU TERSYNC"), buf);
        delay(1500);
        return true;
      }
      delay(500);
    }
  }
  return false;
}

bool getTimeWithFallback(struct tm *ti)
{
  if (getLocalTime(ti))
  {
    if (ti->tm_year >= 120)
    {
      return true;
    }
  }
  if (tws && lvt > 0)
  {
    unsigned long es = (millis() - bt) / 1000;
    time_t et = lvt + es;
    *ti = *localtime(&et);
    return true;
  }
  return false;
}

inline void periodicTimeSync()
{
  static unsigned long lsa = 0;
  const unsigned long SI = 3600000UL;
  if (millis() - lsa > SI)
  {
    lsa = millis();
    if (WiFi.status() == WL_CONNECTED)
    {
      syncTimeWithFallback();
    }
  }
}

void setup()
{
  Wire.begin(SDA, SCL);
  pinMode(BUZ, OUTPUT);
  dsp.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  showStartupAnimation();
  playStartupMelody();

  if (!connectToWiFi())
  {
    fatalError(F("WIFI GAGAL"));
  }
  showProgress(F("PING API"), 2000);
  int arc = 0;
  while (!pingAPI())
  {
    arc++;
    snprintf_P(buf, sizeof(buf), PSTR("PERCOBAAN %d"), arc);
    showOLED(F("API GAGAL"), buf);
    playToneError();
    delay(2000);
    if (arc >= 5)
    {
      fatalError(F("API TIDAK TERSEDIA"));
    }
  }
  showOLED(F("API TERHUBUNG"), F("SINKRONISASI WAKTU"));
  playToneSuccess();

  if (!syncTimeWithFallback())
  {
    struct tm ti;
    if (!getTimeWithFallback(&ti))
    {
      showOLED(F("PERINGATAN"), F("WAKTU TIDAK TERSEDIA"));
      playToneError();
      delay(3000);
      showOLED(F("MODE MANUAL"), F("TANPA AUTO SLEEP"));
      delay(2000);
    }
    else
    {
      showOLED(F("WAKTU ESTIMASI"), F("AKURASI TERBATAS"));
      playToneError();
      delay(2000);
    }
  }

  SPI.begin(SCK, MSO, MSI, SS);
  rfd.PCD_Init();
  delay(100);
  byte ver = rfd.PCD_ReadRegister(rfd.VersionReg);
  if (ver == 0x00 || ver == 0xFF)
  {
    fatalError(F("RC522 TIDAK TERDETEKSI"));
  }
  showOLED(F("SISTEM SIAP"), F("TEMPELKAN KARTU"));
  playToneSuccess();
  bt = millis();
}

void loop()
{
  periodicTimeSync();
  struct tm ti;
  if (getTimeWithFallback(&ti))
  {
    int jm = ti.tm_hour;
    if (jm >= SST || jm < EST)
    {
      showOLED(F("SLEEP MODE"), F("SAMPAI PAGI"));
      delay(2000);
      int ds;
      if (jm >= SST)
      {
        ds = ((24 - jm + EST) * 3600) - (ti.tm_min * 60 + ti.tm_sec);
      }
      else
      {
        ds = ((EST - jm) * 3600) - (ti.tm_min * 60 + ti.tm_sec);
      }
      uint64_t sd = (uint64_t)ds * 1000000ULL;
      esp_sleep_enable_timer_wakeup(sd);
      esp_deep_sleep_start();
    }
  }
  if (rfd.PICC_IsNewCardPresent() && rfd.PICC_ReadCardSerial())
  {
    uidToString(rfd.uid.uidByte, rfd.uid.size, buf);
    if (strcmp(buf, luid) == 0 && millis() - lst < DEB)
    {
      rfd.PICC_HaltA();
      rfd.PCD_StopCrypto1();
      return;
    }
    strcpy(luid, buf);
    lst = millis();
    showOLED(F("RFID TERDETEKSI"), buf);
    playToneNotify();
    delay(50);

    char psn[32], nm[32], sts[16], wkt[16];
    bool sks = kirimPresensi(buf, psn, nm, sts, wkt);
    showOLED(sks ? F("BERHASIL") : F("INFO"), psn);
    if (sks)
    {
      playToneSuccess();
    }
    else
    {
      playToneError();
    }
    delay(150);
    rfd.PICC_HaltA();
    rfd.PCD_StopCrypto1();
  }

  showStandbySignal();
  delay(80);
}

void showStandbySignal()
{
  dsp.clearDisplay();
  const __FlashStringHelper *ttl = F("TEMPELKAN KARTU");
  int16_t x1, y1;
  uint16_t w1, h1;
  dsp.setTextSize(1);
  dsp.setTextColor(WHITE);
  dsp.getTextBounds(ttl, 0, 0, &x1, &y1, &w1, &h1);
  dsp.setCursor((SW - w1) / 2, 20);
  dsp.println(ttl);

  struct tm ti;
  if (getTimeWithFallback(&ti))
  {
    snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    dsp.getTextBounds(buf, 0, 0, &x1, &y1, &w1, &h1);
    dsp.setCursor((SW - w1) / 2, 35);
    dsp.println(buf);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    long rsi = WiFi.RSSI();
    int brs = (rsi > -67) ? 4 : (rsi > -70) ? 3
                            : (rsi > -80)   ? 2
                            : (rsi > -90)   ? 1
                                            : 0;
    const int bx = SW - 18, bw = 3, sp = 2;
    for (int i = 0; i < 4; i++)
    {
      int bh = 2 + i * 2, x = bx + i * (bw + sp), y = 10 - bh;
      if (i < brs)
        dsp.fillRect(x, y, bw, bh, WHITE);
      else
        dsp.drawRect(x, y, bw, bh, WHITE);
    }
  }
  else
  {
    dsp.setCursor(SW - 20, 2);
    dsp.println(F("X"));
  }
  dsp.display();
}

void fatalError(const __FlashStringHelper *p)
{
  showOLED(p, F("RESTART..."));
  playToneError();
  delay(3000);
  ESP.restart();
}

bool connectToWiFi()
{
  WiFi.mode(WIFI_STA);
  for (int i = 0; i < WCT; i++)
  {
    char sid[16], pwd[16];
    if (i == 0)
    {
      strcpy_P(sid, WS1);
      strcpy_P(pwd, WP1);
    }
    else
    {
      strcpy_P(sid, WS2);
      strcpy_P(pwd, WP2);
    }
    WiFi.begin(sid, pwd);
    for (int r = 0; r < 20 && WiFi.status() != WL_CONNECTED; r++)
    {
      dsp.clearDisplay();
      dsp.setTextSize(1);
      dsp.setTextColor(WHITE);
      dsp.setCursor((SW - strlen(sid) * 6) / 2, 10);
      dsp.println(sid);
      const char ldg[] = "MENGHUBUNGKAN";
      int dts = r % 4;
      dsp.setCursor((SW - strlen(ldg) * 6) / 2, 30);
      dsp.print(ldg);
      for (int j = 0; j < dts; j++)
        dsp.print('.');
      dsp.display();
      delay(300);
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      WiFi.localIP().toString().toCharArray(buf, sizeof(buf));
      showOLED(F("WIFI TERHUBUNG"), buf);
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
  HTTPClient htp;
  htp.setTimeout(10000);
  char url[80];
  strcpy_P(url, API);
  strcat_P(url, PSTR("/presensi/ping"));
  htp.begin(url);
  char apk[32];
  strcpy_P(apk, KEY);
  htp.addHeader(F("X-API-KEY"), apk);
  int cde = htp.GET();
  htp.end();
  return cde == 200;
}

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

bool kirimPresensi(const char *rfd, char *psn, char *nm, char *sts, char *wkt)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    strcpy_P(psn, PSTR("TIDAK ADA WIFI"));
    return false;
  }
  HTTPClient htp;
  htp.setTimeout(30000);
  char url[80];
  strcpy_P(url, API);
  strcat_P(url, PSTR("/presensi/rfid"));
  htp.begin(url);
  htp.addHeader(F("Content-Type"), F("application/json"));
  htp.addHeader(F("Accept"), F("application/json"));
  char apk[32];
  strcpy_P(apk, KEY);
  htp.addHeader(F("X-API-KEY"), apk);
  char pld[64];
  snprintf_P(pld, sizeof(pld), PSTR("{\"rfid\":\"%s\"}"), rfd);
  int cde = htp.POST(pld);
  String res = htp.getString();
  htp.end();

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, res);
  if (err)
  {
    strcpy_P(psn, PSTR("ULANGI !"));
    return false;
  }

  const char *msg = doc["message"] | "TERJADI KESALAHAN";
  strncpy(psn, msg, 31);
  psn[31] = '\0';

  if (cde == 200 && doc.containsKey("data"))
  {
    JsonObject d = doc["data"];
    const char *n = d["nama"] | "-";
    const char *w = d["waktu"] | "-";
    const char *s = d["statusPulang"] | d["status"] | "-";
    strncpy(nm, n, 31);
    nm[31] = '\0';
    strncpy(wkt, w, 15);
    wkt[15] = '\0';
    strncpy(sts, s, 15);
    sts[15] = '\0';
    return true;
  }
  return false;
}

void showOLED(const __FlashStringHelper *l1, const char *l2)
{
  dsp.clearDisplay();
  dsp.setTextSize(1);
  dsp.setTextColor(WHITE);
  int16_t x1, y1;
  uint16_t w1, h1;
  dsp.getTextBounds(l1, 0, 0, &x1, &y1, &w1, &h1);
  dsp.setCursor((SW - w1) / 2, 10);
  dsp.println(l1);
  dsp.getTextBounds(l2, 0, 0, &x1, &y1, &w1, &h1);
  dsp.setCursor((SW - w1) / 2, 30);
  dsp.println(l2);
  dsp.display();
}

void showOLED(const __FlashStringHelper *l1, const __FlashStringHelper *l2)
{
  dsp.clearDisplay();
  dsp.setTextSize(1);
  dsp.setTextColor(WHITE);
  int16_t x1, y1;
  uint16_t w1, h1;
  dsp.getTextBounds(l1, 0, 0, &x1, &y1, &w1, &h1);
  dsp.setCursor((SW - w1) / 2, 10);
  dsp.println(l1);
  dsp.getTextBounds(l2, 0, 0, &x1, &y1, &w1, &h1);
  dsp.setCursor((SW - w1) / 2, 30);
  dsp.println(l2);
  dsp.display();
}

inline void playToneSuccess()
{
  for (int i = 0; i < 2; i++)
  {
    tone(BUZ, 3000, 100);
    delay(150);
  }
  noTone(BUZ);
}

inline void playToneError()
{
  for (int i = 0; i < 3; i++)
  {
    tone(BUZ, 3500, 150);
    delay(200);
  }
  noTone(BUZ);
}

inline void playToneNotify()
{
  tone(BUZ, 2500, 100);
  delay(120);
  noTone(BUZ);
}

void playStartupMelody()
{
  const int mld[] PROGMEM = {2500, 3000, 2500, 3000};
  for (int i = 0; i < 4; i++)
  {
    tone(BUZ, pgm_read_word_near(mld + i), 100);
    delay(150);
  }
  noTone(BUZ);
}

void showProgress(const __FlashStringHelper *m, int t)
{
  const int stp = 8, pw = 80;
  int dps = t / (pw / stp), x = (SW - pw) / 2;
  dsp.clearDisplay();
  dsp.setTextSize(1);
  dsp.setTextColor(WHITE);
  int16_t x1, y1;
  uint16_t w1, h1;
  dsp.getTextBounds(m, 0, 0, &x1, &y1, &w1, &h1);
  dsp.setCursor((SW - w1) / 2, 20);
  dsp.println(m);
  dsp.display();
  for (int i = 0; i <= pw; i += stp)
  {
    dsp.fillRect(x, 40, i, 4, WHITE);
    dsp.display();
    delay(dps);
  }
  delay(500);
}

void showStartupAnimation()
{
  dsp.clearDisplay();
  dsp.setTextColor(WHITE);
  const char ttl[] PROGMEM = "ZEDLABS";
  const char st1[] PROGMEM = "INNOVATE BEYOND";
  const char st2[] PROGMEM = "LIMITS";
  const char ldg[] PROGMEM = "STARTING v0.1.1";
  const int tln = 7;
  const int xj = (SW - (tln * 12)) / 2;
  const int xs = (SW - 15 * 6) / 2;
  const int xs2 = (SW - 6 * 6) / 2;
  const int xl = (SW - 15 * 6) / 2;

  for (int x = -80; x <= xj; x += 4)
  {
    dsp.clearDisplay();
    dsp.setTextSize(2);
    dsp.setCursor(x, 5);
    dsp.println((__FlashStringHelper *)ttl);
    dsp.setTextSize(1);
    dsp.setCursor(xs, 30);
    dsp.println((__FlashStringHelper *)st1);
    dsp.setCursor(xs2, 40);
    dsp.println((__FlashStringHelper *)st2);
    dsp.display();
    delay(30);
  }
  delay(300);
  dsp.setTextSize(1);
  dsp.setCursor(xl, 55);
  dsp.print((__FlashStringHelper *)ldg);
  dsp.display();
  for (int i = 0; i < 3; i++)
  {
    delay(300);
    dsp.print('.');
    dsp.display();
  }
  showProgress(F("MENYIAPKAN"), 2000);
  delay(1000);
  dsp.clearDisplay();
  dsp.display();
}
