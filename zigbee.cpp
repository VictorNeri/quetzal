#include "zigbee.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "utils.h"
#include "buttons_compat.h"
#include <WiFi.h>
#include "esp_ieee802154.h"
#include <cstring>

extern TFT_eSPI tft;
extern ButtonExpander pcf;

// ═══════════════════════════════════════════════════════════════════════════
// 802.15.4 / Zigbee passive recon
// ═══════════════════════════════════════════════════════════════════════════
// Uses the ESP32-C5's raw esp_ieee802154 driver directly (promiscuous mode) -
// NOT the high-level Arduino `Zigbee` library, which is for device emulation
// (joining/hosting a network) and pulls in a completely separate stack
// (esp-zigbee-lib/zboss) this feature has no use for.
//
// This is a MAC-layer-only tool: it parses the 802.15.4 header (frame type,
// sequence number, PAN IDs, short addresses) but does not decode Zigbee
// NWK/APS payloads. Most real-world Zigbee traffic is NWK-encrypted, so
// without a captured network key that's the honest limit of what a passive
// listener can show - this is an 802.15.4 sniffer, not a full Zigbee packet
// dissector.
//
// esp_ieee802154_receive_done() is the driver's RX callback (declared
// `extern "C"` in esp_ieee802154.h); it runs outside the normal Arduino loop
// so it only ever touches plain counters/buffers here, never the display.
// ═══════════════════════════════════════════════════════════════════════════

#define ZB_CHANNEL_MIN 11
#define ZB_CHANNEL_MAX 26
#define ZB_NUM_CHANNELS (ZB_CHANNEL_MAX - ZB_CHANNEL_MIN + 1)

enum class ZMode { NONE, SCAN, SNIFF };
static volatile ZMode g_mode = ZMode::NONE;
static bool g_radioEnabled = false;

// --- Channel scanner state (written from the RX callback) ---
static volatile uint32_t g_chanFrameCount[ZB_NUM_CHANNELS];
static volatile int8_t g_chanBestRssi[ZB_NUM_CHANNELS];

// --- Sniffer state: small ring buffer of recently seen frames ---
struct SniffedFrame {
  uint8_t mhr[32];
  uint8_t mhrLen;
  int8_t rssi;
  uint8_t lqi;
  uint8_t channel;
};
#define SNIFF_BUF_SIZE 16
static volatile SniffedFrame g_sniffBuf[SNIFF_BUF_SIZE];
static volatile int g_sniffHead = 0;
static volatile uint32_t g_sniffSeq = 0;  // bumps on every stored frame

extern "C" void esp_ieee802154_receive_done(uint8_t* frame, esp_ieee802154_frame_info_t* frame_info) {
  // frame[0] is the PHY length byte; frame[1..] is the MHR + payload (the
  // trailing 2 bytes are RSSI/LQI in promiscuous mode, not a real FCS).
  uint8_t len = frame[0];
  int ch = frame_info->channel;

  if (g_mode == ZMode::SCAN) {
    int idx = ch - ZB_CHANNEL_MIN;
    if (idx >= 0 && idx < ZB_NUM_CHANNELS) {
      g_chanFrameCount[idx] = g_chanFrameCount[idx] + 1;
      if (frame_info->rssi > g_chanBestRssi[idx]) g_chanBestRssi[idx] = frame_info->rssi;
    }
  } else if (g_mode == ZMode::SNIFF) {
    int idx = g_sniffHead;
    uint8_t copyLen = (len > sizeof(g_sniffBuf[0].mhr)) ? sizeof(g_sniffBuf[0].mhr) : len;
    memcpy((void*)g_sniffBuf[idx].mhr, &frame[1], copyLen);
    g_sniffBuf[idx].mhrLen = copyLen;
    g_sniffBuf[idx].rssi = frame_info->rssi;
    g_sniffBuf[idx].lqi = frame_info->lqi;
    g_sniffBuf[idx].channel = ch;
    g_sniffHead = (g_sniffHead + 1) % SNIFF_BUF_SIZE;
    g_sniffSeq = g_sniffSeq + 1;
  }

  // The driver owns the callback buffer. Release it after copying the fields
  // we need, then re-arm the one-shot receiver for the next frame.
  esp_ieee802154_receive_handle_done(frame);
  esp_ieee802154_receive();
}

static void radioEnable() {
  if (g_radioEnabled) return;
  // Best-effort coexistence: WiFi is the heavier-duty radio user in this
  // firmware and the most likely to starve the 802.15.4 receiver of airtime,
  // so drop it before enabling the ieee802154 radio. (Real-world coexistence
  // behavior with BLE features left running is untested - flag if issues
  // show up sharing the radio with an active BLE feature.)
  WiFi.mode(WIFI_OFF);
  esp_ieee802154_enable();
  esp_ieee802154_set_promiscuous(true);
  g_radioEnabled = true;
}

static void radioDisable() {
  if (!g_radioEnabled) return;
  g_mode = ZMode::NONE;
  esp_ieee802154_disable();
  g_radioEnabled = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Channel Scanner - scrolling waterfall
// ═══════════════════════════════════════════════════════════════════════════
// Continuously sweeps channels 11-26; each full sweep becomes one new row at
// the top of the waterfall (magenta = new, fading to cyan as it scrolls
// down), matching the visual style of the existing 2.4GHz Spectrum Analyzer
// (see Analyzer::getGradientWaterfallColor in bluetooth.cpp).
namespace ZigbeeScan {

#define BTN_UP BOARD_BUTTON_UP
#define BTN_DOWN BOARD_BUTTON_DOWN
#define BTN_RIGHT BOARD_BUTTON_RIGHT
#define BTN_LEFT BOARD_BUTTON_LEFT

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 2

#define CHANNEL_DWELL_MS 80
#define AXIS_Y 40
#define WF_Y 52
#define WF_HEIGHT 260
#define COL_WIDTH (SCREEN_WIDTH / ZB_NUM_CHANNELS)  // 15px

// Per-channel activity level (frame count that sweep, clamped) for each
// waterfall row. Row 0 is always the most recently completed sweep.
uint8_t waterfall[WF_HEIGHT][ZB_NUM_CHANNELS];
bool paused = false;

int yshift = 30;
static bool uiDrawn = false;
static int iconX[ICON_NUM] = {210, 10};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_power,  // pause/resume the sweep
  bitmap_icon_go_back
};

// Same gradient formula as Analyzer::getGradientWaterfallColor (top =
// magenta/pink, bottom = cyan/teal, brightness scaled by activity level).
uint16_t waterfallColor(uint8_t level, int row, int maxRows) {
  if (level == 0) return TFT_BLACK;

  float vRatio = (float)row / (float)(maxRows - 1);
  uint8_t r = 255 - (uint8_t)(vRatio * 255);
  uint8_t g = 28 + (uint8_t)(vRatio * 179);
  uint8_t b = 82 + (uint8_t)(vRatio * 173);

  float brightness = 0.4f + (level >= 8 ? 0.6f : level * 0.075f);
  if (brightness > 1.0f) brightness = 1.0f;

  r = (uint8_t)(r * brightness);
  g = (uint8_t)(g * brightness);
  b = (uint8_t)(b * brightness);

  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void drawAxis() {
  tft.fillRect(0, AXIS_Y, SCREEN_WIDTH, WF_Y - AXIS_Y, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  for (int i = 0; i < ZB_NUM_CHANNELS; i++) {
    tft.setCursor(i * COL_WIDTH + 1, AXIS_Y);
    tft.print(ZB_CHANNEL_MIN + i);
  }
}

void drawWaterfall() {
  for (int row = 0; row < WF_HEIGHT; row++) {
    for (int c = 0; c < ZB_NUM_CHANNELS; c++) {
      uint8_t level = waterfall[row][c];
      uint16_t color = waterfallColor(level, row, WF_HEIGHT);
      tft.fillRect(c * COL_WIDTH, WF_Y + row, COL_WIDTH, 1, color);
    }
  }
}

// One full 11-26 channel sweep (blocking, ~1.3s), pushed in as the newest
// waterfall row.
void runSweepRow() {
  for (int i = 0; i < ZB_NUM_CHANNELS; i++) {
    g_chanFrameCount[i] = 0;
    g_chanBestRssi[i] = -128;
  }
  g_mode = ZMode::SCAN;

  for (int ch = ZB_CHANNEL_MIN; ch <= ZB_CHANNEL_MAX; ch++) {
    esp_ieee802154_set_channel(ch);
    esp_ieee802154_receive();
    delay(CHANNEL_DWELL_MS);
  }

  for (int row = WF_HEIGHT - 1; row > 0; row--) {
    memcpy(waterfall[row], waterfall[row - 1], ZB_NUM_CHANNELS);
  }
  for (int i = 0; i < ZB_NUM_CHANNELS; i++) {
    uint32_t cnt = g_chanFrameCount[i];
    waterfall[0][i] = (uint8_t)(cnt > 15 ? 15 : cnt);
  }

  drawWaterfall();
}

void runUI() {
  static int iconY = STATUS_BAR_Y_OFFSET;

  if (!uiDrawn) {
    tft.drawLine(0, 19, 240, 19, UI_CYAN);
    tft.fillRect(140, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 140, STATUS_BAR_HEIGHT, DARK_GRAY);
    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_CYAN);
    }
    tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
    uiDrawn = true;
    drawAxis();
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_CYAN);
      animationState = 2;
      switch (activeIcon) {
        case 0: paused = !paused; break;
        case 1: feature_exit_requested = true; break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck >= 50) {
    if (ts.touched() && feature_active) {
      TS_Point p = ts.getPoint();
      int x = ::map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
      int y = ::map(p.y, TS_MAXY, TS_MINY, 0, SCREENHEIGHT - 1);
      if (y >= STATUS_BAR_Y_OFFSET && y <= STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT + 5) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x >= iconX[i] - 5 && x <= iconX[i] + ICON_SIZE + 5) {
            if (icons[i] != NULL && animationState == 0) {
              tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
              animationState = 1;
              activeIcon = i;
              lastAnimationTime = millis();
            }
            break;
          }
        }
      }
    }
    lastTouchCheck = millis();
  }
}

void zigbeeScanSetup() {
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  uiDrawn = false;
  paused = false;
  memset(waterfall, 0, sizeof(waterfall));

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);

  radioEnable();
  runUI();
}

void zigbeeScanLoop() {
  tft.drawLine(0, 19, 240, 19, UI_CYAN);
  updateStatusBar();
  runUI();
  if (!paused) runSweepRow();
}

void zigbeeScanCleanup() {
  radioDisable();
}

}  // namespace ZigbeeScan

// ═══════════════════════════════════════════════════════════════════════════
// Live Sniffer (MAC-layer only)
// ═══════════════════════════════════════════════════════════════════════════
namespace ZigbeeSniffer {

#define BTN_UP BOARD_BUTTON_UP
#define BTN_DOWN BOARD_BUTTON_DOWN
#define BTN_RIGHT BOARD_BUTTON_RIGHT
#define BTN_LEFT BOARD_BUTTON_LEFT

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 2

int channel = 11;
uint32_t lastSeenSeq = 0;

int yshift = 30;
static bool uiDrawn = false;
static int iconX[ICON_NUM] = {210, 10};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_sort_up_plus,  // change channel
  bitmap_icon_go_back
};

struct ParsedFrame {
  uint8_t frameType;
  uint8_t seqNum;
  bool hasDestPan; uint16_t destPan;
  bool hasSrcPan;  uint16_t srcPan;
  uint8_t destAddrMode, srcAddrMode;
  uint16_t destShort, srcShort;
};

// Simplified 802.15.4-2006 MAC header parser (frame type, sequence number,
// PAN IDs, short addresses). Does not handle frame-version-2 (2015+) sequence
// number suppression or security headers - out of scope for a first pass.
bool parseMacHeader(const uint8_t* mhr, uint8_t len, ParsedFrame& out) {
  if (len < 3) return false;
  uint16_t fcf = mhr[0] | (mhr[1] << 8);
  out.frameType = fcf & 0x07;
  bool panIdCompression = (fcf >> 6) & 0x01;
  out.destAddrMode = (fcf >> 10) & 0x03;
  out.srcAddrMode = (fcf >> 14) & 0x03;

  size_t pos = 2;
  if (pos >= len) return false;
  out.seqNum = mhr[pos++];

  out.hasDestPan = false;
  out.hasSrcPan = false;
  out.destShort = 0;
  out.srcShort = 0;

  if (out.destAddrMode != 0) {
    if (pos + 2 > len) return false;
    out.destPan = mhr[pos] | (mhr[pos + 1] << 8);
    pos += 2;
    out.hasDestPan = true;
    if (out.destAddrMode == 2) {
      if (pos + 2 > len) return false;
      out.destShort = mhr[pos] | (mhr[pos + 1] << 8);
      pos += 2;
    } else if (out.destAddrMode == 3) {
      pos += 8;
      if (pos > len) return false;
    }
  }

  if (out.srcAddrMode != 0) {
    bool omitSrcPan = panIdCompression && out.hasDestPan;
    if (!omitSrcPan) {
      if (pos + 2 > len) return false;
      out.srcPan = mhr[pos] | (mhr[pos + 1] << 8);
      pos += 2;
      out.hasSrcPan = true;
    } else {
      out.srcPan = out.destPan;
      out.hasSrcPan = out.hasDestPan;
    }
    if (out.srcAddrMode == 2) {
      if (pos + 2 > len) return false;
      out.srcShort = mhr[pos] | (mhr[pos + 1] << 8);
      pos += 2;
    } else if (out.srcAddrMode == 3) {
      pos += 8;
      if (pos > len) return false;
    }
  }

  return true;
}

const char* frameTypeName(uint8_t t) {
  switch (t) {
    case 0: return "Beacon";
    case 1: return "Data";
    case 2: return "Ack";
    case 3: return "Cmd";
    default: return "Other";
  }
}

#define MAX_LINES 14
String lines[MAX_LINES];
int lineCount = 0;

void addLine(const String& s) {
  for (int i = MAX_LINES - 1; i > 0; i--) lines[i] = lines[i - 1];
  lines[0] = s;
  if (lineCount < MAX_LINES) lineCount++;
}

void drawLines() {
  tft.fillRect(0, 37, 240, 280, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  for (int i = 0; i < lineCount; i++) {
    tft.setCursor(5, 40 + i * 18);
    tft.print(lines[i]);
  }
}

// g_sniffSeq counts total frames stored (0-indexed); the k-th stored frame
// lives at ring index (k % SNIFF_BUF_SIZE). lastSeenSeq tracks how many of
// those we've already consumed, so the next unread frame is always at
// (lastSeenSeq % SNIFF_BUF_SIZE) - simpler and correct, unlike indexing
// backwards from the current write head.
void drainNewFrames() {
  bool any = false;
  if (g_sniffSeq - lastSeenSeq > SNIFF_BUF_SIZE) {
    // Fell behind the ring buffer; the oldest unread frames were overwritten.
    lastSeenSeq = g_sniffSeq - SNIFF_BUF_SIZE;
  }
  while (lastSeenSeq != g_sniffSeq) {
    int idx = lastSeenSeq % SNIFF_BUF_SIZE;
    lastSeenSeq++;
    ParsedFrame pf;
    uint8_t mhrCopy[32];
    uint8_t mhrLen = g_sniffBuf[idx].mhrLen;
    memcpy(mhrCopy, (const void*)g_sniffBuf[idx].mhr, mhrLen);
    int8_t rssi = g_sniffBuf[idx].rssi;
    uint8_t ch = g_sniffBuf[idx].channel;

    String line;
    if (parseMacHeader(mhrCopy, mhrLen, pf)) {
      line = String(frameTypeName(pf.frameType)) + " ch" + String(ch) + " seq" + String(pf.seqNum);
      if (pf.hasSrcPan) line += " pan:" + String(pf.srcPan, HEX);
      if (pf.srcAddrMode == 2) line += " src:" + String(pf.srcShort, HEX);
      line += " " + String(rssi) + "dBm";
    } else {
      line = "ch" + String(ch) + " (unparsed, " + String(mhrLen) + "B) " + String(rssi) + "dBm";
    }
    addLine(line);
    any = true;
  }
  if (any) drawLines();
}

void changeChannel(int delta) {
  channel += delta;
  if (channel > ZB_CHANNEL_MAX) channel = ZB_CHANNEL_MIN;
  if (channel < ZB_CHANNEL_MIN) channel = ZB_CHANNEL_MAX;
  esp_ieee802154_set_channel(channel);
  esp_ieee802154_receive();

  tft.fillRect(140, STATUS_BAR_Y_OFFSET, 60, STATUS_BAR_HEIGHT, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, DARK_GRAY);
  tft.setCursor(142, STATUS_BAR_Y_OFFSET + 4);
  tft.print("Ch " + String(channel));
}

void runUI() {
  static int iconY = STATUS_BAR_Y_OFFSET;

  if (!uiDrawn) {
    tft.drawLine(0, 19, 240, 19, UI_CYAN);
    tft.fillRect(140, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 140, STATUS_BAR_HEIGHT, DARK_GRAY);
    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_CYAN);
    }
    tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
    uiDrawn = true;
    tft.setTextFont(1);
    tft.setTextColor(UI_CYAN, DARK_GRAY);
    tft.setCursor(142, STATUS_BAR_Y_OFFSET + 4);
    tft.print("Ch " + String(channel));
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_CYAN);
      animationState = 2;
      switch (activeIcon) {
        case 0: changeChannel(1); break;
        case 1: feature_exit_requested = true; break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck >= 50) {
    if (ts.touched() && feature_active) {
      TS_Point p = ts.getPoint();
      int x = ::map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
      int y = ::map(p.y, TS_MAXY, TS_MINY, 0, SCREENHEIGHT - 1);
      if (y >= STATUS_BAR_Y_OFFSET && y <= STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT + 5) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x >= iconX[i] - 5 && x <= iconX[i] + ICON_SIZE + 5) {
            if (icons[i] != NULL && animationState == 0) {
              tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
              animationState = 1;
              activeIcon = i;
              lastAnimationTime = millis();
            }
            break;
          }
        }
      }
    }
    lastTouchCheck = millis();
  }
}

void zigbeeSnifferSetup() {
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  uiDrawn = false;
  lineCount = 0;
  channel = 11;
  lastSeenSeq = g_sniffSeq;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);

  radioEnable();
  g_mode = ZMode::SNIFF;
  esp_ieee802154_set_channel(channel);
  esp_ieee802154_receive();

  runUI();
}

void zigbeeSnifferLoop() {
  tft.drawLine(0, 19, 240, 19, UI_CYAN);
  updateStatusBar();
  runUI();
  drainNewFrames();
}

void zigbeeSnifferCleanup() {
  radioDisable();
}

}  // namespace ZigbeeSniffer
