#ifndef BLE_COMPAT_H
#define BLE_COMPAT_H

// ═══════════════════════════════════════════════════════════════════════════
// Bluetooth stack compatibility layer
// ═══════════════════════════════════════════════════════════════════════════
// The NM-CYD-C5 (ESP32-C5) is BLE-only and its Arduino core builds the NimBLE
// host (CONFIG_BT_NIMBLE_ENABLED, Bluedroid disabled). The Bluedroid headers
// (esp_bt_main.h, esp_gap_bt_api.h, BLEDevice.h) are unavailable on this chip.
//
// This header exposes the Bluedroid Arduino "BLE" library's type names
// (BLEDevice, BLEScan, ...) as aliases for the NimBLE classes, plus a few
// small helper wrappers, so the firmware can keep using those names.
// ═══════════════════════════════════════════════════════════════════════════

#include "board_config.h"
#include <Arduino.h>
#include <string>
#include <cstring>

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
using BLEUUID               = NimBLEUUID;
using BLEClient            = NimBLEClient;
using BLERemoteService     = NimBLERemoteService;
using BLERemoteCharacteristic = NimBLERemoteCharacteristic;

// ─── Helpers ─────────────────────────────────────────────────────────────

// Raise BLE advertising TX power to the maximum (P9 / +9 dBm).
inline void bleCompatSetAdvTxPowerMax() {
  NimBLEDevice::setPower(9);
}

// Raise default, advertising and scan TX power to the maximum (P9 / +9 dBm).
inline void bleCompatSetAllTxPowerMax() {
  NimBLEDevice::setPower(9);
}

// Advertise from a spoofed 6-byte random device address.
inline void bleCompatSetAdvRandomAddress(BLEAdvertising* advertising, const uint8_t addr[6]) {
  (void)advertising;  // NimBLE sets the own-address globally, not per-advertiser.
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  NimBLEDevice::setOwnAddr(addr);
}

// Select the advertising PDU type (0 = connectable/scannable IND,
// 1 = scannable SCAN_IND, 2 = non-connectable NONCONN_IND).
inline void bleCompatSetAdvType(BLEAdvertising* advertising, int type) {
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
}

// Set the preferred connection interval advertised to peers. NimBLE advertises
// without these preferred-parameter AD fields, so this is a no-op.
inline void bleCompatSetPreferredInterval(BLEAdvertising* advertising, uint16_t minInterval, uint16_t maxInterval) {
  (void)advertising;
  (void)minInterval;
  (void)maxInterval;
}

// Append raw advertising-data bytes to an advertisement payload.
inline void bleCompatAddRawAdvData(BLEAdvertisementData& data, const uint8_t* payload, size_t len) {
  data.addData(payload, len);
}

#endif // BLE_COMPAT_H
