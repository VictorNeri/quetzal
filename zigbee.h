#ifndef ZIGBEE_H
#define ZIGBEE_H

#include "board_config.h"
#include <Arduino.h>

// Passive 802.15.4/Zigbee recon. NM-CYD-C5-only (the ESP32-C5's native radio) -
// see zigbee.cpp for the full picture (raw esp_ieee802154 API, MAC-layer-only
// parsing, no NWK/APS decode).

namespace ZigbeeScan {
  void zigbeeScanSetup();
  void zigbeeScanLoop();
  void zigbeeScanCleanup();
}

namespace ZigbeeSniffer {
  void zigbeeSnifferSetup();
  void zigbeeSnifferLoop();
  void zigbeeSnifferCleanup();
}

#endif // ZIGBEE_H
