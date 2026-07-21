#ifndef BLE_GATT_ENUM_H
#define BLE_GATT_ENUM_H

#include "board_config.h"
#include <Arduino.h>
#include "ble_compat.h"

namespace GattEnum {
  void gattEnumSetup();
  void gattEnumLoop();
  void gattEnumCleanup();
}

#endif // BLE_GATT_ENUM_H
