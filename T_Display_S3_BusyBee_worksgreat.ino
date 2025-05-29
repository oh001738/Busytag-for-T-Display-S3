// BusyBee ESP32 Status Display

#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <esp_adc_cal.h>

// Web server instance (port 80)
WebServer server(80);

// TFT display and sprite objects
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// Preferences for saving settings in flash memory
Preferences prefs;

// ADC characteristics for battery voltage calculation
esp_adc_cal_characteristics_t *adc_chars;

// WiFi SSID and IP address
char ssid[32] = "BusyBee";
const char* ipAddress = "192.168.4.1";

// Pin definitions
const uint8_t BUTTON_PIN = 14;      // Main status button
const uint8_t ADC_PIN = 4;          // Battery voltage detection (GPIO 4)
const float FULL_BATTERY_VOLTAGE = 4.2; // Full battery voltage (V)
const uint8_t ANGLE_BUTTON_PIN = 0; // Screen rotation button

// Display settings
const int screenWidth = 320;
const int screenHeight = 170;
const int updateInterval = 80;      // Marquee update interval (ms)
const int textSpeed = 2;            // Marquee text speed (pixels per update)

// Status variables
bool isBusy = true;
char customText[32] = "BUSY";
uint32_t customColor = TFT_RED;
bool showBattery = true;
int showCornerInfo = 2; // 0: none, 1: SSID, 2: IP

// FreeRTOS critical section for status protection
#include "freertos/portmacro.h"
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;

// Flash save flag
bool needSaveStatus = false;

// Save current status to flash
void reallySaveStatus() {
  prefs.putBool("isBusy", isBusy);
  prefs.putString("customText", customText);
  prefs.putUInt("customColor", customColor);
  prefs.putBool("showBattery", showBattery);
  prefs.putInt("showCornerInfo", showCornerInfo);
  needSaveStatus = false;
}

// Marquee variables
int16_t textX = screenWidth;        // Marquee text X position
bool useMarquee = false;            // Whether to use marquee effect
bool marqueeNeedsUpdate = false;
unsigned long lastUpdate = 0;

// C++ STL for animation and math
#include <vector>
#include <algorithm>
#include <math.h>

// Particle struct for boot animation
struct Particle {
  float x, y;
  float tx, ty;
  float vx, vy;
  uint16_t color;
};

// Arduino setup: runs once at boot
void setup() {
  randomSeed(micros());
  // Generate unique SSID based on MAC address
  uint8_t mac[6];
  WiFi.macAddress(mac);
  sprintf(ssid, "BusyBee-%02X%02X%02X", mac[3], mac[4], mac[5]);
  tft.init();
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  sprite.createSprite(screenWidth, screenHeight);

  // Load saved preferences
  prefs.begin("statusTag", false);
  isBusy = prefs.getBool("isBusy", true);
  String tmpText = prefs.getString("customText", "BUSY");
  tmpText.toCharArray(customText, sizeof(customText));
  customColor = prefs.getUInt("customColor", TFT_RED);
  showBattery = prefs.getBool("showBattery", true);
  showCornerInfo = prefs.getInt("showCornerInfo", 1);

  // Set screen rotation from saved preference
  int savedRotation = prefs.getInt("screenRotation", 3);
  tft.setRotation(savedRotation);

  // Show boot animation
  showBootAnimation();

  // Start web server task on core 0
  xTaskCreatePinnedToCore(
    handleWebServer,
    "WebServerTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );

  // Start display update task on core 1
  xTaskCreatePinnedToCore(
    updateDisplay,
    "DisplayTask",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  Serial.begin(115200);

  // Initialize ADC for battery voltage reading
  adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, adc_chars);

  // Initialize button pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  pinMode(ANGLE_BUTTON_PIN, INPUT_PULLUP);

  // Initialize display and start services
  initDisplay();
  startAPMode();
  checkMarquee();
  displayStatus();
  setupWebServer();
}

// Web server task: handles HTTP requests
void handleWebServer(void * parameter) {
  for (;;) {
    server.handleClient();
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

// Display update task: handles button and display refresh
void updateDisplay(void * parameter) {
  unsigned long lastBatteryUpdate = 0;
  unsigned long lastSaveCheck = 0;
  for (;;) {
    handleButtonPress();
    updateMarqueeIfNeeded();

    unsigned long currentMillis = millis();
    // Update battery display every 60 seconds
    if (currentMillis - lastBatteryUpdate >= 60000) {
      displayStatus();
      lastBatteryUpdate = currentMillis;
    }

    // Check if need to save status every 1 second
    if (currentMillis - lastSaveCheck >= 1000) {
      if (needSaveStatus) {
        reallySaveStatus();
      }
      lastSaveCheck = currentMillis;
    }

    delay(10);
  }
}

// Main loop: not used, just yields CPU
void loop() {
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// Initialize TFT display and sprite
void initDisplay() {
  tft.init();
  sprite.createSprite(screenWidth, screenHeight);
}

// Set font size dynamically based on text length
void setDynamicFont() {
  if (strlen(customText) <= 10) {
    sprite.setFreeFont(&FreeSans24pt7b);
  } else {
    sprite.setFreeFont(&FreeSans18pt7b);
  }
}

// Check if marquee effect is needed
void checkMarquee() {
  setDynamicFont();
  useMarquee = sprite.textWidth(customText) > screenWidth;
}

// Draw only the marquee text (for scrolling effect)
void drawMarqueeTextOnly() {
  sprite.fillRect(0, 60, screenWidth, 50, customColor);
  sprite.setTextColor(TFT_WHITE);
  setDynamicFont();
  sprite.drawString(customText, textX, 60);
  sprite.pushSprite(0, 0);
}

// Update marquee text position if needed
void updateMarqueeIfNeeded() {
  if (!useMarquee) return;

  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdate >= updateInterval) {
    textX -= textSpeed;
    if (textX < -sprite.textWidth(customText)) {
      textX = screenWidth;
    }
    marqueeNeedsUpdate = true;
    lastUpdate = currentMillis;
  }

  if (marqueeNeedsUpdate) {
    drawMarqueeTextOnly();
    marqueeNeedsUpdate = false;
  }
}

// Boot animation: particles fly to form "BusyBee" text
void showBootAnimation() {
  sprite.fillSprite(TFT_BLACK);
  String text = "BusyBee";
  sprite.setFreeFont(&FreeSans24pt7b);
  int w = sprite.textWidth(text);
  int h = sprite.fontHeight();
  int textX = (screenWidth - w) / 2;
  int textY = (screenHeight - h) / 2;
  sprite.fillSprite(TFT_BLACK);
  sprite.setTextColor(0xFFFF);
  sprite.drawString(text, textX, textY);

  struct Particle {
    float x, y;
    float tx, ty;
    float vx, vy;
    uint16_t color;
  };
  std::vector<Particle> particles;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint16_t c = sprite.readPixel(textX + x, textY + y);
      if (c != TFT_BLACK) {
        Particle p;
        p.tx = textX + x;
        p.ty = textY + y;
        p.x = rand() % screenWidth;
        p.y = rand() % screenHeight;
        p.vx = 0;
        p.vy = 0;
        p.color = TFT_WHITE;
        particles.push_back(p);
      }
    }
  }
  int frames = 40;
  for (int f = 0; f < frames; ++f) {
    sprite.fillSprite(TFT_BLACK);
    for (auto& p : particles) {
      float dx = p.tx - p.x;
      float dy = p.ty - p.y;
      p.vx = dx * 0.15;
      p.vy = dy * 0.15;
      p.x += p.vx;
      p.y += p.vy;
      sprite.fillCircle((int)p.x, (int)p.y, 1, p.color);
    }
    sprite.setTextFont(2);
    sprite.setTextColor(TFT_LIGHTGREY);
    sprite.drawRightString(ssid, screenWidth - 12, screenHeight - 8, 1);
    sprite.pushSprite(0, 0);
    delay(30);
  }
  sprite.fillSprite(TFT_BLACK);
  for (auto& p : particles) {
    sprite.fillCircle((int)p.tx, (int)p.ty, 1, p.color);
  }
  sprite.setTextFont(2);
  sprite.setTextColor(TFT_LIGHTGREY);
  sprite.drawRightString(ssid, screenWidth - 12, screenHeight - 8, 1);
  sprite.pushSprite(0, 0);
  delay(800);
}

// Display current status, text, IP, and battery level
void displayStatus() {
  float batteryLevel = readBatteryLevel();
  sprite.fillSprite(customColor);
  sprite.setTextColor(TFT_WHITE);
  setDynamicFont();

  if (useMarquee) {
    sprite.drawString(customText, textX, 60);
  } else {
    sprite.drawCentreString(customText, screenWidth / 2, 60, 1);
  }

  if (showCornerInfo == 1) {
    sprite.setTextFont(2);
    sprite.setTextColor(TFT_CYAN);
    sprite.drawRightString(ssid, screenWidth - 10, screenHeight - 10, 1);
  } else if (showCornerInfo == 2) {
    sprite.setTextFont(2);
    sprite.setTextColor(TFT_CYAN);
    sprite.drawRightString(ipAddress, screenWidth - 10, screenHeight - 10, 1);
  }
  if (showBattery) {
    sprite.setTextFont(2);
    sprite.setTextColor(TFT_YELLOW);
    sprite.drawString(String(batteryLevel) + "%", 10, screenHeight - 10, 1);
  }
  sprite.pushSprite(0, 0);
}

// Read battery voltage and return percentage (0~100%)
float readBatteryLevel() {
  int rawValue = analogRead(ADC_PIN);
  uint32_t voltage = esp_adc_cal_raw_to_voltage(rawValue, adc_chars);
  float v_adc = voltage / 1000.0;
  float v_batt = v_adc * 2.0;

  float batteryLevel = (v_batt / FULL_BATTERY_VOLTAGE) * 100.0;
  batteryLevel = constrain(batteryLevel, 0.0, 100.0);
  return batteryLevel;
}

// Button press handling (main and rotation buttons)
unsigned long buttonPressTime = 0;
const unsigned long longPressDuration = 3000;

void handleButtonPress() {
  static int lastRotation = 3;
  static unsigned long lastPress = 0;
  static bool buttonWasPressed = false;

  // Screen rotation (toggle between 1 and 3)
  if (!digitalRead(ANGLE_BUTTON_PIN)) {
    unsigned long now = millis();
    if (now - lastPress > 300) {
      int newRotation;
      if (lastRotation == 1) {
        newRotation = 3;
      } else {
        newRotation = 1;
      }
      tft.setRotation(newRotation);
      displayStatus();
      prefs.putInt("screenRotation", newRotation);
      lastRotation = newRotation;
      lastPress = now;
    }
  }

  // Main button: short press to toggle status, long press to deep sleep
  bool buttonPressed = !digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (buttonPressed && !buttonWasPressed) {
    buttonPressTime = now;
  } else if (!buttonPressed && buttonWasPressed) {
    unsigned long pressDuration = now - buttonPressTime;
    if (pressDuration >= longPressDuration) {
      esp_deep_sleep_start();
    } else if (pressDuration > 30) {
      if (now - lastPress > 300) {
        isBusy = !isBusy;
        portENTER_CRITICAL(&statusMux);
        if (isBusy) {
          strcpy(customText, "BUSY");
          customColor = TFT_RED;
        } else {
          strcpy(customText, "LET'S TALK");
          customColor = TFT_GREEN;
        }
        portEXIT_CRITICAL(&statusMux);
        textX = screenWidth;
        checkMarquee();
        displayStatus();
        saveStatus();
        lastPress = now;
      }
    }
    buttonPressTime = 0;
  }
  buttonWasPressed = buttonPressed;
}

// Save current status to flash
void saveStatus() {
  prefs.putBool("isBusy", isBusy);
  prefs.putString("customText", customText);
  prefs.putUInt("customColor", customColor);
  prefs.putBool("showBattery", showBattery);
  prefs.putInt("showCornerInfo", showCornerInfo);
}

// Start WiFi AP mode
void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, "", 1);
  WiFi.setSleep(false);
}

// Setup web server routes and handlers
void setupWebServer() {
  server.on("/", serverRoot);
  server.on("/setCornerInfo", []() {
    if (server.hasArg("cornerInfo")) {
      showCornerInfo = server.arg("cornerInfo").toInt();
    }
    saveStatus();
    displayStatus();
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  server.on("/setShowBattery", []() {
    if (server.hasArg("showBattery")) {
      showBattery = true;
    } else {
      showBattery = false;
    }
    saveStatus();
    displayStatus();
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  server.on("/setStatus", []() {
    String status = server.arg("status");
    if (status == "BUSY") {
      isBusy = true;
      strcpy(customText, "BUSY");
      customColor = TFT_RED;
    } else if (status == "TALK") {
      isBusy = false;
      strcpy(customText, "LET'S TALK");
      customColor = TFT_GREEN;
    } else if (status == "CUSTOM") {
      serverSetCustom();
      return;
    }
    textX = screenWidth;
    checkMarquee();
    server.send(200, "text/html", String("<h1>Status set to ") + customText + "</h1><a href='/'>Back</a>");
    displayStatus();
    saveStatus();
  });

  server.begin();
}

// Serve the main web page for status control
void serverRoot() {
  char hexColor[8];
  sprintf(hexColor, "#%04X", customColor);

  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Busy Tag</title>
    <style>
      body {
        font-family: 'Segoe UI', Arial, sans-serif;
        background: #f6f8fa;
        color: #222;
        margin: 0;
        padding: 0;
      }
      .container {
        max-width: 400px;
        margin: 32px auto;
        background: #fff;
        border-radius: 18px;
        box-shadow: 0 4px 24px #0001;
        padding: 32px 24px 24px 24px;
      }
      h1 {
        font-size: 2rem;
        margin-bottom: 12px;
        letter-spacing: 1px;
      }
      .status-display {
        display: flex;
        align-items: center;
        justify-content: center;
        margin-bottom: 24px;
        gap: 12px;
      }
      .status-circle {
        width: 28px;
        height: 28px;
        border-radius: 50%;
        border: 2px solid #ccc;
        display: inline-block;
        background: )rawliteral" + String(hexColor) + R"rawliteral(;
        box-shadow: 0 0 8px )rawliteral" + String(hexColor) + R"rawliteral(44;
      }
      .status-label {
        font-size: 1.3rem;
        font-weight: bold;
        letter-spacing: 1px;
      }
      .btn-group {
        display: flex;
        gap: 16px;
        margin-bottom: 28px;
        justify-content: center;
      }
      button {
        font-size: 1.1rem;
        padding: 12px 0;
        width: 140px;
        border: none;
        border-radius: 8px;
        font-weight: bold;
        color: #fff;
        cursor: pointer;
        transition: background 0.2s, box-shadow 0.2s;
        box-shadow: 0 2px 8px #0002;
      }
      #busyBtn {
        background: linear-gradient(90deg, #e53935 60%, #b71c1c 100%);
      }
      #busyBtn:hover, #busyBtn:focus {
        background: linear-gradient(90deg, #b71c1c 60%, #e53935 100%);
      }
      #talkBtn {
        background: linear-gradient(90deg, #43a047 60%, #1b5e20 100%);
      }
      #talkBtn:hover, #talkBtn:focus {
        background: linear-gradient(90deg, #1b5e20 60%, #43a047 100%);
      }
      .custom-section {
        margin-top: 18px;
        padding: 18px 0 0 0;
        border-top: 1px solid #eee;
      }
      .custom-row {
        display: flex;
        align-items: center;
        gap: 10px;
        margin-bottom: 14px;
      }
      .custom-row label {
        min-width: 60px;
        font-size: 1rem;
      }
      .custom-row input[type="text"] {
        flex: 1;
        font-size: 1rem;
        padding: 6px 8px;
        border-radius: 5px;
        border: 1px solid #ccc;
      }
      .custom-row input[type="color"] {
        width: 36px;
        height: 36px;
        border: none;
        background: none;
        padding: 0;
        margin-left: 4px;
      }
      .custom-row .color-preview {
        width: 28px;
        height: 28px;
        border-radius: 50%;
        border: 1.5px solid #bbb;
        margin-left: 6px;
        background: )rawliteral" + String(hexColor) + R"rawliteral(;
        box-shadow: 0 0 6px )rawliteral" + String(hexColor) + R"rawliteral(33;
      }
      .custom-section input[type="submit"] {
        width: 100%;
        margin-top: 8px;
        background: #1976d2;
        color: #fff;
        border: none;
        border-radius: 8px;
        font-size: 1.1rem;
        padding: 10px 0;
        font-weight: bold;
        cursor: pointer;
        box-shadow: 0 2px 8px #1976d233;
        transition: background 0.2s;
      }
      .custom-section input[type="submit"]:hover {
        background: #0d47a1;
      }
      @media (max-width: 500px) {
        .container { padding: 16px 4vw; }
        button { width: 100px; font-size: 1rem; }
      }
    </style>
  </head>
  <body>
    <div class="container">
      <h1>Busy Tag Control Panel</h1>
      <div class="status-display">
        <span class="status-circle"></span>
        <span class="status-label">)rawliteral" + customText + R"rawliteral(</span>
      </div>
      <div class="btn-group">
        <button id="busyBtn" onclick='fetch("/setStatus", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: "status=BUSY"}).then(()=>location.reload())'>BUSY</button>
        <button id="talkBtn" onclick='fetch("/setStatus", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: "status=TALK"}).then(()=>location.reload())'>LET'S TALK</button>
      </div>
      <div class="custom-section">
        <form id="cornerInfoForm" style="margin-bottom:10px;" method="POST" action="/setCornerInfo">
          <div class="custom-row">
            <label for="cornerInfo">Right bottom info</label>
            <select id="cornerInfo" name="cornerInfo" onchange="document.getElementById('cornerInfoForm').submit();">
              <option value="0" )rawliteral" + String(showCornerInfo == 0 ? "selected" : "") + R"rawliteral(>None</option>
              <option value="1" )rawliteral" + String(showCornerInfo == 1 ? "selected" : "") + R"rawliteral(>Show SSID</option>
              <option value="2" )rawliteral" + String(showCornerInfo == 2 ? "selected" : "") + R"rawliteral(>Show IP</option>
            </select>
          </div>
        </form>
        <form id="batteryForm" style="margin-bottom:10px;" method="POST" action="/setShowBattery">
          <div class="custom-row">
            <label for="showBattery">Show battery</label>
            <input type="checkbox" id="showBattery" name="showBattery" onchange="document.getElementById('batteryForm').submit();" )rawliteral" + String(showBattery ? "checked" : "") + R"rawliteral(>
          </div>
        </form>
        <form onsubmit='event.preventDefault(); fetch("/setStatus", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: "status=CUSTOM&customText=" + this.customText.value + "&customColor=" + this.customColor.value}).then(()=>location.reload())'>
          <div class="custom-row">
            <label for="customText">Text</label>
            <input type="text" id="customText" name="customText" maxlength="50" value=")rawliteral" + customText + R"rawliteral(" autocomplete="off">
          </div>
          <div class="custom-row">
            <label for="customColor">Color</label>
            <input type="color" id="customColor" name="customColor" value=")rawliteral" + String(hexColor) + R"rawliteral(">
            <span class="color-preview"></span>
          </div>
          <input type="submit" value="Apply Custom">
        </form>
      </div>
    </div>
    <script>
      const colorInput = document.getElementById('customColor');
      const colorPreview = document.querySelector('.color-preview');
      colorInput.addEventListener('input', function() {
        colorPreview.style.background = colorInput.value;
      });
      colorPreview.style.background = colorInput.value;
    </script>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html; charset=utf-8", html);
}

// Convert RGB888 to RGB565 color format
uint16_t rgbTo565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Handle custom status and color from web interface
void serverSetCustom() {
  String newText = server.arg("customText");
  String newColor = server.arg("customColor");

  if (newText.length() > 0 && newText.length() <= 20) {
    portENTER_CRITICAL(&statusMux);
    strncpy(customText, newText.c_str(), sizeof(customText) - 1);
    customText[sizeof(customText) - 1] = '\0';
    portEXIT_CRITICAL(&statusMux);
  }

  if (newColor.length() == 7 && newColor[0] == '#') {
    uint8_t r = strtol(newColor.substring(1, 3).c_str(), nullptr, 16);
    uint8_t g = strtol(newColor.substring(3, 5).c_str(), nullptr, 16);
    uint8_t b = strtol(newColor.substring(5, 7).c_str(), nullptr, 16);
    portENTER_CRITICAL(&statusMux);
    customColor = rgbTo565(r, g, b);
    portEXIT_CRITICAL(&statusMux);
  }

  textX = screenWidth;
  checkMarquee();
  displayStatus();
  needSaveStatus = true;

  server.send(200, "text/html", String("<h1>Custom set to: ") + customText + "</h1><a href='/'>Back</a>");
  WiFi.setSleep(false);
}
