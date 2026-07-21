#ifndef UTILS_H
#define UTILS_H

#include "board_config.h"
#include <Arduino.h>

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SD.h>

extern TFT_eSPI tft;

#define XPT2046_IRQ   BOARD_TOUCH_IRQ
#define XPT2046_MOSI  BOARD_TOUCH_MOSI
#define XPT2046_MISO  BOARD_TOUCH_MISO
#define XPT2046_CLK   BOARD_TOUCH_CLK
#define XPT2046_CS    BOARD_TOUCH_CS

void updateStatusBar();
float readBatteryVoltage();
float readInternalTemperature();
bool isSDCardAvailable();
void drawStatusBar(float batteryVoltage, bool forceUpdate = false);
void checkStatusBarTap();
void showConnectivityPopup();

void showNotification(const char* title, const char* message);
void hideNotification();
void printWrappedText(int x, int y, int maxWidth, const char* text);
void loading(int frameDelay, uint16_t color, int16_t x, int16_t y, int repeats, bool center);
void displayLogo(uint16_t color, int displayTime);


namespace Terminal {
  void terminalSetup();
  void terminalLoop();
}

#endif
