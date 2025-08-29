#ifndef CONFIG_H
#define CONFIG_H
#define RST 3
#define SS 7
#define SDA 8
#define SCL 9
#define BUZ 10
#define SW 128
#define SH 64
#define DEB 300
#define SCK 4
#define MSO 5
#define MSI 6

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
#endif