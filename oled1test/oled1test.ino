// OLED Single-Screen Cycle Test
// Adafruit Feather ESP32-S3 | SDA=3 SCL=4 OLED=0x3C
// Cycles 3 screens every 7 s: heart animation → temp/humidity → battery
// No WiFi, no time.
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
#define SDA_PIN          3
#define SCL_PIN          4

#define HEADER_H            16
#define LED_BRIGHTNESS      35
#define FRAME_DELAY         42
#define FRAME_WIDTH         48
#define FRAME_HEIGHT        48
#define LOW_BATTERY_PERCENT  5
#define SCREEN_SWITCH_INTERVAL_MS 7000UL
#define BATTERY_READ_INTERVAL_MS  10000UL
#define SENSOR_READ_INTERVAL_MS   5000UL

const byte PROGMEM frames[][288] = {
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,63,224,7,252,0,0,255,248,31,255,0,1,192,60,60,7,192,7,128,14,112,1,224,6,0,7,224,0,224,14,0,3,192,0,112,28,0,1,128,0,56,24,0,0,0,0,24,56,0,0,0,0,24,56,0,0,0,0,12,48,0,0,0,0,12,48,0,0,0,0,12,48,0,0,0,0,12,48,0,0,0,0,12,48,0,0,0,0,12,48,0,0,0,0,28,56,0,0,0,0,28,24,0,0,0,0,24,24,0,0,0,0,56,12,0,0,0,0,56,12,0,0,0,0,112,6,0,0,0,0,112,7,0,0,0,0,224,3,128,0,0,1,192,1,128,0,0,1,192,1,192,0,0,3,128,0,240,0,0,7,0,0,120,0,0,30,0,0,60,0,0,60,0,0,30,0,0,120,0,0,7,128,0,224,0,0,3,192,3,192,0,0,1,240,7,128,0,0,0,120,30,0,0,0,0,60,60,0,0,0,0,15,240,0,0,0,0,7,224,0,0,0,0,3,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,224,7,248,0,0,127,248,31,254,0,0,240,62,120,15,0,1,192,14,240,3,128,3,128,3,192,1,192,7,0,3,192,0,224,6,0,1,128,0,96,14,0,0,0,0,48,12,0,0,0,0,48,12,0,0,0,0,48,12,0,0,0,0,48,12,0,0,0,0,48,12,0,0,0,0,48,12,0,0,0,0,48,12,0,0,0,0,48,12,0,0,128,0,48,12,0,1,128,0,112,6,0,0,0,0,96,6,0,0,0,0,224,3,0,0,0,0,224,3,128,0,0,1,192,1,128,0,0,1,128,0,192,0,0,3,128,0,224,0,0,7,0,0,112,0,0,14,0,0,56,0,0,28,0,0,30,0,0,56,0,0,15,0,0,240,0,0,3,128,1,192,0,0,1,224,7,128,0,0,0,240,15,0,0,0,0,60,28,0,0,0,0,30,120,0,0,0,0,7,224,0,0,0,0,3,192,0,0,0,0,1,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,224,3,224,0,0,31,248,31,248,0,0,120,30,120,30,0,0,224,15,224,7,0,1,192,3,192,3,128,1,128,1,128,1,128,3,0,0,128,0,192,3,0,0,0,0,192,7,0,0,0,0,64,6,0,0,0,0,96,6,0,0,0,0,96,6,0,2,32,0,96,6,0,15,240,0,96,2,0,31,248,0,96,2,0,31,248,0,192,3,0,15,240,0,192,3,0,15,240,1,192,1,128,7,224,1,128,1,128,3,192,3,128,0,192,0,0,3,0,0,96,0,0,7,0,0,112,0,0,14,0,0,56,0,0,28,0,0,28,0,0,56,0,0,14,0,0,112,0,0,7,128,0,224,0,0,1,192,3,128,0,0,0,224,7,0,0,0,0,120,30,0,0,0,0,28,56,0,0,0,0,15,240,0,0,0,0,3,192,0,0,0,0,1,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,0,0,64,0,0,63,128,0,252,0,7,255,240,7,255,192,7,243,253,159,159,224,15,128,63,252,1,240,30,0,14,240,0,120,60,0,7,224,0,60,56,0,3,192,0,28,112,0,1,128,0,14,112,0,0,0,0,6,224,0,0,0,0,7,224,0,252,31,128,3,192,3,254,127,192,3,192,7,255,255,224,3,192,15,255,255,224,3,192,15,255,255,240,3,192,15,255,255,240,3,192,15,255,255,240,3,192,15,255,255,240,7,224,15,255,255,240,7,96,7,255,255,240,7,96,7,255,255,224,14,112,3,255,255,224,14,48,3,255,255,192,28,56,1,255,255,128,28,28,0,255,255,0,56,28,0,127,254,0,120,14,0,63,252,0,112,7,0,31,240,0,224,7,128,7,224,1,192,15,192,3,128,3,128,12,224,0,0,7,0,0,120,0,0,14,0,0,60,0,0,60,8,0,30,0,0,124,12,0,111,128,0,254,0,0,99,192,3,196,0,0,1,240,7,128,0,0,0,248,31,128,0,0,0,252,61,128,0,0,0,159,240,0,0,0,0,7,224,0,0,0,0,3,192,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,32,0,0,6,0,0,96,0,0,6,1,128,96,0,14,0,1,128,64,224,6,0,1,128,0,224,0,63,225,135,252,0,0,255,248,31,255,0,1,255,252,63,255,192,7,255,254,127,255,224,7,255,255,255,255,224,15,255,255,255,255,240,31,255,255,255,255,248,31,255,255,255,255,248,63,255,255,255,255,248,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,31,255,255,255,255,248,31,255,255,255,255,248,15,255,255,255,255,248,15,255,255,255,255,240,7,255,255,255,255,240,7,255,255,255,255,224,3,255,255,255,255,192,1,255,255,255,255,192,1,255,255,255,255,128,0,255,255,255,255,0,12,127,255,255,254,0,28,63,255,255,252,16,16,31,255,255,248,24,0,7,255,255,224,24,0,3,255,255,192,0,0,1,255,255,130,0,0,224,127,254,7,0,0,192,63,252,3,0,0,128,15,240,0,0,0,0,135,225,128,0,0,1,195,129,192,0,0,1,128,0,128,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,63,224,7,252,0,0,255,248,31,255,0,1,255,252,63,255,192,7,255,254,127,255,224,7,255,255,255,255,224,15,255,255,255,255,240,31,255,255,255,255,248,31,255,255,255,255,248,63,255,255,255,255,248,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,31,255,255,255,255,248,31,255,255,255,255,248,15,255,255,255,255,248,15,255,255,255,255,240,7,255,255,255,255,240,7,255,255,255,255,224,3,255,255,255,255,192,1,255,255,255,255,192,1,255,255,255,255,128,0,255,255,255,255,0,0,127,255,255,254,0,0,63,255,255,252,0,0,31,255,255,248,0,0,7,255,255,224,0,0,3,255,255,192,0,0,1,255,255,128,0,0,0,127,254,0,0,0,0,63,252,0,0,0,0,15,240,0,0,0,0,7,224,0,0,0,0,3,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,63,128,0,252,0,0,255,240,7,255,128,3,255,252,31,255,224,15,255,254,127,255,240,31,255,254,255,255,248,63,255,255,255,255,252,63,255,255,255,255,252,127,255,255,255,255,254,127,255,255,255,255,254,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,127,255,255,255,255,255,127,255,255,255,255,254,127,255,255,255,255,254,63,255,255,255,255,252,63,255,255,255,255,252,31,255,255,255,255,248,31,255,255,255,255,248,15,255,255,255,255,240,7,255,255,255,255,224,3,255,255,255,255,192,1,255,255,255,255,128,0,255,255,255,255,0,0,127,255,255,254,0,0,63,255,255,252,0,0,31,255,255,248,0,0,15,255,255,240,0,0,3,255,255,192,0,0,1,255,255,128,0,0,0,255,254,0,0,0,0,63,252,0,0,0,0,31,240,0,0,0,0,7,224,0,0,0,0,3,192,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,192,1,240,0,0,127,240,15,254,0,0,240,252,62,31,128,3,192,30,112,3,192,7,0,7,224,1,224,6,0,3,192,0,96,14,0,1,128,0,48,28,1,252,31,128,56,24,7,254,127,224,24,24,15,255,255,240,24,24,15,255,255,240,24,56,31,255,255,248,24,56,31,255,255,248,28,24,31,255,255,248,28,24,31,255,255,248,24,24,31,255,255,248,24,24,31,255,255,248,24,24,31,255,255,248,56,12,15,255,255,240,56,12,15,255,255,240,112,6,7,255,255,224,112,7,3,255,255,192,224,3,3,255,255,192,224,3,129,255,255,1,192,1,192,127,254,3,128,0,224,63,252,7,0,0,112,31,248,14,0,0,56,7,224,28,0,0,30,3,192,56,0,0,15,0,0,240,0,0,7,128,1,192,0,0,1,224,7,128,0,0,0,240,15,0,0,0,0,60,28,0,0,0,0,30,120,0,0,0,0,15,224,0,0,0,0,3,192,0,0,0,0,1,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,63,224,7,252,0,0,255,248,31,255,0,1,192,60,60,7,192,7,128,14,112,1,224,6,0,7,224,0,224,14,0,3,192,0,112,28,0,1,128,0,56,24,0,0,0,0,24,56,0,0,0,0,24,56,0,0,0,0,12,48,0,0,0,0,12,48,0,0,0,0,12,48,0,6,48,0,12,48,0,31,248,0,12,48,0,63,252,0,12,48,0,63,252,0,28,56,0,63,252,0,28,24,0,31,252,0,24,24,0,31,248,0,56,12,0,15,240,0,56,12,0,7,224,0,112,6,0,3,192,0,112,7,0,1,0,0,224,3,128,0,0,1,192,1,128,0,0,1,192,1,192,0,0,3,128,0,240,0,0,7,0,0,120,0,0,30,0,0,60,0,0,60,0,0,30,0,0,120,0,0,7,128,0,224,0,0,3,192,3,192,0,0,1,240,7,128,0,0,0,120,30,0,0,0,0,60,60,0,0,0,0,15,240,0,0,0,0,7,224,0,0,0,0,3,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};
#define FRAME_COUNT (sizeof(frames)/sizeof(frames[0]))

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MAX17048 maxlipo;
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
Adafruit_SHT4x sht4;

bool batteryOK = false;
int  batteryPercent = -1;
float batteryVoltage = -1.0f;
bool sht4OK = false;
float lastTempF = -999.0f, lastHumidity = -999.0f;
int heartFrame = 0;
int screenMode = 0; // 0=heart  1=temp/humidity  2=battery

unsigned long lastBatteryRead = 0, lastSensorRead = 0;
unsigned long lastDrawMs = 0, lastScreenSwitch = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void enableI2CPower() {
#ifdef PIN_I2C_POWER
  pinMode(PIN_I2C_POWER, OUTPUT);
  digitalWrite(PIN_I2C_POWER, HIGH);
#endif
}

void enableNeoPixelPower() {
#ifdef NEOPIXEL_POWER
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
#endif
}

void setPixel(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void centerText(const String& t, int y, int s) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(s);
  display.getTextBounds(t, 0, y, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, y);
  display.print(t);
}

void centerText(const String& t, int y, int s) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(s);
  display.getTextBounds(t, 0, y, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, y);
  display.print(t);
}

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

void drawHeader() {
  display.fillRect(0, 0, SCREEN_WIDTH, HEADER_H, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  drawBatteryIcon(4, 3, 18, 9, batteryPercent);
  display.setTextSize(1);
  display.setCursor(28, 4);
  if (batteryPercent >= 0) { display.print(batteryPercent); display.print("%"); }
  else display.print("--%");
}

// ---------------------------------------------------------------------------
// Screen draw functions
// ---------------------------------------------------------------------------

void drawScreen0() { // heart animation
  display.clearDisplay();
  drawHeader();
  display.drawBitmap(40, HEADER_H, frames[heartFrame], FRAME_WIDTH, FRAME_HEIGHT, SSD1306_WHITE);
  heartFrame = (heartFrame + 1) % FRAME_COUNT;
  display.display();
}

void drawScreen1() { // temperature & humidity
  display.clearDisplay();
  drawHeader();
  display.setTextColor(SSD1306_WHITE);
  centerText("INDOOR", 18, 1);
  if (!sht4OK) {
    centerText("No sensor", 34, 1);
  } else if (lastTempF < -998.0f) {
    centerText("Reading...", 34, 1);
  } else {
    char tempStr[10]; dtostrf(lastTempF, 4, 1, tempStr);
    int16_t x1, y1; uint16_t numW, numH, fW, fH;
    display.setTextSize(2);
    display.getTextBounds(tempStr, 0, 0, &x1, &y1, &numW, &numH);
    display.getTextBounds("F", 0, 0, &x1, &y1, &fW, &fH);
    const int degGap = 8;
    int totalW = (int)numW + degGap + (int)fW;
    int sx = (SCREEN_WIDTH - totalW) / 2;
    const int ty = 27;
    display.setCursor(sx, ty); display.print(tempStr);
    display.drawCircle(sx + (int)numW + 3, ty + 3, 2, SSD1306_WHITE);
    display.setCursor(sx + (int)numW + degGap, ty); display.print("F");
    char humStr[14]; snprintf(humStr, sizeof(humStr), "Hum: %.0f%%", lastHumidity);
    uint16_t humW, humH;
    display.setTextSize(1);
    display.getTextBounds(humStr, 0, 0, &x1, &y1, &humW, &humH);
    display.setCursor((SCREEN_WIDTH - (int)humW) / 2, 50);
    display.print(humStr);
  }
  display.display();
}

void drawScreen2() { // battery
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  const int bx = 8, by = 4, bw = 96, bh = 28;
  display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
  int tipH = bh / 2, tipY = by + (bh - tipH) / 2;
  display.fillRect(bx + bw, tipY, 5, tipH, SSD1306_WHITE);
  int fill = (batteryPercent >= 0) ? ((bw - 6) * constrain(batteryPercent, 0, 100)) / 100 : 0;
  if (fill > 0) display.fillRect(bx + 3, by + 3, fill, bh - 6, SSD1306_WHITE);
  if (batteryPercent < 0) {
    display.drawLine(bx + 3, by + 3, bx + bw - 4, by + bh - 4, SSD1306_WHITE);
    display.drawLine(bx + bw - 4, by + 3, bx + 3, by + bh - 4, SSD1306_WHITE);
  }
  display.setTextSize(2);
  char pctStr[8];
  if (batteryPercent >= 0) snprintf(pctStr, sizeof(pctStr), "%d%%", batteryPercent);
  else strncpy(pctStr, "--%", sizeof(pctStr));
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(pctStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, by + (bh - (int)h) / 2);
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.print(pctStr);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  char vStr[16];
  if (batteryVoltage > 0.0f) snprintf(vStr, sizeof(vStr), "%.2fV", batteryVoltage);
  else strncpy(vStr, "-.--V", sizeof(vStr));
  display.setCursor(bx, by + bh + 6);
  display.print(vStr);
  display.display();
}

// ---------------------------------------------------------------------------
// Sensor helpers
// ---------------------------------------------------------------------------

void refreshBattery() {
  if (!batteryOK) return;
  float p = maxlipo.cellPercent(), v = maxlipo.cellVoltage();
  batteryPercent = isnan(p) ? -1 : constrain((int)(p + 0.5f), 0, 100);
  batteryVoltage = isnan(v) ? -1.0f : v;
}

void refreshSensor() {
  if (!sht4OK) return;
  sensors_event_t humidity, temp;
  sht4.getEvent(&humidity, &temp);
  lastTempF    = (temp.temperature * 9.0f / 5.0f) + 32.0f;
  lastHumidity = humidity.relative_humidity;
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  enableI2CPower();
  enableNeoPixelPower();
  pixel.begin();
  pixel.setBrightness(LED_BRIGHTNESS);
  setPixel(0, 0, 0);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 not found — check wiring");
    while (true) { setPixel(10, 0, 0); delay(200); setPixel(0, 0, 0); delay(200); }
  }

  batteryOK = maxlipo.begin(&Wire);
  if (batteryOK) { maxlipo.quickStart(); delay(250); refreshBattery(); }

  sht4OK = sht4.begin(&Wire);
  if (sht4OK) { sht4.setPrecision(SHT4X_HIGH_PRECISION); refreshSensor(); }

  drawScreen0();
}

void loop() {
  unsigned long now = millis();

  if (now - lastScreenSwitch >= SCREEN_SWITCH_INTERVAL_MS) {
    lastScreenSwitch = now;
    screenMode = (screenMode + 1) % 3;
  }

  if (now - lastBatteryRead >= BATTERY_READ_INTERVAL_MS) { lastBatteryRead = now; refreshBattery(); }
  if (now - lastSensorRead  >= SENSOR_READ_INTERVAL_MS)  { lastSensorRead  = now; refreshSensor();  }

  if (now - lastDrawMs >= FRAME_DELAY) {
    lastDrawMs = now;
    switch (screenMode) {
      case 0: drawScreen0(); break;
      case 1: drawScreen1(); break;
      case 2: drawScreen2(); break;
    }
  }

  delay(10);
}
