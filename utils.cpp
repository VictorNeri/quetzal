#include "utils.h"
#include "shared.h"
#include "icon.h"
#include "quetzal_logo.h"
#include "Touchscreen.h"
#include "ble_hid_inject.h"  // BleHidInject::isConnected() for the connectivity popup
#include <WiFi.h>  // For WiFi.RSSI() and WiFi.status()
#include "esp_idf_version.h"  // ESP_IDF_VERSION_MAJOR for API-compat guards

// Vertical-scroll command opcodes. TFT_eSPI only defines the ILI9341_* names when
// the ILI9341 driver is selected; the NM-CYD-C5 uses the ST7789 driver, which
// shares the same standard MIPI DCS opcodes, so provide fallbacks.
#ifndef ILI9341_VSCRDEF
#define ILI9341_VSCRDEF  0x33  // Vertical Scrolling Definition
#endif
#ifndef ILI9341_VSCRSADD
#define ILI9341_VSCRSADD 0x37  // Vertical Scrolling Start Address
#endif


/*
 *
 * Notification
 *
 */


/*
    showNotification("New Message!", "Task Failed Successfully.");

    if (notificationVisible && ts.touched()) {
      int x, y, z;
        TS_Point p = ts.getPoint();
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, DISPLAY_WIDTH - 1);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, DISPLAY_HEIGHT - 1);

    if (x >= closeButtonX && x <= (closeButtonX + closeButtonSize) &&
        y >= closeButtonY && y <= (closeButtonY + closeButtonSize)) {
        hideNotification();
    }

    if (x >= okButtonX && x <= (okButtonX + okButtonWidth) &&
        y >= okButtonY && y <= (okButtonY + okButtonHeight)) {
        hideNotification();
    }
     delay(100);
  }

*/

bool notificationVisible = true;
int notifX, notifY, notifWidth, notifHeight;
int closeButtonX, closeButtonY, closeButtonSize = 15;
int okButtonX, okButtonY, okButtonWidth = 60, okButtonHeight = 20;


void showNotification(const char* title, const char* message) {
    notifWidth = 200;
    notifHeight = 80;
    notifX = (240 - notifWidth) / 2;
    notifY = (320 - notifHeight) / 2;

    tft.fillRect(notifX, notifY, notifWidth, notifHeight, LIGHT_GRAY);
    tft.fillRect(notifX, notifY, notifWidth, 20, BLUE);

    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.setCursor(notifX + 5, notifY + 5);
    tft.print(title);

    closeButtonX = notifX + notifWidth - closeButtonSize - 5;
    closeButtonY = notifY + 2;
    tft.fillRect(closeButtonX, closeButtonY, closeButtonSize, closeButtonSize, RED);
    tft.setTextColor(WHITE);
    tft.setCursor(closeButtonX + 5, closeButtonY + 4);
    tft.print("X");

    int messageBoxX = notifX + 5;
    int messageBoxY = notifY + 25;
    int messageBoxWidth = notifWidth - 10;
    int messageBoxHeight = notifHeight - 45;

    tft.fillRect(messageBoxX, messageBoxY, messageBoxWidth, messageBoxHeight, WHITE);
    tft.setTextColor(BLACK);
    printWrappedText(messageBoxX + 2, messageBoxY + 5, messageBoxWidth + 2, message);

    okButtonX = notifX + (notifWidth - okButtonWidth) / 2;
    okButtonY = notifY + notifHeight - 25;

    tft.fillRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, GRAY);
    tft.drawRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, DARK_GRAY);
    tft.drawLine(okButtonX, okButtonY, okButtonX + okButtonWidth, okButtonY, WHITE);
    tft.drawLine(okButtonX, okButtonY, okButtonX, okButtonY + okButtonHeight, WHITE);

    tft.setTextColor(BLACK);
    tft.setCursor(okButtonX + 20, okButtonY + 5);
    tft.print("OK");

    notificationVisible = true;
}

void hideNotification() {
    tft.fillRect(notifX, notifY, notifWidth, notifHeight, BLACK);
    notificationVisible = false;
}

void printWrappedText(int x, int y, int maxWidth, const char* text) {
    String message = text;
    int cursorX = x, cursorY = y;

    while (message.length() > 0) {
        int lineEnd = message.length();

        while (tft.textWidth(message.substring(0, lineEnd)) > maxWidth) {
            lineEnd--;
        }

        if (lineEnd < message.length()) {
            int lastSpace = message.substring(0, lineEnd).lastIndexOf(' ');
            if (lastSpace > 0) lineEnd = lastSpace;
        }

        tft.setCursor(cursorX, cursorY);
        tft.print(message.substring(0, lineEnd));

        message = message.substring(lineEnd);
        message.trim();

        cursorY += 15;
    }
}


/*
 *
 * StatusBar
 *
 */

#if ESP_IDF_VERSION_MAJOR < 5
// Legacy ESP32 internal temperature sensor (note the upstream typo in the name).
// Removed in ESP-IDF 5; the C5 build uses the Arduino core's temperatureRead().
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif
uint8_t temprature_sens_read();
#endif

unsigned long lastStatusBarUpdate = 0;
const int STATUS_BAR_UPDATE_INTERVAL = 1000;
float lastBatteryVoltage = 0.0;

float readBatteryVoltage() {
  static bool adcInitialized = false;
  static unsigned long lastDebugPrint = 0;

#if BOARD_BATTERY_ADC_PIN < 0
  // No battery divider on this board (e.g. NM-CYD-C5; GPIO36 does not exist on
  // the C5). Report a nominal "full" voltage instead of spamming ADC errors.
  return 4.2f;
#else
  // Initialize ADC attenuation on first call (11dB for 0-3.3V range)
  if (!adcInitialized) {
    analogSetPinAttenuation(BOARD_BATTERY_ADC_PIN, ADC_11db);
    adcInitialized = true;
    Serial.println("[BATTERY] ADC initialized on GPIO36 with 11dB attenuation");
  }

  const int sampleCount = 16;  // More samples for better accuracy
  uint32_t sum = 0;

  for (int i = 0; i < sampleCount; i++) {
    sum += analogReadMilliVolts(BOARD_BATTERY_ADC_PIN);  // Use calibrated millivolt reading
    delayMicroseconds(500);  // Faster sampling
  }

  float avgMv = (float)sum / sampleCount;
  // Voltage divider ratio is 2:1, so multiply by 2 to get actual battery voltage
  float voltage = (avgMv / 1000.0) * 2.0;

  // DEBUG: Print battery info every 5 seconds to help diagnose issues
  if (millis() - lastDebugPrint > 5000) {
    Serial.printf("[BATTERY] Raw mV: %.1f, Voltage: %.2fV, Expected range: 3.0-4.2V\n", avgMv, voltage);
    if (avgMv < 100) {
      Serial.println("[BATTERY] WARNING: Very low reading - check GPIO36 wiring to battery divider!");
    }
    lastDebugPrint = millis();
  }

  return voltage;
#endif // BOARD_BATTERY_ADC_PIN
}

float readInternalTemperature() {
#if ESP_IDF_VERSION_MAJOR >= 5
  // Arduino core 3.x returns the internal sensor reading directly in Celsius.
  return temperatureRead();
#else
  float temperature = ((temprature_sens_read() - 32) / 1.8);
  return temperature;
#endif
}

// Check if SD card is available
bool isSDCardAvailable() {
  return SD.begin();
}

void drawStatusBar(float batteryVoltage, bool forceUpdate) {
  static int lastBatteryPercentage = -1;
  static int lastWiFiStrength = -1;
  static bool lastBleConnected = false;
  static String lastDisplayedTime = "";

  // IGNORE the passed voltage - read FRESH every time!
  // The caller passes a stale global that's set once at startup
  float freshVoltage = readBatteryVoltage();

  int batteryPercentage = map(freshVoltage * 100, 300, 420, 0, 100);
  batteryPercentage = constrain(batteryPercentage, 0, 100);

  // Get REAL WiFi signal strength (not random!)
  int wifiStrength = 0;
  wifi_mode_t wifiMode = WiFi.getMode();

  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    // Map RSSI (-100 to -30 dBm) to percentage (0-100%)
    wifiStrength = constrain(map(rssi, -100, -30, 0, 100), 0, 100);
  } else if (wifiMode == WIFI_AP || wifiMode == WIFI_AP_STA) {
    // In AP mode, show full bars (we ARE the access point)
    wifiStrength = 100;
  }
  // If not connected and not in AP mode, wifiStrength stays 0

  float internalTemp = readInternalTemperature();
  bool sdAvailable = false;
  //bool sdAvailable = isSDCardAvailable();
  bool bleConnected = BleHidInject::isConnected();

  if (batteryPercentage != lastBatteryPercentage || wifiStrength != lastWiFiStrength ||
      bleConnected != lastBleConnected || forceUpdate) {
    int barHeight = 20;  // Status bar height
    int x = 7;           // Padding for battery icon
    int y = 4;           // Vertical offset

    // **Dark Background with Neon Green Edge**
    tft.fillRect(0, 0, tft.width(), barHeight, DARK_GRAY);
    //tft.fillRect(0, barHeight - 2, tft.width(), 3, ORANGE);

    // **Draw Battery Icon (Hacker/Techy Look)**
    tft.drawRoundRect(x, y, 22, 10, 2, UI_CYAN);        // Battery border
    tft.fillRect(x + 22, y + 3, 2, 4, UI_CYAN);         // Battery terminal

    int batteryLevelWidth = map(batteryPercentage, 0, 100, 0, 20);
    uint16_t batteryColor = (batteryPercentage > 20) ? GREEN : RED;
    tft.fillRoundRect(x + 2, y + 2, batteryLevelWidth, 6, 1, batteryColor);

    // **Display Battery Percentage**
    tft.setCursor(x + 30, y + 2);
    tft.setTextColor(GREEN, DARK_GRAY);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.print(String(batteryPercentage) + "%");

    // **Draw Wi-Fi Signal Bars**
    int wifiX = 180;
    int wifiY = y + 11;
    for (int i = 0; i < 4; i++) {
      int barHeight = (i + 1) * 3;
      int barWidth = 4;
      int barX = wifiX + i * 6;
      if (wifiStrength > i * 25) {
        tft.fillRoundRect(barX, wifiY - barHeight, barWidth, barHeight, 1, GREEN);
      } else {
        tft.drawRoundRect(barX, wifiY - barHeight, barWidth, barHeight, 1, UI_CYAN);
      }
    }

    // Bluetooth connection indicator (BLE Remote pairing state)
    tft.drawBitmap(145, y - 3, bitmap_icon_ble, 16, 16, bleConnected ? UI_GREEN : UI_GUNMETAL);

    // Temperature indicator
    if (internalTemp >= 55) {
      tft.drawBitmap(203, y - 3, bitmap_icon_temp, 16, 16, RED);           // HOT - danger zone
    } else if (internalTemp >= 50) {
      tft.drawBitmap(203, y - 3, bitmap_icon_temp, 16, 16, ORANGE);        // WARM - caution
    } else {
      tft.drawBitmap(203, y - 3, bitmap_icon_temp, 16, 16, UI_CYAN);  // COOL - normal
    }

    // **Display SD Card Icon (If Available)**
    if (sdAvailable) {
      tft.drawBitmap(220, y - 3, bitmap_icon_sdcard, 16, 16, GREEN);
    } else {
      tft.drawBitmap(220, y - 3, bitmap_icon_nullsdcard, 16, 16, RED);
    }

    // **Bottom Line for Aesthetic (Neon Green)**
    //tft.drawLine(0, barHeight - 1, tft.width(), barHeight - 1, ORANGE);

    // **Update Last Values**
    lastBatteryPercentage = batteryPercentage;
    lastWiFiStrength = wifiStrength;
    lastBleConnected = bleConnected;
  }
}

// Tapping the always-visible status bar (y = 0..20) opens a popup with live
// WiFi/Bluetooth/temperature detail. updateStatusBar() is called from nearly
// every feature's loop across the firmware, which makes it the one place a
// global tap check reaches almost every screen - unlike handleButtons(),
// which only runs for the main menu/submenu grid.
void checkStatusBarTap() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 50) return;
  lastCheck = millis();

  if (!ts.touched()) return;
  TS_Point p = ts.getPoint();
  int x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
  int y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

  if (y < 0 || y >= 20) return;

  delay(150);  // debounce so the same physical tap isn't immediately re-read as the dismiss tap
  showConnectivityPopup();
}

void showConnectivityPopup() {
  int boxW = 200, boxH = 100;
  int boxX = (240 - boxW) / 2;
  int boxY = (320 - boxH) / 2;
  int closeSize = 15;
  int closeX = boxX + boxW - closeSize - 5;
  int closeY = boxY + 2;

  // Snapshot whatever's currently on screen under the popup so closing it can
  // restore the real content instead of leaving a black hole behind - the
  // caller's own screen may not redraw that region on its own for a while.
  uint16_t* background = (uint16_t*)malloc((size_t)boxW * boxH * sizeof(uint16_t));
  if (background) tft.readRect(boxX, boxY, boxW, boxH, background);

  tft.fillRect(boxX, boxY, boxW, boxH, UI_DARK);
  tft.drawRect(boxX, boxY, boxW, boxH, UI_AMBER);
  tft.fillRect(boxX, boxY, boxW, 18, UI_GUNMETAL);
  tft.setTextFont(1);
  tft.setTextColor(UI_AMBER, UI_GUNMETAL);
  tft.setCursor(boxX + 6, boxY + 5);
  tft.print("Status");

  tft.fillRect(closeX, closeY, closeSize, closeSize, RED);
  tft.setTextColor(WHITE, RED);
  tft.setCursor(closeX + 4, closeY + 3);
  tft.print("X");

  tft.setTextColor(UI_CYAN, UI_DARK);
  tft.setCursor(boxX + 8, boxY + 28);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print("WiFi: " + WiFi.SSID());
    tft.setCursor(boxX + 8, boxY + 40);
    tft.print("  " + WiFi.localIP().toString());
  } else {
    tft.print("WiFi: Not connected");
  }

  tft.setCursor(boxX + 8, boxY + 56);
  tft.print(String("Bluetooth: ") + (BleHidInject::isConnected() ? "Connected (Remote)" : "Not connected"));

  tft.setCursor(boxX + 8, boxY + 72);
  tft.printf("Temp: %.1f C", readInternalTemperature());

  while (true) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
      int y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);
      if (x >= closeX && x <= closeX + closeSize && y >= closeY && y <= closeY + closeSize) {
        if (background) {
          tft.pushImage(boxX, boxY, boxW, boxH, background);
          free(background);
        } else {
          // readback unavailable/allocation failed - best effort, matches
          // the plain black-fill behavior showNotification()/hideNotification()
          // already use elsewhere in this codebase.
          tft.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
        }
        delay(150);
        return;
      }
    }
    delay(10);
  }
}

void updateStatusBar() {
  checkStatusBarTap();

  unsigned long currentMillis = millis();

  if (currentMillis - lastStatusBarUpdate > STATUS_BAR_UPDATE_INTERVAL) {
    float batteryVoltage = readBatteryVoltage();

    if (abs(batteryVoltage - lastBatteryVoltage) > 0.05 || lastBatteryVoltage == 0) {
      drawStatusBar(batteryVoltage);
      lastBatteryVoltage = batteryVoltage;
    }

    lastStatusBarUpdate = currentMillis;
  }
}


/*
 *
 * Loading
 *
 */

// Text-only loading indicator: "Loading" with a cycling number of dots.
// A proper animated asset is still TBD; the (x, y) params are kept for call
// compatibility but only used when center is false.
void loading(int frameDelay, uint16_t color, int16_t x, int16_t y, int repeats, bool center) {
  int16_t screenWidth = tft.width();
  int16_t screenHeight = tft.height();

  tft.setTextFont(4);
  tft.setTextSize(1);

  const int maxDots = 3;
  const char* base = "Loading";
  int16_t maxWidth = tft.textWidth(String(base) + "...");
  int16_t textY = center ? (screenHeight / 2) : y;

  for (int r = 0; r < repeats; r++) {
    for (int dots = 0; dots <= maxDots; dots++) {
      String text = base;
      for (int d = 0; d < dots; d++) text += ".";

      int16_t textX = center ? (screenWidth - maxWidth) / 2 : x;
      tft.fillRect(textX, textY, maxWidth, 20, TFT_BLACK);
      tft.setTextColor(color, TFT_BLACK);
      tft.setCursor(textX, textY);
      tft.print(text);

      delay(frameDelay);
    }
  }
}


/*
 *
 * Display Logo
 *
 */

void displayLogo(uint16_t color, int displayTime) {
  (void)color;  // the splash image carries its own colors now

  // Full-screen pixel-art quetzal splash (includes its own "BOOTING..."
  // text), scaled to exactly fill the display - see quetzal_logo.h.
  // pushImage() expects byte-swapped RGB565 by default; the array here was
  // generated in plain (non-swapped) order, so without this the panel
  // renders it with R/B channels scrambled (green art coming out blue-ish).
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, QUETZAL_LOGO_WIDTH, QUETZAL_LOGO_HEIGHT, bitmap_quetzal_logo);
  tft.setSwapBytes(false);

  Serial.println("==========================================");
  Serial.println("QUETZAL - Pentesting Tools");
  Serial.println(BOARD_NAME);
  Serial.println("Quetzal - see README for history and attribution");
  Serial.println("==========================================");

  delay(displayTime);
}


/*
 *
 * Terminal
 *
 */

namespace Terminal {

#define TEXT_HEIGHT 16
#define BOT_FIXED_AREA 0
#define TOP_FIXED_AREA 86
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320
#define SCREEN_WIDTH 240
#define SCREENHEIGHT 320

static bool uiDrawn = false;

uint16_t yStart = TOP_FIXED_AREA;
uint16_t yArea = DISPLAY_HEIGHT - TOP_FIXED_AREA - BOT_FIXED_AREA;
uint16_t yDraw = DISPLAY_HEIGHT - BOT_FIXED_AREA - TEXT_HEIGHT;

uint16_t xPos = 0;

byte data = 0;

boolean change_colour = 1;
boolean selected = 1;
boolean terminalActive = true;

int blank[19];

long baudRates[] = {9600, 19200, 38400, 57600, 115200};
byte baudIndex = 0;

void runUI() {

    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 3

    static int iconX[ICON_NUM] = {210, 170, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,
        bitmap_icon_power,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.drawLine(0, 19, 240, 19, UI_CYAN);
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_CYAN);
            }
        }
        tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_CYAN);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                  if (terminalActive) {
                    terminalActive = false;
                  } else if (!terminalActive) {
                    baudIndex = (baudIndex + 1) % 5;
                    Serial.end();
                    delay(100);
                    Serial.begin(baudRates[baudIndex]);
                    tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
                    tft.setTextColor(UI_CYAN, UI_CYAN);
                    String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
                    tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);
                    delay(10);
                  }
                    break;
                case 1:
                    delay(10);
                    tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
                    tft.setTextColor(UI_CYAN, UI_CYAN);
                    tft.drawCentreString(" Serial Terminal Active ", DISPLAY_WIDTH / 2, 37, 2);
                    terminalActive = true;
                    break;

                case 2:
                    feature_exit_requested = true;
                    break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    static unsigned long lastTouchCheck = 0;
    const unsigned long touchCheckInterval = 50;

    if (millis() - lastTouchCheck >= touchCheckInterval) {
        if (ts.touched() && feature_active) {
            TS_Point p = ts.getPoint();
            int x = ::map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
            int y = ::map(p.y, TS_MAXY, TS_MINY, 0, SCREENHEIGHT - 1);

            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {
                            tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                            animationState = 1;
                            activeIcon = i;
                            lastAnimationTime = millis();
                        }
                        break;
                    }
                }
            }
        }
        lastTouchCheck = millis();
    }
}

void scrollAddress(uint16_t vsp) {
  tft.writecommand(ILI9341_VSCRSADD);
  tft.writedata(vsp >> 8);
  tft.writedata(vsp);
}

int scroll_line() {
  int yTemp = yStart;
  tft.fillRect(0, yStart, blank[(yStart - TOP_FIXED_AREA) / TEXT_HEIGHT], TEXT_HEIGHT, TFT_BLACK);

  yStart += TEXT_HEIGHT;
  if (yStart >= DISPLAY_HEIGHT - BOT_FIXED_AREA) yStart = TOP_FIXED_AREA + (yStart - DISPLAY_HEIGHT + BOT_FIXED_AREA);
  scrollAddress(yStart);
  delay(1);
  return yTemp;
}

void setupScrollArea(uint16_t tfa, uint16_t bfa) {
  tft.writecommand(ILI9341_VSCRDEF);
  tft.writedata(tfa >> 8);
  tft.writedata(tfa);
  tft.writedata((DISPLAY_HEIGHT - tfa - bfa) >> 8);
  tft.writedata(DISPLAY_HEIGHT - tfa - bfa);
  tft.writedata(bfa >> 8);
  tft.writedata(bfa);
}

void terminalSetup() {

  setupTouchscreen();
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
  tft.setTextColor(UI_CYAN, UI_CYAN);
  String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
  tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  uiDrawn = false;

  Serial.begin(baudRates[baudIndex]);

  setupScrollArea(TOP_FIXED_AREA, BOT_FIXED_AREA);

  for (byte i = 0; i < 19; i++) blank[i] = 0;

}

void terminalLoop() {

  runUI();

  if (terminalActive) {
    byte charCount = 0;
    while (Serial.available() && charCount < 10) {
      data = Serial.read();
      if (data == '\r' || xPos > 231) {
        xPos = 0;
        yDraw = scroll_line();
      }
      if (data > 31 && data < 128) {
        xPos += tft.drawChar(data, xPos, yDraw, 2);
        blank[(18 + (yStart - TOP_FIXED_AREA) / TEXT_HEIGHT) % 19] = xPos;
      }
      charCount++;
      }
    }
  }
}
