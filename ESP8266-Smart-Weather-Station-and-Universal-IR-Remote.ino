#include <TFT_eSPI.h>
#include <SPI.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"

#define DECODE_NEC
#define DECODE_SAMSUNG
#define DECODE_SONY

using namespace fs;

// ─── CONFIG ───────────────────────────────
const char* WIFI_SSID = "WIFI_SSID";
const char* WIFI_PASS = "WIFI_PASSWORD";
const char* CITY      = "CITY";
const char* COUNTRY   = "COUNTRY";
const char* API_KEY   = "OPENWEATHER_API";
#define WEATHER_INTERVAL 600000UL

// ─── PINS ─────────────────────────────────
#define IR_RECV_PIN 2
#define IR_SEND_PIN 5
#define CAL_FILE    "/touch.cal"
#define IR_SAVE_FILE "/irsave.dat"

// ─── OBJECTS ──────────────────────────────
TFT_eSPI tft = TFT_eSPI();
IRrecv irrecv(IR_RECV_PIN, 100);
IRsend irsend(IR_SEND_PIN);
decode_results results;

// ─── TABS ─────────────────────────────────
#define TAB_CLOCK 0
#define TAB_IR    1
int currentTab = TAB_CLOCK;

// ─── WEATHER ──────────────────────────────
char weatherDesc[24]            = "Loading...";
int  weatherTemp                = 0;
int  weatherHumidity            = 0;
int  weatherTempMax             = 0;
bool weatherLoaded              = false;
unsigned long lastWeatherUpdate = 0;

// ─── IR ───────────────────────────────────
struct IRSignal {
  uint64_t      value;
  decode_type_t protocol;
  uint16_t      bits;
  char          name[12];
};
IRSignal saved[5];
int  savedCount    = 0;
int  selectedIndex = -1;
bool capturing     = false;
bool needRedraw    = false;

// ─── TIME ─────────────────────────────────
int lastMinute = -1;

// ─── KEYBOARD ─────────────────────────────
const char* kb_rows[] = {
  "1234567890",
  "QWERTYUIOP",
  "ASDFGHJKL",
  "ZXCVBNM"
};
char kbInput[12] = "";
int  kbLen       = 0;
#define KEY_W 22
#define KEY_H 23
#define KEY_G  2

// ─────────────────────────────────────────
// CALIBRATION
// ─────────────────────────────────────────
void calibrateTouch() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.print(F("Calibration"));
  tft.setTextSize(1);
  tft.setCursor(10, 140);
  tft.print(F("Touch each cross with fingernail"));
  delay(2000);

  uint16_t calData[5];
  tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);

  SPIFFS.begin();
  fs::File f = SPIFFS.open(CAL_FILE, "w");
  if (f) { f.write((uint8_t*)calData, 10); f.close(); }
  tft.setTouch(calData);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, 130);
  tft.print(F("Saved!"));
  delay(1500);
}

bool loadCalibration() {
  SPIFFS.begin();
  if (SPIFFS.exists(CAL_FILE)) {
    fs::File f = SPIFFS.open(CAL_FILE, "r");
    if (f) {
      uint16_t calData[5];
      f.read((uint8_t*)calData, 10);
      f.close();
      tft.setTouch(calData);
      return true;
    }
  }
  return false;
}

// ─────────────────────────────────────────
// IR SPIFFS SAVE/LOAD
// ─────────────────────────────────────────
void saveIRSignals() {
  fs::File f = SPIFFS.open(IR_SAVE_FILE, "w");
  if (f) {
    f.write((uint8_t*)&savedCount, sizeof(savedCount));
    f.write((uint8_t*)saved, sizeof(IRSignal) * savedCount);
    f.close();
  }
}

void loadIRSignals() {
  if (SPIFFS.exists(IR_SAVE_FILE)) {
    fs::File f = SPIFFS.open(IR_SAVE_FILE, "r");
    if (f) {
      f.read((uint8_t*)&savedCount, sizeof(savedCount));
      if (savedCount > 5) savedCount = 0;
      f.read((uint8_t*)saved, sizeof(IRSignal) * savedCount);
      f.close();
    }
  }
}

// ─────────────────────────────────────────
// KEYBOARD
// ─────────────────────────────────────────
void drawKey(int x, int y, const char* label, uint16_t color) {
  tft.fillRoundRect(x, y, KEY_W, KEY_H, 3, color);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(x + 7, y + 8);
  tft.print(label);
}

void drawKeyboard() {
  tft.fillRect(0, 155, 240, 165, TFT_BLACK);

  tft.fillRoundRect(5, 158, 230, 22, 4, TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 166);
  tft.print(kbInput);
  tft.print(F("_"));

  // Number row
  for (int i = 0; i < 10; i++) {
    char label[2] = { kb_rows[0][i], 0 };
    drawKey(2 + i * (KEY_W + KEY_G), 183, label, 0x4208);
  }

  // QWERTY row
  for (int i = 0; i < 10; i++) {
    char label[2] = { kb_rows[1][i], 0 };
    drawKey(2 + i * (KEY_W + KEY_G), 208, label, TFT_DARKGREY);
  }

  // ASDFGHJKL row
  for (int i = 0; i < 9; i++) {
    char label[2] = { kb_rows[2][i], 0 };
    drawKey(13 + i * (KEY_W + KEY_G), 233, label, TFT_DARKGREY);
  }

  // DEL
  tft.fillRoundRect(2, 258, 33, KEY_H, 3, TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 266);
  tft.print(F("DEL"));

  // ZXCVBNM row
  for (int i = 0; i < 7; i++) {
    char label[2] = { kb_rows[3][i], 0 };
    drawKey(37 + i * (KEY_W + KEY_G), 258, label, TFT_DARKGREY);
  }

  // SPACE
  tft.fillRoundRect(2, 283, 150, KEY_H, 3, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(55, 291);
  tft.print(F("SPACE"));

  // OK
  tft.fillRoundRect(156, 283, 82, KEY_H, 3, TFT_GREEN);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(185, 291);
  tft.print(F("OK"));
}

void updateInputBox() {
  tft.fillRoundRect(5, 158, 230, 22, 4, TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 166);
  tft.print(kbInput);
  tft.print(F("_"));
}

char getKeyboardInput() {
  uint16_t tx, ty;
  if (!tft.getTouch(&tx, &ty)) return 0;
  delay(150);

  if (ty >= 183 && ty <= 205) {
    int i = (tx - 2) / (KEY_W + KEY_G);
    if (i >= 0 && i < 10) return kb_rows[0][i];
  }
  if (ty >= 208 && ty <= 230) {
    int i = (tx - 2) / (KEY_W + KEY_G);
    if (i >= 0 && i < 10) return kb_rows[1][i];
  }
  if (ty >= 233 && ty <= 255) {
    int i = (tx - 13) / (KEY_W + KEY_G);
    if (i >= 0 && i < 9) return kb_rows[2][i];
  }
  if (ty >= 258 && ty <= 280) {
    if (tx < 35) return '\b';
    int i = (tx - 37) / (KEY_W + KEY_G);
    if (i >= 0 && i < 7) return kb_rows[3][i];
  }
  if (ty >= 283 && ty <= 305) {
    if (tx < 155) return ' ';
    return '\n';
  }
  return 0;
}

void nameSignal(int index) {
  memset(kbInput, 0, sizeof(kbInput));
  kbLen = 0;

  tft.fillRect(0, 148, 240, 10, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(5, 150);
  tft.print(F("Name this signal:"));

  drawKeyboard();

  while (true) {
    char k = getKeyboardInput();
    if (k == '\n') {
      if (kbLen == 0) {
        snprintf(saved[index].name, 12, "Signal%d", index + 1);
      } else {
        strncpy(saved[index].name, kbInput, 11);
        saved[index].name[11] = 0;
      }
      break;
    }
    if (k == '\b') {
      if (kbLen > 0) {
        kbLen--;
        kbInput[kbLen] = 0;
        updateInputBox();
      }
    } else if (k != 0 && kbLen < 11) {
      kbInput[kbLen++] = k;
      kbInput[kbLen]   = 0;
      updateInputBox();
    }
  }
  tft.fillRect(0, 148, 240, 175, TFT_BLACK);
}

// ─────────────────────────────────────────
// WIFI
// ─────────────────────────────────────────
void connectWiFi() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 10);
  tft.print(F("Connecting to WiFi..."));

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    tft.print(F("."));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    tft.setCursor(5, 30);
    tft.setTextColor(TFT_GREEN);
    tft.print(F("Connected!"));
    configTime(19800, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

    tft.setCursor(5, 50);
    tft.setTextColor(TFT_YELLOW);
    tft.print(F("Syncing time..."));

    attempts = 0;
    time_t now = time(nullptr);
    while (now < 1000000000UL && attempts < 40) {
      delay(500);
      now = time(nullptr);
      attempts++;
      tft.print(F("."));
    }

    tft.setCursor(5, 70);
    if (now > 1000000000UL) {
      tft.setTextColor(TFT_GREEN);
      tft.print(F("Time synced!"));
    } else {
      tft.setTextColor(TFT_RED);
      tft.print(F("Time sync failed."));
    }
    delay(1000);
  } else {
    tft.setCursor(5, 30);
    tft.setTextColor(TFT_RED);
    tft.print(F("WiFi failed. Clock only."));
    delay(2000);
  }
}

void reconnectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    attempts++;
  }
}

// ─────────────────────────────────────────
// WEATHER
// ─────────────────────────────────────────
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;
  char url[180];
  snprintf(url, sizeof(url),
    "http://api.openweathermap.org/data/2.5/weather?q=%s,%s&appid=%s&units=metric",
    CITY, COUNTRY, API_KEY);

  http.begin(client, url);
  http.setTimeout(5000);
  int code = http.GET();

  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<768> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      const char* desc = doc["weather"][0]["description"] | "unknown";
      strncpy(weatherDesc, desc, sizeof(weatherDesc) - 1);
      weatherDesc[0]  = toupper(weatherDesc[0]);
      weatherTemp     = (int)round((float)doc["main"]["temp"]);
      weatherHumidity = (int)doc["main"]["humidity"];
      weatherTempMax  = (int)round((float)doc["main"]["temp_max"]);
      weatherLoaded   = true;
    }
  }
  http.end();
  lastWeatherUpdate = millis();
}

// ─────────────────────────────────────────
// CLOCK TAB
// ─────────────────────────────────────────
void drawTabBar() {
  uint16_t cCol = (currentTab == TAB_CLOCK) ? TFT_ORANGE : TFT_DARKGREY;
  uint16_t iCol = (currentTab == TAB_IR)    ? 0x001F    : TFT_DARKGREY;

  tft.fillRoundRect(2,   2, 116, 26, 4, cCol);
  tft.fillRoundRect(122, 2, 116, 26, 4, iCol);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(38, 10);
  tft.print(F("CLOCK"));
  tft.setCursor(148, 10);
  tft.print(F("IR TOOL"));
}

void getTime12(int hour, int min, char* timeStr, char* ampmStr) {
  int h = hour % 12;
  if (h == 0) h = 12;
  snprintf(timeStr, 6, "%02d:%02d", h, min);
  strcpy(ampmStr, hour < 12 ? "AM" : "PM");
}

void drawClockFull() {
  tft.fillRect(0, 30, 240, 290, TFT_BLACK);

  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  lastMinute = t->tm_min;

  // City + badge
  tft.setTextColor(TFT_ORANGE);
  tft.setTextSize(2);
  tft.setCursor(8, 38);
  tft.print(F("Yelahanka"));

  tft.fillRoundRect(170, 36, 32, 18, 4, TFT_GREEN);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(175, 42);
  tft.print(F("IN"));

  tft.fillCircle(218, 44, 10, TFT_ORANGE);

  // Max temp
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 60);
  if (weatherLoaded) {
    tft.print(F("Max:"));
    tft.print(weatherTempMax);
    tft.print(F("C"));
  } else {
    tft.setTextSize(1);
    tft.print(F("Fetching weather..."));
  }

  // Clock
  char timeStr[6];
  char ampmStr[3];
  getTime12(t->tm_hour, t->tm_min, timeStr, ampmStr);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(5);
  tft.setCursor(8, 82);
  tft.print(timeStr);

  // AM/PM badge
  tft.fillRoundRect(182, 82, 50, 24, 4, TFT_NAVY);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(190, 89);
  tft.print(ampmStr);

  // Weather badge
  tft.fillRoundRect(182, 112, 50, 20, 4, TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  char badge[9];
  strncpy(badge, weatherDesc, 8);
  badge[8] = 0;
  tft.setCursor(185, 119);
  tft.print(badge);

  // Date
  const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  char dateStr[12];
  snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d",
    t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 142);
  tft.print(dateStr);

  tft.setTextColor(TFT_GREEN);
  tft.setCursor(8, 164);
  tft.print(days[t->tm_wday]);

  tft.drawFastHLine(0, 185, 240, TFT_DARKGREY);

  // Temp bar
  tft.setTextColor(TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(4, 191);
  tft.print(F("*"));
  tft.fillRoundRect(28, 196, 150, 12, 4, TFT_DARKGREY);
  if (weatherLoaded) {
    int tw = constrain(map(weatherTemp, 0, 50, 0, 150), 0, 150);
    tft.fillRoundRect(28, 196, tw, 12, 4, 0x03EF);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(184, 191);
    tft.print(weatherTemp);
    tft.print(F("C"));
  }

  // Humidity bar
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(4, 214);
  tft.print(F("~"));
  tft.fillRoundRect(28, 219, 150, 12, 4, TFT_DARKGREY);
  if (weatherLoaded) {
    int hw = constrain(map(weatherHumidity, 0, 100, 0, 150), 0, 150);
    tft.fillRoundRect(28, 219, hw, 12, 4, TFT_CYAN);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(184, 214);
    tft.print(weatherHumidity);
    tft.print(F("%"));
  }

  tft.drawFastHLine(0, 238, 240, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setCursor(8, 244);
  tft.print(weatherLoaded ? F("Weather OK | Tap IR tab") : F("No weather | Check WiFi"));
}

void updateClockTime() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);

  if (t->tm_min != lastMinute) {
    lastMinute = t->tm_min;
    tft.fillRect(8, 82, 170, 60, TFT_BLACK);

    char timeStr[6];
    char ampmStr[3];
    getTime12(t->tm_hour, t->tm_min, timeStr, ampmStr);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(5);
    tft.setCursor(8, 82);
    tft.print(timeStr);

    tft.fillRoundRect(182, 82, 50, 24, 4, TFT_NAVY);
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(2);
    tft.setCursor(190, 89);
    tft.print(ampmStr);
  }
}

// ─────────────────────────────────────────
// IR TAB
// ─────────────────────────────────────────
void drawIRStatus(const char* msg, uint16_t color) {
  tft.fillRect(0, 308, 240, 12, TFT_BLACK);
  tft.setTextColor(color);
  tft.setTextSize(1);
  tft.setCursor(5, 309);
  tft.print(msg);
}

void drawIRSignalList() {
  tft.fillRect(0, 182, 240, 125, TFT_BLACK);
  tft.setTextSize(1);

  if (savedCount == 0) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(5, 192);
    tft.print(F("None. Point remote and capture."));
    return;
  }

  for (int i = 0; i < savedCount && i < 5; i++) {
    if (i == selectedIndex) {
      tft.fillRect(0, 182 + (i * 22), 240, 21, TFT_NAVY);
      tft.setTextColor(TFT_YELLOW);
    } else {
      tft.setTextColor(TFT_CYAN);
    }
    tft.setCursor(5, 185 + (i * 22));
    tft.print(i + 1);
    tft.print(F(". "));
    tft.print(saved[i].name);
    tft.setTextColor(i == selectedIndex ? TFT_WHITE : TFT_DARKGREY);
    tft.setCursor(5, 194 + (i * 22));
    tft.print(F("P:"));
    tft.print(saved[i].protocol);
    tft.print(F(" 0x"));
    tft.print((uint32_t)saved[i].value, HEX);
  }
}

void drawIRTab() {
  tft.fillRect(0, 30, 240, 290, TFT_BLACK);

  tft.fillRoundRect(10,  38, 105, 45, 8, TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(18, 53);
  tft.print(F("CAPTURE"));

  tft.fillRoundRect(125, 38, 105, 45, 8, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(148, 53);
  tft.print(F("SEND"));

  tft.fillRoundRect(10,  90, 105, 35, 8, TFT_RED);
  tft.setTextSize(1);
  tft.setCursor(28, 104);
  tft.print(F("CLEAR ALL"));

  tft.fillRoundRect(125, 90, 105, 35, 8, TFT_DARKGREY);
  tft.setCursor(143, 104);
  tft.print(F("DUMP ALL"));

  tft.drawFastHLine(0, 133, 240, TFT_DARKGREY);
  tft.setTextColor(0xFD20);
  tft.setTextSize(1);
  tft.setCursor(5, 138);
  tft.print(F("WiFi off in IR mode"));

  tft.drawFastHLine(0, 150, 240, TFT_DARKGREY);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(5, 155);
  tft.print(F("Tap signal to select then SEND:"));

  drawIRSignalList();
  drawIRStatus("Ready", TFT_GREEN);
}

void highlightCapture(bool active) {
  if (active) {
    tft.fillRoundRect(10, 38, 105, 45, 8, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(14, 57);
    tft.print(F("LISTENING.."));
  } else {
    tft.fillRoundRect(10, 38, 105, 45, 8, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(18, 53);
    tft.print(F("CAPTURE"));
  }
}

void highlightSend(bool active) {
  if (active) {
    tft.fillRoundRect(125, 38, 105, 45, 8, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(148, 53);
    tft.print(F("SENT!"));
  } else {
    tft.fillRoundRect(125, 38, 105, 45, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(148, 53);
    tft.print(F("SEND"));
  }
}

// ─────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.print(F("Heap start: "));
  Serial.println(ESP.getFreeHeap());

  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);

  if (!loadCalibration()) {
    calibrateTouch();
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 80);
    tft.print(F("Cal found!"));
    tft.setTextSize(1);
    tft.setCursor(10, 120);
    tft.print(F("Touch to recalibrate"));
    tft.setCursor(10, 135);
    tft.print(F("Wait 3 sec to skip..."));

    unsigned long s = millis();
    bool recal = false;
    while (millis() - s < 3000) {
      uint16_t tx, ty;
      if (tft.getTouch(&tx, &ty)) { recal = true; break; }
    }
    if (recal) calibrateTouch();
  }

  loadIRSignals();
  connectWiFi();
  fetchWeather();
  irsend.begin();

  Serial.print(F("Heap after init: "));
  Serial.println(ESP.getFreeHeap());

  tft.fillScreen(TFT_BLACK);
  drawTabBar();
  drawClockFull();
}

// ─────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────
void loop() {
  uint16_t tx, ty;

  if (currentTab == TAB_CLOCK) {
    updateClockTime();
    if (millis() - lastWeatherUpdate > WEATHER_INTERVAL) {
      reconnectWiFi();
      fetchWeather();
      drawClockFull();
    }
  }

  if (tft.getTouch(&tx, &ty)) {
    delay(200);

    // Tab switch
    if (ty < 30) {
      if (tx < 120 && currentTab != TAB_CLOCK) {
        irrecv.disableIRIn();
        capturing  = false;
        currentTab = TAB_CLOCK;
        reconnectWiFi();
        tft.fillScreen(TFT_BLACK);
        drawTabBar();
        drawClockFull();
      } else if (tx >= 120 && currentTab != TAB_IR) {
        WiFi.disconnect();
        currentTab = TAB_IR;
        irrecv.enableIRIn();
        tft.fillScreen(TFT_BLACK);
        drawTabBar();
        drawIRTab();
      }
      return;
    }

    if (currentTab == TAB_IR) {

      // CAPTURE
      if (tx > 10 && tx < 115 && ty > 38 && ty < 83) {
        if (savedCount >= 5) {
          drawIRStatus("Max 5. Clear first.", TFT_RED);
        } else {
          capturing = true;
          highlightCapture(true);
          drawIRStatus("Point remote and press...", TFT_YELLOW);
        }
      }

      // SEND
      else if (tx > 125 && tx < 230 && ty > 38 && ty < 83) {
        if (selectedIndex < 0 || selectedIndex >= savedCount) {
          drawIRStatus("Select a signal first!", TFT_RED);
        } else {
          irrecv.disableIRIn();
          irsend.send(
            saved[selectedIndex].protocol,
            saved[selectedIndex].value,
            saved[selectedIndex].bits
          );
          irrecv.enableIRIn();

          Serial.print(F("Sent: "));
          Serial.println(saved[selectedIndex].name);

          highlightSend(true);
          delay(500);
          highlightSend(false);
          drawIRStatus("Signal sent!", TFT_GREEN);
        }
      }

      // CLEAR ALL
      else if (tx > 10 && tx < 115 && ty > 90 && ty < 125) {
        savedCount    = 0;
        selectedIndex = -1;
        capturing     = false;
        needRedraw    = false;
        saveIRSignals();
        drawIRTab();
      }

      // DUMP ALL
      else if (tx > 125 && tx < 230 && ty > 90 && ty < 125) {
        Serial.println(F("===== SAVED IR SIGNALS ====="));
        for (int i = 0; i < savedCount; i++) {
          Serial.print(i + 1);
          Serial.print(F(". "));
          Serial.print(saved[i].name);
          Serial.print(F(" P:"));
          Serial.print(saved[i].protocol);
          Serial.print(F(" 0x"));
          Serial.print((uint32_t)saved[i].value, HEX);
          Serial.print(F(" bits:"));
          Serial.println(saved[i].bits);
        }
        Serial.println(F("============================"));
        drawIRStatus("Dumped to Serial Monitor", TFT_CYAN);
      }

      // SELECT SIGNAL
      else if (ty > 182 && ty < 307 && savedCount > 0) {
        int tapped = (ty - 182) / 22;
        if (tapped >= 0 && tapped < savedCount) {
          selectedIndex = tapped;
          drawIRSignalList();
          drawIRStatus("Selected — tap SEND to fire", TFT_GREEN);
        }
      }
    }
  }

  // IR decode
  if (currentTab == TAB_IR && capturing && irrecv.decode(&results)) {
    if (results.value != 0xFFFFFFFFULL &&
        results.value != 0ULL &&
        results.bits > 0) {
      saved[savedCount].value    = results.value;
      saved[savedCount].protocol = results.decode_type;
      saved[savedCount].bits     = results.bits;
      savedCount++;
      capturing = false;

      Serial.print(F("Captured P:"));
      Serial.print(results.decode_type);
      Serial.print(F(" 0x"));
      Serial.println((uint32_t)results.value, HEX);

      irrecv.resume();

      // Name it
      nameSignal(savedCount - 1);
      saveIRSignals();
      needRedraw = true;
    } else {
      drawIRStatus("Repeat/unknown, try again.", TFT_RED);
      irrecv.resume();
    }
  }

  if (needRedraw && currentTab == TAB_IR) {
    needRedraw = false;
    highlightCapture(false);
    drawIRSignalList();
    drawIRStatus("Captured! Tap to select.", TFT_GREEN);
  }
}
