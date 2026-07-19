#ifndef BLECONFIG_H
#define BLECONFIG_H

#include "board_config.h"
#include <Arduino.h>

#include "utils.h"
#include "subconfig.h"  // For cleanupSubGHz() and subghz_receive_active

// ═══════════════════════════════════════════════════════════════════════════
// NRF24 Cleanup - GPIO 5 shared between NRF24 CSN_PIN_3 and SD Card CS
// ═══════════════════════════════════════════════════════════════════════════
// Call this BEFORE any SD card operations to release GPIO 5 from NRF24
void cleanupNRF24();

#include <TFT_eSPI.h> 
#include "buttons_compat.h"
#include <XPT2046_Touchscreen.h>

#include "ble_compat.h"  // Bluedroid on ESP32-DIV V1, NimBLE on NM-CYD-C5
#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include "esp_wifi.h"
#include <Wire.h>


#define XPT2046_IRQ   BOARD_TOUCH_IRQ
#define XPT2046_MOSI  BOARD_TOUCH_MOSI
#define XPT2046_MISO  BOARD_TOUCH_MISO
#define XPT2046_CLK   BOARD_TOUCH_CLK
#define XPT2046_CS    BOARD_TOUCH_CS

extern TFT_eSPI tft;
extern ButtonExpander pcf;


namespace BleJammer {
void blejamSetup();
void blejamLoop();
}

namespace BleSpoofer {
  void spooferSetup();
  void spooferLoop();
}

namespace SourApple {
  void sourappleSetup();
  void sourappleLoop();
}

namespace BleScan {
  void bleScanSetup();
  void bleScanLoop();
}

namespace Scanner {
  void scannerSetup();
  void scannerLoop();
}

namespace Analyzer {
  void analyzerSetup();
  void analyzerLoop();
}

namespace WLANJammer {
  void wlanjammerSetup();
  void wlanjammerLoop();
}

namespace ProtoKill {
  void prokillLoop();
  void prokillSetup();
}

namespace BleSniffer {
  void blesnifferLoop();
  void blesnifferSetup();
  void blesnifferCleanup();
}

#endif // CONFIG_H
