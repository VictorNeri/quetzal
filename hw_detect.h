#ifndef HW_DETECT_H
#define HW_DETECT_H

#include "board_config.h"
#include <Arduino.h>

// NM-CYD-C5 has a single external RF-HAT slot: either an NRF24L01+ or a
// CC1101 can be populated (never both). Everything else (WiFi, BLE, the
// onboard 802.15.4 radio) is always present since it's part of the SoC.
extern bool hwNrf24Present;
extern bool hwCc1101Present;

// Probes both chip protocols on the shared SPI bus once at boot. Safe to
// call even if neither/either is populated - each probe is just a register
// read that fails gracefully if the expected chip isn't there.
void detectPeripherals();

// What external hardware a menu entry needs, so menus can be dimmed/blocked
// when that hardware wasn't detected at boot.
enum HwReq { HW_NONE, HW_NRF24, HW_CC1101 };

inline bool hwReqSatisfied(HwReq req) {
  switch (req) {
    case HW_NRF24:  return hwNrf24Present;
    case HW_CC1101: return hwCc1101Present;
    default:        return true;
  }
}

// Call at the top of a hardware-gated feature's setup() function. If the
// required chip wasn't detected, shows a message and sets
// feature_exit_requested so the caller's dispatch loop returns to the
// submenu immediately - check feature_exit_requested right after calling
// this and bail out of setup() if it's set.
void blockFeatureIfMissing(HwReq req);

#endif // HW_DETECT_H
