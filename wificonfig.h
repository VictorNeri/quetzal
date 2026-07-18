#ifndef WIFICONFIG_H
#define WIFICONFIG_H

#include "board_config.h"

#include "arduinoFFT.h"
#include "utils.h"

// ═══════════════════════════════════════════════════════════════════════════
// SD Card Cleanup - GPIO 5 shared between SD Card CS and NRF24 CSN_PIN_3
// ═══════════════════════════════════════════════════════════════════════════
// Call this BEFORE any NRF24 radio3 operations to release GPIO 5 from SD
void cleanupSD();

#include <WiFi.h>
#include <TFT_eSPI.h> 
#include <PCF8574.h>
#include <XPT2046_Touchscreen.h>

extern TFT_eSPI tft;
extern PCF8574 pcf;

#define XPT2046_IRQ   BOARD_TOUCH_IRQ
#define XPT2046_MOSI  BOARD_TOUCH_MOSI
#define XPT2046_MISO  BOARD_TOUCH_MISO
#define XPT2046_CLK   BOARD_TOUCH_CLK
#define XPT2046_CS    BOARD_TOUCH_CS

#include <esp_wifi.h>
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR >= 5
// ESP-IDF 5 (ESP32-C5 core) replaced tcpip_adapter with esp_netif and removed
// the legacy esp_event_loop.h.
#include "esp_netif.h"
#else
#include "esp_event_loop.h"
#endif
#include "nvs_flash.h"
#include <stdio.h>
#include <string>
#include <cstddef>
#include <Wire.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <SD.h>
#include <Update.h>
#include <ESPmDNS.h>
using namespace std;


namespace PacketMonitor {
  void ptmSetup();
  void ptmLoop();
}

namespace BeaconSpammer {
  void beaconSpamSetup();
  void beaconSpamLoop();
}

namespace DeauthDetect {
  void deauthdetectSetup();
  void deauthdetectLoop();
}

namespace WifiScan {
  void wifiscanSetup();
  void wifiscanLoop();
}


namespace CaptivePortal {
  void cportalSetup();
  void cportalLoop();
}

namespace Deauther {
  void deautherSetup();
  void deautherLoop();
}

namespace FirmwareUpdate {
  void updateSetup();
  void updateLoop();
}


#endif // WIFICONFIG_H
