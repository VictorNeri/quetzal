#ifndef BLE_HID_INJECT_H
#define BLE_HID_INJECT_H

#include "board_config.h"
#include <Arduino.h>
#include "ble_compat.h"

namespace BleHidInject {
  void hidInjectSetup();
  void hidInjectLoop();
  void hidInjectCleanup();
  bool isConnected();  // true once a host has paired to the BLE remote
}

#endif // BLE_HID_INJECT_H
