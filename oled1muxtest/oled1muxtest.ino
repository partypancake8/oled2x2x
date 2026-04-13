// ─── 2x2 OLED Dashboard ───────────────────────────────────────────────────────
// Adafruit Feather ESP32-S3
// TCA9548A I2C mux at 0x70, four SSD1306 128x64 OLED at 0x3C
// Wire (SDA=GPIO3, SCL=GPIO4): TCA9548A + OLEDs + MAX17048 + SHT4x
//
// Screen layout (physical 2x2 grid):
//   CH0 (top-left,  FLIPPED) : Clock         — yellow bar at physical bottom
//   CH1 (top-right, normal)  : Temp/Humidity — yellow bar at physical top
//   CH2 (bot-left,  FLIPPED) : Battery       — yellow bar at physical bottom
//   CH3 (bot-right, normal)  : WiFi / IP     — yellow bar at physical top
// ─────────────────────────────────────────────────────────────────────────────

#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MAX1704X.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SHT4x.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDR    0x3C
#define TCA_ADDR     0x70

#define SDA_PIN 3   // SDA — all devices (STEMMA QT default)
#define SCL_PIN 4   // SCL — all devices

#define YBAR_BOT_Y    48
#define YBAR_BOT_H    15

// Network / time
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* TZ_INFO   = "EST5EDT,M3.2.0,M11.1.0";
const char* NTP1 = "pool.ntp.org";
const char* NTP2 = "time.nist.gov";
const char* NTP3 = "time.google.com";

#define DRAW_MS       100UL
#define SENSOR_MS    5000UL
#define BATTERY_MS  10000UL
#define WIFI_RETRY  10000UL

// ─── globals ──────────────────────────────────────────────────────────────────

Adafruit_SSD1306 disp0(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SSD1306 disp1(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SSD1306 disp2(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SSD1306 disp3(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Adafruit_MAX17048 maxlipo;
Adafruit_SHT4x    sht4;
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

bool  batteryOK   = false;
int   batteryPct  = -1;
float batteryV    = -1.0f;

bool  sht4OK      = false;
float lastTempF   = -999.0f;
float lastHumidity = -999.0f;

bool wifiConnected  = false;
bool wifiConnecting = false;
bool timeReady      = false;

unsigned long lastDraw = 0, lastSensor = 0, lastBattery = 0, lastWifiRetry = 0;

// ─── TCA helpers ──────────────────────────────────────────────────────────────

static void tcaSelect(uint8_t ch) {
    Wire.beginTransmission(TCA_ADDR);
    Wire.write((ch < 8) ? (1 << ch) : 0x00);
    Wire.endTransmission();
}
static void tcaDisable() {
    Wire.beginTransmission(TCA_ADDR);
    Wire.write(0x00);
    Wire.endTransmission();
}
static bool i2cProbe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

// ─── draw helpers ─────────────────────────────────────────────────────────────

static void centerText(Adafruit_SSD1306& d, const char* t, int y, int s) {
    int16_t x1, y1; uint16_t w, h;
    d.setTextSize(s);
    d.getTextBounds(t, 0, y, &x1, &y1, &w, &h);
    d.setCursor((SCREEN_WIDTH - (int)w) / 2, y);
    d.print(t);
}

static void battIcon(Adafruit_SSD1306& d, int x, int y, int w, int h, int pct) {
    d.drawRect(x, y, w, h, SSD1306_WHITE);
    int th = h / 2, ty = y + (h - th) / 2;
    d.fillRect(x + w, ty, 2, th, SSD1306_WHITE);
    if (pct >= 0) {
        int fw = ((w - 4) * constrain(pct, 0, 100)) / 100;
        if (fw > 0) d.fillRect(x + 2, y + 2, fw, h - 4, SSD1306_WHITE);
    }
}

// ─── per-screen draw functions ────────────────────────────────────────────────

static void drawCh0() {
    disp0.clearDisplay();
    disp0.setTextColor(SSD1306_WHITE);
    if (!timeReady) {
        centerText(disp0, "No time", 18, 1);
        centerText(disp0, "Connecting...", 30, 1);
    } else {
        struct tm ti;
        if (getLocalTime(&ti, 20)) {
            char hm[6]; strftime(hm, sizeof(hm), "%I:%M", &ti);
            if (hm[0] == '0') memmove(hm, hm + 1, sizeof(hm) - 1);
            centerText(disp0, hm, 4, 3);
            char sub[8]; strftime(sub, sizeof(sub), ":%S %p", &ti);
            centerText(disp0, sub, 34, 1);
        }
    }
    disp0.drawFastHLine(0, YBAR_BOT_Y - 1, SCREEN_WIDTH, SSD1306_WHITE);
    if (timeReady) {
        struct tm ti;
        if (getLocalTime(&ti, 20)) {
            char date[16]; strftime(date, sizeof(date), "%a  %b %d", &ti);
            centerText(disp0, date, YBAR_BOT_Y + 2, 1);
        }
    } else {
        centerText(disp0, "-- --- --", YBAR_BOT_Y + 2, 1);
    }
    disp0.display();
}

static void drawCh1() {
    disp1.clearDisplay();
    disp1.setTextColor(SSD1306_WHITE);
    centerText(disp1, "INDOOR", 3, 1);
    disp1.drawFastHLine(0, 15, SCREEN_WIDTH, SSD1306_WHITE);
    if (!sht4OK) {
        centerText(disp1, "No sensor", 34, 1);
    } else if (lastTempF < -998.0f) {
        centerText(disp1, "Reading...", 34, 1);
    } else {
        char tstr[8]; dtostrf(lastTempF, 4, 1, tstr);
        int16_t x1, y1; uint16_t nw, nh, fw, fh;
        disp1.setTextSize(3);
        disp1.getTextBounds(tstr, 0, 0, &x1, &y1, &nw, &nh);
        disp1.setTextSize(1);
        disp1.getTextBounds("F", 0, 0, &x1, &y1, &fw, &fh);
        int totalW = (int)nw + 10 + (int)fw;
        int sx = (SCREEN_WIDTH - totalW) / 2;
        disp1.setTextSize(3);
        disp1.setCursor(sx, 18);
        disp1.print(tstr);
        disp1.drawCircle(sx + (int)nw + 4, 20, 2, SSD1306_WHITE);
        disp1.setTextSize(2);
        disp1.setCursor(sx + (int)nw + 10, 22);
        disp1.print("F");
        int hpct = (int)constrain(lastHumidity, 0, 100);
        disp1.drawRect(4, 50, 120, 8, SSD1306_WHITE);
        disp1.fillRect(5, 51, (118 * hpct) / 100, 6, SSD1306_WHITE);
        char hstr[12]; snprintf(hstr, sizeof(hstr), "%.0f%% RH", lastHumidity);
        centerText(disp1, hstr, 42, 1);
    }
    disp1.display();
}

static void drawCh2() {
    disp2.clearDisplay();
    disp2.setTextColor(SSD1306_WHITE);
    if (!batteryOK) {
        centerText(disp2, "No gauge", 18, 1);
    } else if (batteryPct < 0) {
        centerText(disp2, "Reading...", 18, 1);
    } else {
        char pstr[8]; snprintf(pstr, sizeof(pstr), "%d%%", batteryPct);
        centerText(disp2, pstr, 2, 3);
        battIcon(disp2, 24, 34, 80, 14, batteryPct);
    }
    disp2.drawFastHLine(0, YBAR_BOT_Y - 1, SCREEN_WIDTH, SSD1306_WHITE);
    if (batteryOK && batteryV > 0) {
        char vstr[12]; snprintf(vstr, sizeof(vstr), "%.3f V", batteryV);
        centerText(disp2, vstr, YBAR_BOT_Y + 2, 1);
    } else {
        centerText(disp2, "---", YBAR_BOT_Y + 2, 1);
    }
    disp2.display();
}

static void drawCh3() {
    disp3.clearDisplay();
    disp3.setTextColor(SSD1306_WHITE);
    centerText(disp3, "NETWORK", 3, 1);
    disp3.drawFastHLine(0, 15, SCREEN_WIDTH, SSD1306_WHITE);
    if (!wifiConnected) {
        centerText(disp3, "Connecting...", 34, 1);
    } else {
        String ssid = WiFi.SSID();
        centerText(disp3, ssid.c_str(), 18, 1);
        String ip = WiFi.localIP().toString();
        centerText(disp3, ip.c_str(), 29, 1);
        int rssi = WiFi.RSSI();
        int rssiPct = (int)map(constrain(rssi, -90, -30), -90, -30, 0, 100);
        char rstr[16]; snprintf(rstr, sizeof(rstr), "%d dBm", rssi);
        disp3.setCursor(0, 42);
        disp3.print(rstr);
        disp3.drawRect(64, 42, 60, 7, SSD1306_WHITE);
        disp3.fillRect(65, 43, (58 * rssiPct) / 100, 5, SSD1306_WHITE);
        uint8_t mac[6]; WiFi.macAddress(mac);
        char macStr[14]; snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X", mac[3], mac[4], mac[5]);
        disp3.setCursor(0, 54);
        disp3.print("MAC: ...");
        disp3.print(macStr);
    }
    disp3.display();
}

// ─── sensor / network helpers ─────────────────────────────────────────────────

static void refreshBattery() {
    if (!batteryOK) return;
    float p = maxlipo.cellPercent();
    float v = maxlipo.cellVoltage();
    batteryPct = isnan(p) ? -1 : constrain((int)(p + 0.5f), 0, 100);
    batteryV   = isnan(v) ? -1.0f : v;
}

static void refreshSensor() {
    if (!sht4OK) return;
    sensors_event_t hum, temp;
    if (sht4.getEvent(&hum, &temp)) {
        lastTempF    = temp.temperature * 9.0f / 5.0f + 32.0f;
        lastHumidity = hum.relative_humidity;
    }
}

static void tryWifi() {
    if (wifiConnected) return;
    wifiConnecting = true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

// ─── setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(1500);

#ifdef NEOPIXEL_POWER
    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, HIGH);
#endif
#ifdef PIN_I2C_POWER
    pinMode(PIN_I2C_POWER, OUTPUT);
    digitalWrite(PIN_I2C_POWER, HIGH);
#endif
    delay(50);

    pixel.begin();
    pixel.setBrightness(30);
    pixel.setPixelColor(0, pixel.Color(5, 5, 0));
    pixel.show();

    Serial.println("\n=== 2x2 OLED Dashboard ===");
    Serial.println("Wire: SDA=3 SCL=4 (all devices)");

    Wire.begin(SDA_PIN, SCL_PIN);   // all devices: TCA, OLEDs, sensors
    delay(100);

    // General Call Reset — unstick any locked devices
    Wire.beginTransmission(0x00);
    Wire.write(0x06);
    Wire.endTransmission();
    delay(50);

    // ── Init all 4 displays ───────────────────────────────────────────────────
    Adafruit_SSD1306* disps[4] = {&disp0, &disp1, &disp2, &disp3};
    for (int ch = 0; ch < 4; ch++) {
        tcaSelect(ch);
        delay(10);
        if (!i2cProbe(OLED_ADDR)) {
            Serial.printf("  CH%d: OLED not found\n", ch);
            tcaDisable(); continue;
        }
        if (!disps[ch]->begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
            Serial.printf("  CH%d: begin() failed\n", ch);
            tcaDisable(); continue;
        }
        if (ch == 0 || ch == 2) disps[ch]->setRotation(2);
        disps[ch]->clearDisplay();
        disps[ch]->display();
        Serial.printf("  CH%d: OK\n", ch);
        tcaDisable();
    }

    // ── Battery gauge ─────────────────────────────────────────────────────────
    batteryOK = maxlipo.begin(&Wire);
    if (batteryOK) {
        maxlipo.quickStart();
        delay(250);
        refreshBattery();
        Serial.printf("MAX17048: OK  %d%%  %.3fV\n", batteryPct, batteryV);
    } else {
        Serial.println("MAX17048: not found");
    }

    // ── Temp/humidity sensor ──────────────────────────────────────────────────
    sht4OK = sht4.begin(&Wire);
    if (sht4OK) {
        sht4.setPrecision(SHT4X_HIGH_PRECISION);
        delay(20);
        refreshSensor();
        Serial.printf("SHT4x: OK  %.1fF  %.0f%%RH\n", lastTempF, lastHumidity);
    } else {
        Serial.println("SHT4x: not found");
    }

    // ── WiFi + NTP ────────────────────────────────────────────────────────────
    configTzTime(TZ_INFO, NTP1, NTP2, NTP3);
    tryWifi();

    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();

    Serial.println("Setup done.");
}

// ─── loop ─────────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    bool wasConnected = wifiConnected;
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected) {
        wifiConnecting = false;
        if (!wasConnected) {
            Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        }
        if (!timeReady) {
            struct tm ti;
            timeReady = getLocalTime(&ti, 50);
        }
    } else if (!wifiConnecting) {
        if (now - lastWifiRetry > WIFI_RETRY) {
            lastWifiRetry = now;
            tryWifi();
        }
    }

    if (now - lastBattery > BATTERY_MS) { lastBattery = now; refreshBattery(); }
    if (now - lastSensor  > SENSOR_MS)  { lastSensor  = now; refreshSensor();  }

    if (now - lastDraw > DRAW_MS) {
        lastDraw = now;
        tcaSelect(0); drawCh0();
        tcaSelect(1); drawCh1();
        tcaSelect(2); drawCh2();
        tcaSelect(3); drawCh3();
        tcaDisable();
    }
}
