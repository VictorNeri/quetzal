#ifndef BLE_COMPAT_H
#define BLE_COMPAT_H

// ═══════════════════════════════════════════════════════════════════════════
// Bluetooth stack compatibility layer
// ═══════════════════════════════════════════════════════════════════════════
// The original ESP32-DIV V1 (ESP32-WROOM-32U) uses the Bluedroid host stack via
// the Arduino "BLE" library and also has a Bluetooth Classic (BR/EDR) radio.
//
// The NM-CYD-C5 (ESP32-C5) is BLE-only and its Arduino core builds the NimBLE
// host (CONFIG_BT_NIMBLE_ENABLED, Bluedroid disabled). The Bluedroid headers
// (esp_bt_main.h, esp_gap_bt_api.h, BLEDevice.h) are therefore unavailable.
//
// This header exposes one API to the firmware regardless of stack:
//   * Type names (BLEDevice, BLEScan, ...) alias the NimBLE classes on C5.
//   * Small helpers wrap the calls whose signatures differ between stacks.
// On the original board every helper reproduces the exact prior Bluedroid call,
// so its behavior is unchanged.
// ═══════════════════════════════════════════════════════════════════════════

#include "board_config.h"
#include <Arduino.h>
#include <string>
#include <cstring>

#if BOARD_BLE_STACK_NIMBLE

#include <NimBLEDevice.h>

// Map the Bluedroid Arduino BLE type names the firmware uses onto NimBLE.
using BLEDevice            = NimBLEDevice;
using BLEScan              = NimBLEScan;
using BLEScanResults       = NimBLEScanResults;
using BLEServer            = NimBLEServer;
using BLEAdvertising       = NimBLEAdvertising;
using BLEAdvertisementData = NimBLEAdvertisementData;
using BLEAdvertisedDevice  = NimBLEAdvertisedDevice;
using BLEAddress           = NimBLEAddress;
using BLEUUID              = NimBLEUUID;

#else  // Bluedroid (original ESP32-DIV V1)

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"

#endif

// ─── Helpers with a single signature across both stacks ────────────────────

// Raise BLE advertising TX power to the maximum (P9 / +9 dBm).
inline void bleCompatSetAdvTxPowerMax() {
#if BOARD_BLE_STACK_NIMBLE
  NimBLEDevice::setPower(9);
#else
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
#endif
}

// Raise default, advertising and scan TX power to the maximum (P9 / +9 dBm).
inline void bleCompatSetAllTxPowerMax() {
#if BOARD_BLE_STACK_NIMBLE
  NimBLEDevice::setPower(9);
#else
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
#endif
}

// Advertise from a spoofed 6-byte random device address.
inline void bleCompatSetAdvRandomAddress(BLEAdvertising* advertising, const uint8_t addr[6]) {
#if BOARD_BLE_STACK_NIMBLE
  (void)advertising;  // NimBLE sets the own-address globally, not per-advertiser.
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  NimBLEDevice::setOwnAddr(addr);
#else
  esp_bd_addr_t a;
  memcpy(a, addr, 6);
  advertising->setDeviceAddress(a, BLE_ADDR_TYPE_RANDOM);
#endif
}

// Select the advertising PDU type (0 = connectable/scannable IND,
// 1 = scannable SCAN_IND, 2 = non-connectable NONCONN_IND).
inline void bleCompatSetAdvType(BLEAdvertising* advertising, int type) {
#if BOARD_BLE_STACK_NIMBLE
  switch (type) {
    case 0:
      advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);
      advertising->enableScanResponse(true);
      break;
    case 1:
      advertising->setConnectableMode(BLE_GAP_CONN_MODE_NON);
      advertising->enableScanResponse(true);
      break;
    default:
      advertising->setConnectableMode(BLE_GAP_CONN_MODE_NON);
      advertising->enableScanResponse(false);
      break;
  }
#else
  switch (type) {
    case 0:  advertising->setAdvertisementType(ADV_TYPE_IND); break;
    case 1:  advertising->setAdvertisementType(ADV_TYPE_SCAN_IND); break;
    default: advertising->setAdvertisementType(ADV_TYPE_NONCONN_IND); break;
  }
#endif
}

// Set the preferred connection interval advertised to peers. NimBLE advertises
// without these preferred-parameter AD fields, so this is a no-op there.
inline void bleCompatSetPreferredInterval(BLEAdvertising* advertising, uint16_t minInterval, uint16_t maxInterval) {
#if BOARD_BLE_STACK_NIMBLE
  (void)advertising;
  (void)minInterval;
  (void)maxInterval;
#else
  advertising->setMinPreferred(minInterval);
  advertising->setMaxPreferred(maxInterval);
#endif
}

// Append raw advertising-data bytes to an advertisement payload.
inline void bleCompatAddRawAdvData(BLEAdvertisementData& data, const uint8_t* payload, size_t len) {
#if BOARD_BLE_STACK_NIMBLE
  data.addData(payload, len);
#else
  data.addData(std::string(reinterpret_cast<const char*>(payload), len));
#endif
}

#endif // BLE_COMPAT_H
