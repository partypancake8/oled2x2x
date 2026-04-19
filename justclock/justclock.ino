// justclock.ino — Clock only with periodic metrics screen
// Adafruit Feather ESP32-S3 | SDA=3 SCL=4 OLED=0x3C
//
// Normal view : big clock + day of week (bottom right)
// Metrics view: every 60 s, for 10 s  →  uptime / wifi status / network name

#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MAX1704X.h>
#include <Adafruit_NeoPixel.h>

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDR     0x3C
#define SDA_PIN          3
#define SCL_PIN          4
#define HEADER_H        16
#define LED_BRIGHTNESS  35
#define LOW_BATTERY_PERCENT 5

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* TZ_INFO   = "EST5EDT,M3.2.0,M11.1.0";
const char* NTP1      = "pool.ntp.org";
const char* NTP2      = "time.nist.gov";
const char* NTP3      = "time.google.com";

Adafruit_SSD1306  display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MAX17048 maxlipo;
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

bool  batteryOK      = false;
int   batteryPercent = -1;
float batteryVoltage = -1.0f;
bool  timeReady      = false;
bool  wifiConnected  = false;
bool  wifiConnecting = false;

// Metrics state — triggers every METRICS_INTERVAL_MS, shows for METRICS_DURATION_MS
bool          metricsActive    = false;
unsigned long lastMetricsStart = 0;  // millis() when metrics last became active

const unsigned long BATTERY_READ_INTERVAL_MS = 10000UL;
const unsigned long NTP_RESYNC_INTERVAL_MS   = 6UL * 60UL * 60UL * 1000UL;
const unsigned long DRAW_INTERVAL_MS         = 50UL;
const unsigned long WIFI_RETRY_INTERVAL_MS   = 10000UL;
const unsigned long METRICS_INTERVAL_MS      = 60000UL;  // once per minute
const unsigned long METRICS_DURATION_MS      = 10000UL;  // show for 10 s

unsigned long lastBatteryRead      = 0;
unsigned long lastNtpSyncMs        = 0;
unsigned long lastDrawMs           = 0;
unsigned long lastReconnectAttempt = 0;

// ── helpers ──────────────────────────────────────────────────────────────────

void enableI2CPower() {
#ifdef PIN_I2C_POWER
  pinMode(PIN_I2C_POWER, OUTPUT); digitalWrite(PIN_I2C_POWER, HIGH);
#endif
}

void enableNeoPixelPower() {
#ifdef NEOPIXEL_POWER
  pinMode(NEOPIXEL_POWER, OUTPUT); digitalWrite(NEOPIXEL_POWER, HIGH);
#endif
}

void setPixel(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b)); pixel.show();
}

void updateStatusLED() {
  unsigned long now = millis();
  if (wifiConnecting) { setPixel(10, 5, 0); return; }
  if (wifiConnected && timeReady) {
    if ((now % 5000UL) < 120UL) setPixel(0, 10, 0); else setPixel(0, 0, 0);
    return;
  }
  if (wifiConnected && !timeReady) {
    if ((now % 1000UL) < 150UL) setPixel(0, 0, 10); else setPixel(0, 0, 0);
    return;
  }
  if ((now % 1200UL) < 180UL) setPixel(10, 0, 0); else setPixel(0, 0, 0);
}

void centerText(const String& t, int y, int s) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(s);
  display.getTextBounds(t, 0, y, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, y);
  display.print(t);
}

// ── battery icon ─────────────────────────────────────────────────────────────

void drawBatteryIcon(int x, int y, int w, int h, int pct) {
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  int th = h / 2, ty = y + (h - th) / 2;
  display.fillRect(x + w, ty, 3, th, SSD1306_WHITE);
  int ix = x + 2, iy = y + 2, iw = w - 4, ih = h - 4;
  if (pct < 0) {
    display.drawLine(ix, iy, ix + iw - 1, iy + ih - 1, SSD1306_WHITE);
    display.drawLine(ix + iw - 1, iy, ix, iy + ih - 1, SSD1306_WHITE);
    return;
  }
  int fw = (iw * constrain(pct, 0, 100)) / 100;
  if (fw > 0) display.fillRect(ix, iy, fw, ih, SSD1306_WHITE);
}

// ── wifi icon (10×10 box with check / dot / cross) ───────────────────────────

void drawWifiIcon(int x, int y) {
  display.drawRect(x, y, 10, 10, SSD1306_WHITE);
  if (wifiConnected) {
    display.drawLine(x + 2, y + 5, x + 4, y + 7, SSD1306_WHITE);
    display.drawLine(x + 4, y + 7, x + 7, y + 3, SSD1306_WHITE);
  } else if (wifiConnecting) {
    display.fillCircle(x + 5, y + 5, 2, SSD1306_WHITE);
  } else {
    display.drawLine(x + 2, y + 2, x + 7, y + 7, SSD1306_WHITE);
    display.drawLine(x + 7, y + 2, x + 2, y + 7, SSD1306_WHITE);
  }
}

// ── wifi / battery helpers ────────────────────────────────────────────────────

bool isLowBattery() { return (batteryPercent >= 0 && batteryPercent <= LOW_BATTERY_PERCENT); }

void refreshBattery() {
  if (!batteryOK) return;
  float p = maxlipo.cellPercent(), v = maxlipo.cellVoltage();
  batteryPercent = isnan(p) ? -1 : constrain((int)(p + 0.5f), 0, 100);
  batteryVoltage = isnan(v) ? -1.0f : v;
}

bool connectWiFi(unsigned long ms = 15000) {
  wifiConnecting = true; wifiConnected = false; updateStatusLED();
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t = millis();
  while (millis() - t < ms) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true; wifiConnecting = false; updateStatusLED(); return true;
    }
    updateStatusLED(); delay(200);
  }
  wifiConnected = false; wifiConnecting = false; updateStatusLED(); return false;
}

bool syncTimeFromNTP(unsigned long ms = 12000) {
  configTzTime(TZ_INFO, NTP1, NTP2, NTP3);
  struct tm ti;
  unsigned long t = millis();
  while (millis() - t < ms) {
    if (getLocalTime(&ti, 200)) { timeReady = true; lastNtpSyncMs = millis(); updateStatusLED(); return true; }
    // Keep the screen alive while waiting for NTP
    drawScreen();
    updateStatusLED();
    delay(100);
  }
  return false;
}

// ── header ────────────────────────────────────────────────────────────────────
// Same style as the original: battery left, time center, wifi icon right

void drawHeader() {
  display.fillRect(0, 0, SCREEN_WIDTH, HEADER_H, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  drawBatteryIcon(4, 3, 18, 9, batteryPercent);
  if (timeReady) {
    struct tm ti;
    if (getLocalTime(&ti, 20)) {
      char timebuf[16];
      strftime(timebuf, sizeof(timebuf), "%I:%M:%S %p", &ti);
      if (timebuf[0] == '0') memmove(timebuf, timebuf + 1, strlen(timebuf));
      int16_t x1, y1; uint16_t w, h;
      display.setTextSize(1);
      display.getTextBounds(timebuf, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((SCREEN_WIDTH - (int)w) / 2, 4);
      display.print(timebuf);
    }
  } else {
    display.setTextSize(1); display.setCursor(28, 2);
    if (batteryPercent >= 0) { display.print(batteryPercent); display.print("%"); }
    else display.print("--%");
  }
  drawWifiIcon(SCREEN_WIDTH - 12, 3);
}

// ── clock screen ──────────────────────────────────────────────────────────────
// Identical design to the original drawTimeScreen() + day of week bottom-right

void drawClockScreen() {
  int16_t x1, y1; uint16_t w, h;
  display.setTextColor(SSD1306_WHITE);

  if (!timeReady) {
    // Show placeholder clock so the screen is never blank
    const char* placeholder = "--:--";
    display.setTextSize(3);
    display.getTextBounds(placeholder, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - (int)w) / 2, 19);
    display.print(placeholder);
    display.setTextSize(1);
    centerText(wifiConnecting ? "connecting..." : "no ntp", 46, 1);
    return;
  }
  struct tm ti;
  if (!getLocalTime(&ti, 20)) {
    const char* placeholder = "--:--";
    display.setTextSize(3);
    display.getTextBounds(placeholder, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - (int)w) / 2, 19);
    display.print(placeholder);
    return;
  }

  // Large HH:MM
  char hm[6]; strftime(hm, sizeof(hm), "%I:%M", &ti);
  if (hm[0] == '0') memmove(hm, hm + 1, sizeof(hm) - 1);
  display.setTextSize(3);
  display.getTextBounds(hm, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, 19);
  display.setTextColor(SSD1306_WHITE);
  display.print(hm);

  // :SS AM/PM — centered, same as original
  char sec[3];  strftime(sec,  sizeof(sec),  "%S", &ti);
  char ampm[3]; strftime(ampm, sizeof(ampm), "%p", &ti);
  char sub[8];  snprintf(sub, sizeof(sub), ":%s %s", sec, ampm);
  display.setTextSize(1);
  display.getTextBounds(sub, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, 46);
  display.print(sub);

  // Day of week — bottom right (replaced by metrics screen once per minute)
  char dayStr[10]; strftime(dayStr, sizeof(dayStr), "%A", &ti);
  display.setTextSize(1);
  display.getTextBounds(dayStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - (int)w - 2, 56);
  display.print(dayStr);
}

// ── metrics screen ────────────────────────────────────────────────────────────
// Shows once per minute for 10 s; replaces the clock / day-of-week view

void drawMetricsScreen() {
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setTextSize(1);
  centerText("-- METRICS --", 18, 1);

  // Uptime
  unsigned long upSec = millis() / 1000UL;
  unsigned long upMin = upSec / 60;  upSec %= 60;
  unsigned long upHr  = upMin / 60;  upMin %= 60;
  unsigned long upDay = upHr  / 24;  upHr  %= 24;

  char upVal[20];
  if (upDay > 0)
    snprintf(upVal, sizeof(upVal), "%lud %02lu:%02lu:%02lu", upDay, upHr, upMin, upSec);
  else
    snprintf(upVal, sizeof(upVal), "%02lu:%02lu:%02lu", upHr, upMin, upSec);

  // Uptime value — slightly larger
  display.setTextSize(1);
  display.setCursor(4, 28);
  display.print("Up: ");
  display.print(upVal);

  // WiFi status
  display.setCursor(4, 40);
  display.print("WiFi: ");
  if (wifiConnected)       display.print("Connected");
  else if (wifiConnecting) display.print("Connecting...");
  else                     display.print("Offline");

  // Network name
  display.setCursor(4, 51);
  display.print("Net: ");
  display.print(wifiConnected ? WIFI_SSID : "--");
}

// ── low battery warning ───────────────────────────────────────────────────────

void drawLowBatteryWarning() {
  display.setTextColor(SSD1306_WHITE);
  centerText("LOW", 22, 2); centerText("BATTERY", 40, 2);
  if (batteryPercent >= 0) centerText(String(batteryPercent) + "%", 56, 1);
  else centerText("CHARGE SOON", 56, 1);
}

// ── composite draw ────────────────────────────────────────────────────────────

void drawScreen() {
  display.clearDisplay();
  drawHeader();
  if (isLowBattery())    drawLowBatteryWarning();
  else if (metricsActive) drawMetricsScreen();
  else                    drawClockScreen();
  display.display();
}

// ── setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200); delay(1000);
  enableI2CPower(); enableNeoPixelPower();
  pixel.begin(); pixel.setBrightness(LED_BRIGHTNESS); setPixel(0, 0, 0);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true) { setPixel(10, 0, 0); delay(100); setPixel(0, 0, 0); delay(100); }
  }
  display.clearDisplay(); display.display();

  batteryOK = maxlipo.begin(&Wire);
  if (batteryOK) { maxlipo.quickStart(); delay(250); refreshBattery(); }

  // Show startup splash
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
  centerText("Connecting", 16, 1); centerText("WiFi...", 32, 2); display.display();
  connectWiFi();
  // Clear splash before NTP sync so drawScreen() during sync shows the clock
  display.clearDisplay(); display.display();
  if (wifiConnected) syncTimeFromNTP();

  // First metrics view will appear METRICS_INTERVAL_MS after boot
  lastMetricsStart = millis();

  drawScreen(); updateStatusLED();
}

// ── loop ──────────────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  // ── metrics timing ────────────────────────────────────────────────────────
  // Activate metrics once per METRICS_INTERVAL_MS; keep active for METRICS_DURATION_MS.
  // After it hides, the 60-second window restarts from when it was shown —
  // giving a clean ~60 s clock → 10 s metrics cycle.
  if (!metricsActive && (now - lastMetricsStart >= METRICS_INTERVAL_MS)) {
    metricsActive    = true;
    lastMetricsStart = now;
  }
  if (metricsActive && (now - lastMetricsStart >= METRICS_DURATION_MS)) {
    metricsActive = false;
    // lastMetricsStart stays so the next trigger is METRICS_INTERVAL_MS later
  }

  // ── wifi reconnect ────────────────────────────────────────────────────────
  if (!wifiConnected && !wifiConnecting && (now - lastReconnectAttempt >= WIFI_RETRY_INTERVAL_MS)) {
    lastReconnectAttempt = now;
    connectWiFi(8000);
    if (wifiConnected) syncTimeFromNTP(5000);
  }

  // ── battery refresh ───────────────────────────────────────────────────────
  if (now - lastBatteryRead >= BATTERY_READ_INTERVAL_MS) { lastBatteryRead = now; refreshBattery(); }

  // ── ntp resync ────────────────────────────────────────────────────────────
  if (wifiConnected && (!timeReady || (now - lastNtpSyncMs >= NTP_RESYNC_INTERVAL_MS)))
    syncTimeFromNTP(5000);

  // ── draw ──────────────────────────────────────────────────────────────────
  if (now - lastDrawMs >= DRAW_INTERVAL_MS) { lastDrawMs = now; drawScreen(); }

  updateStatusLED();
  delay(10);
}
