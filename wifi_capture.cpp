#include "wifi_capture.h"

#include <WiFi.h>
#include <esp_timer.h>
#include <algorithm>
#include <cstring>

namespace WifiCapture {
namespace {
constexpr size_t QUEUE_DEPTH = 24;
FrameRecord queue[QUEUE_DEPTH];
volatile size_t writeIndex = 0;
volatile size_t readIndex = 0;
volatile bool captureActive = false;
volatile uint32_t receivedCount = 0;
volatile uint32_t droppedCount = 0;
volatile uint32_t byteCount = 0;
wifi_band_mode_t previousBandMode = WIFI_BAND_MODE_AUTO;
bool restoreBandMode = false;
portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED;

void promiscuousCallback(void* buffer, wifi_promiscuous_pkt_type_t type) {
  if (!captureActive || !buffer || type == WIFI_PKT_MISC) return;
  auto* packet = static_cast<wifi_promiscuous_pkt_t*>(buffer);
  const uint16_t originalLength = packet->rx_ctrl.dump_len != 0
      ? packet->rx_ctrl.dump_len : packet->rx_ctrl.sig_len;
  if (packet->rx_ctrl.rx_state != 0 || originalLength < 2) return;

  portENTER_CRITICAL(&queueMux);
  if (!captureActive) {
    portEXIT_CRITICAL(&queueMux);
    return;
  }
  receivedCount = receivedCount + 1;
  byteCount += originalLength;
  const size_t next = (writeIndex + 1) % QUEUE_DEPTH;
  if (next == readIndex) {
    droppedCount = droppedCount + 1;
    portEXIT_CRITICAL(&queueMux);
    return;
  }
  FrameRecord& record = queue[writeIndex];
  record.timestampUs = esp_timer_get_time();
  record.rssi = packet->rx_ctrl.rssi;
  record.channel = packet->rx_ctrl.channel;
  record.packetType = type;
  record.originalLength = originalLength;
  record.capturedLength = static_cast<uint16_t>(std::min<size_t>(originalLength, SNAPSHOT_BYTES));
  std::memcpy(record.payload, packet->payload, record.capturedLength);
  writeIndex = next;
  portEXIT_CRITICAL(&queueMux);
}
}  // namespace

bool begin(const CaptureConfig& config) {
  end();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(20);
#if CONFIG_IDF_TARGET_ESP32C5
  restoreBandMode = esp_wifi_get_band_mode(&previousBandMode) == ESP_OK;
  if (esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO) != ESP_OK) {
    restoreBandMode = false;
    return false;
  }
#endif
  wifi_promiscuous_filter_t filter{config.filterMask};
  if (esp_wifi_set_promiscuous(false) != ESP_OK ||
      esp_wifi_set_promiscuous_filter(&filter) != ESP_OK ||
      esp_wifi_set_promiscuous_rx_cb(&promiscuousCallback) != ESP_OK ||
      esp_wifi_set_channel(config.channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    end();
    return false;
  }
  portENTER_CRITICAL(&queueMux);
  writeIndex = 0;
  readIndex = 0;
  receivedCount = 0;
  droppedCount = 0;
  byteCount = 0;
  captureActive = true;
  portEXIT_CRITICAL(&queueMux);
  if (esp_wifi_set_promiscuous(true) != ESP_OK) {
    end();
    return false;
  }
  return true;
}

bool setChannel(uint8_t channel) {
  if (!captureActive) return false;
  return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

bool poll(FrameRecord& frame) {
  bool available = false;
  portENTER_CRITICAL(&queueMux);
  if (readIndex != writeIndex) {
    frame = queue[readIndex];
    readIndex = (readIndex + 1) % QUEUE_DEPTH;
    available = true;
  }
  portEXIT_CRITICAL(&queueMux);
  return available;
}

void end() {
  esp_wifi_set_promiscuous(false);
  portENTER_CRITICAL(&queueMux);
  captureActive = false;
  writeIndex = 0;
  readIndex = 0;
  portEXIT_CRITICAL(&queueMux);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
#if CONFIG_IDF_TARGET_ESP32C5
  if (restoreBandMode) esp_wifi_set_band_mode(previousBandMode);
  restoreBandMode = false;
#endif
}

bool active() { return captureActive; }

CaptureStats stats() {
  CaptureStats result;
  portENTER_CRITICAL(&queueMux);
  result.received = receivedCount;
  result.dropped = droppedCount;
  result.bytes = byteCount;
  portEXIT_CRITICAL(&queueMux);
  return result;
}

}  // namespace WifiCapture
