#include "espnow_test.h"
#include <Arduino.h>
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "utils.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

extern TFT_eSPI tft;

namespace EspNowTest {

static constexpr int SCREEN_WIDTH = 240;
static constexpr int SCREEN_HEIGHT = 320;
static constexpr int STATUS_BAR_Y = 20;
static constexpr int STATUS_BAR_HEIGHT = 16;
static constexpr int BACK_ICON_X = 210;
static constexpr int ICON_SIZE = 16;
static constexpr unsigned long SEND_INTERVAL_MS = 1000;
static constexpr uint8_t ESPNOW_CHANNEL = 1;

static bool broadcastMode = false;
static bool initialized = false;
static bool uiDrawn = false;
static unsigned long lastSendAt = 0;
static uint32_t sentCount = 0;
static uint32_t sendErrors = 0;
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t receivedCount = 0;
static volatile int8_t lastRssi = 0;
static volatile uint16_t lastLength = 0;
static uint8_t lastSender[6] = {};
static char lastPayload[33] = {};

void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
  if (!info || !data || length <= 0) return;

  const int copyLength = min(length, 32);
  portENTER_CRITICAL(&rxMux);
  memcpy(lastSender, info->src_addr, sizeof(lastSender));
  memcpy(lastPayload, data, copyLength);
  lastPayload[copyLength] = '\0';
  lastLength = static_cast<uint16_t>(min(length, 65535));
  lastRssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  receivedCount = receivedCount + 1;
  portEXIT_CRITICAL(&rxMux);
}

void drawStaticUi() {
  tft.fillScreen(TFT_BLACK);
  tft.drawLine(0, 19, SCREEN_WIDTH, 19, UI_CYAN);
  tft.fillRect(190, STATUS_BAR_Y, 50, STATUS_BAR_HEIGHT, DARK_GRAY);
  tft.drawBitmap(BACK_ICON_X, STATUS_BAR_Y, bitmap_icon_go_back, ICON_SIZE, ICON_SIZE, UI_CYAN);
  tft.drawLine(0, STATUS_BAR_Y + STATUS_BAR_HEIGHT, SCREEN_WIDTH,
               STATUS_BAR_Y + STATUS_BAR_HEIGHT, UI_AMBER);

  tft.setTextFont(2);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 48);
  tft.print(broadcastMode ? "ESP-NOW Broadcast" : "ESP-NOW Receive");

  tft.setTextFont(1);
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(10, 72);
  tft.printf("Radio channel: %u", ESPNOW_CHANNEL);
  tft.setCursor(10, 84);
  tft.print(broadcastMode ? "Sends QUETZAL-TEST every second" : "Listening for ESP-NOW frames");
  uiDrawn = true;
}

void drawStatus() {
  uint32_t rxCount;
  int8_t rssi;
  uint16_t length;
  uint8_t sender[6];
  char payload[33];

  portENTER_CRITICAL(&rxMux);
  rxCount = receivedCount;
  rssi = lastRssi;
  length = lastLength;
  memcpy(sender, lastSender, sizeof(sender));
  memcpy(payload, lastPayload, sizeof(payload));
  portEXIT_CRITICAL(&rxMux);

  tft.fillRect(0, 105, SCREEN_WIDTH, 160, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 110);
  tft.printf("State: %s", initialized ? "ready" : "init failed");
  tft.setCursor(10, 128);
  tft.printf("Sent: %lu   errors: %lu", static_cast<unsigned long>(sentCount),
             static_cast<unsigned long>(sendErrors));
  tft.setCursor(10, 146);
  tft.printf("Received: %lu", static_cast<unsigned long>(rxCount));

  if (rxCount > 0) {
    tft.setCursor(10, 166);
    tft.printf("From: %02X:%02X:%02X:%02X:%02X:%02X", sender[0], sender[1],
               sender[2], sender[3], sender[4], sender[5]);
    tft.setCursor(10, 184);
    tft.printf("RSSI: %d dBm   bytes: %u", rssi, length);
    tft.setCursor(10, 202);
    tft.print("Data: ");
    tft.print(payload);
  }
}

bool initializeEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(50);
  if (esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;

  if (esp_now_init() != ESP_OK) return false;
  if (esp_now_register_recv_cb(onReceive) != ESP_OK) {
    esp_now_deinit();
    return false;
  }

  if (broadcastMode) {
    const uint8_t broadcastAddress[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastAddress, sizeof(broadcastAddress));
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(broadcastAddress) && esp_now_add_peer(&peer) != ESP_OK) {
      esp_now_unregister_recv_cb();
      esp_now_deinit();
      return false;
    }
  }

  return true;
}

void espNowTestSetup(bool useBroadcastMode) {
  broadcastMode = useBroadcastMode;
  sentCount = 0;
  sendErrors = 0;
  receivedCount = 0;
  lastRssi = 0;
  lastLength = 0;
  memset(lastSender, 0, sizeof(lastSender));
  memset(lastPayload, 0, sizeof(lastPayload));
  lastSendAt = 0;
  uiDrawn = false;
  initialized = initializeEspNow();
  drawStaticUi();
  drawStatus();
}

void espNowTestLoop() {
  if (!uiDrawn) drawStaticUi();
  updateStatusBar();

  if (initialized && broadcastMode && millis() - lastSendAt >= SEND_INTERVAL_MS) {
    static const uint8_t broadcastAddress[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const char payload[] = "QUETZAL-TEST";
    lastSendAt = millis();
    if (esp_now_send(broadcastAddress, reinterpret_cast<const uint8_t*>(payload),
                     sizeof(payload) - 1) == ESP_OK) {
      sentCount++;
    } else {
      sendErrors++;
    }
    drawStatus();
  }

  static uint32_t displayedRxCount = UINT32_MAX;
  uint32_t currentRxCount;
  portENTER_CRITICAL(&rxMux);
  currentRxCount = receivedCount;
  portEXIT_CRITICAL(&rxMux);
  if (currentRxCount != displayedRxCount) {
    displayedRxCount = currentRxCount;
    drawStatus();
  }

  if (!ts.touched()) return;
  TS_Point point = ts.getPoint();
  int x = ::map(point.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
  int y = ::map(point.y, TS_MAXY, TS_MINY, 0, SCREEN_HEIGHT - 1);
  if (x >= BACK_ICON_X - 8 && y >= STATUS_BAR_Y - 4 &&
      y <= STATUS_BAR_Y + STATUS_BAR_HEIGHT + 8) {
    feature_exit_requested = true;
    unsigned long releaseStart = millis();
    while (ts.touched() && millis() - releaseStart < 1500) delay(10);
  }
}

void espNowTestCleanup() {
  if (initialized) {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
  }
  initialized = false;
  uiDrawn = false;
}

}  // namespace EspNowTest
