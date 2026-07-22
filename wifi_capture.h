#pragma once

#include <Arduino.h>
#include <esp_wifi.h>

namespace WifiCapture {

constexpr size_t SNAPSHOT_BYTES = 384;

struct CaptureConfig {
  uint8_t channel{1};
  uint32_t filterMask{WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
};

struct FrameRecord {
  uint64_t timestampUs{0};
  int8_t rssi{0};
  uint8_t channel{0};
  wifi_promiscuous_pkt_type_t packetType{WIFI_PKT_MISC};
  uint16_t capturedLength{0};
  uint16_t originalLength{0};
  uint8_t payload[SNAPSHOT_BYTES]{};
};

struct CaptureStats {
  uint32_t received{0};
  uint32_t dropped{0};
  uint32_t bytes{0};
};

bool begin(const CaptureConfig& config);
bool setChannel(uint8_t channel);
bool poll(FrameRecord& frame);
void end();
bool active();
CaptureStats stats();

}  // namespace WifiCapture
