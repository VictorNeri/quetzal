#include "ble_hid_inject.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "utils.h"
#include "buttons_compat.h"
#include <NimBLEHIDDevice.h>

#define US_KEYBOARD
#include <HIDKeyboardTypes.h>  // keymap[]/KEYMAP_SIZE/MODIFIER_KEY - ASCII to USB HID usage codes

// ═══════════════════════════════════════════════════════════════════════════
// BLE Remote - interactive BLE HID keyboard + media remote ("BadBLE" style).
// ═══════════════════════════════════════════════════════════════════════════
// Advertises the board as a combined BLE HID keyboard + consumer-control
// (media key) device. Three tabs share one pairing session:
//   Keys    - on-screen QWERTY-ish grid, fires one keystroke per tap
//   Media   - play/pause/next/prev/volume/mute buttons
//   Presets - the original scripted-payload feature (typed test string,
//             Win+R Notepad demo), preserved as-is from the MVP version
//
// The USB HID report descriptor below has two top-level collections: the
// standard single-report "boot keyboard" descriptor (8-byte input report: 1
// modifier byte, 1 reserved byte, 6 keycodes; 1-byte LED output report) used
// verbatim by the vast majority of USB/BLE HID keyboard implementations,
// plus a second, smaller Consumer Control collection (report ID 2) for
// media keys.
//
// keymap[]/KEYMAP_SIZE/MODIFIER_KEY come from the ESP32 Arduino core's own
// Bluedroid BLE library (libraries/BLE/src/HIDKeyboardTypes.h) - that header
// has no Bluedroid-specific dependencies (just a bit-mask enum and a static
// ASCII-to-USB-HID-usage lookup table), so it's reused as-is here on NimBLE.
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;
extern ButtonExpander pcf;

namespace BleHidInject {

#define BTN_UP BOARD_BUTTON_UP
#define BTN_DOWN BOARD_BUTTON_DOWN
#define BTN_RIGHT BOARD_BUTTON_RIGHT
#define BTN_LEFT BOARD_BUTTON_LEFT

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16

static const uint8_t hidReportDescriptor[] = {
  // --- Keyboard, report ID 1 ---
  0x05, 0x01,       // USAGE_PAGE (Generic Desktop)
  0x09, 0x06,       // USAGE (Keyboard)
  0xa1, 0x01,       // COLLECTION (Application)
  0x85, 0x01,       //   REPORT_ID (1)
  0x05, 0x07,       //   USAGE_PAGE (Kbrd/Keypad)
  0x19, 0xe0,       //   USAGE_MINIMUM (0xE0)
  0x29, 0xe7,       //   USAGE_MAXIMUM (0xE7)
  0x15, 0x00,       //   LOGICAL_MINIMUM (0)
  0x25, 0x01,       //   LOGICAL_MAXIMUM (1)
  0x75, 0x01,       //   REPORT_SIZE (1)
  0x95, 0x08,       //   REPORT_COUNT (8)
  0x81, 0x02,       //   INPUT (Data,Var,Abs)   ; modifier byte
  0x95, 0x01,       //   REPORT_COUNT (1)
  0x75, 0x08,       //   REPORT_SIZE (8)
  0x81, 0x01,       //   INPUT (Cnst,Arr,Abs)   ; reserved byte
  0x95, 0x06,       //   REPORT_COUNT (6)
  0x75, 0x08,       //   REPORT_SIZE (8)
  0x15, 0x00,       //   LOGICAL_MINIMUM (0)
  0x25, 0x65,       //   LOGICAL_MAXIMUM (101)
  0x05, 0x07,       //   USAGE_PAGE (Kbrd/Keypad)
  0x19, 0x00,       //   USAGE_MINIMUM (0)
  0x29, 0x65,       //   USAGE_MAXIMUM (101)
  0x81, 0x00,       //   INPUT (Data,Arr,Abs)   ; up to 6 simultaneous keycodes
  0x95, 0x05,       //   REPORT_COUNT (5)
  0x75, 0x01,       //   REPORT_SIZE (1)
  0x05, 0x08,       //   USAGE_PAGE (LEDs)
  0x19, 0x01,       //   USAGE_MINIMUM (1)
  0x29, 0x05,       //   USAGE_MAXIMUM (5)
  0x91, 0x02,       //   OUTPUT (Data,Var,Abs)  ; LED state
  0x95, 0x01,       //   REPORT_COUNT (1)
  0x75, 0x03,       //   REPORT_SIZE (3)
  0x91, 0x01,       //   OUTPUT (Cnst,Arr,Abs)  ; LED padding
  0xc0,             // END_COLLECTION

  // --- Consumer Control (media keys), report ID 2 ---
  0x05, 0x0C,       // USAGE_PAGE (Consumer)
  0x09, 0x01,       // USAGE (Consumer Control)
  0xA1, 0x01,       // COLLECTION (Application)
  0x85, 0x02,       //   REPORT_ID (2)
  0x15, 0x00,       //   LOGICAL_MINIMUM (0)
  0x25, 0x01,       //   LOGICAL_MAXIMUM (1)
  0x75, 0x01,       //   REPORT_SIZE (1)
  0x95, 0x07,       //   REPORT_COUNT (7)
  0x09, 0xB5,       //   USAGE (Scan Next Track)     -> bit 0
  0x09, 0xB6,       //   USAGE (Scan Previous Track) -> bit 1
  0x09, 0xCD,       //   USAGE (Play/Pause)          -> bit 2
  0x09, 0xE2,       //   USAGE (Mute)                -> bit 3
  0x09, 0xE9,       //   USAGE (Volume Increment)    -> bit 4
  0x09, 0xEA,       //   USAGE (Volume Decrement)    -> bit 5
  0x09, 0xB7,       //   USAGE (Stop)                -> bit 6
  0x81, 0x02,       //   INPUT (Data,Var,Abs)
  0x95, 0x01,       //   REPORT_COUNT (1)
  0x75, 0x01,       //   REPORT_SIZE (1)
  0x81, 0x01,       //   INPUT (Cnst,Arr,Abs)        ; 1-bit pad, byte-align
  0xC0
};

NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* inputKeyboard = nullptr;
NimBLECharacteristic* inputConsumer = nullptr;
BLEServer* pServer = nullptr;
bool advertisingStarted = false;

bool isConnected() {
  return pServer && pServer->getConnectedCount() > 0;
}

void releaseAllKeys() {
  if (!inputKeyboard) return;
  uint8_t report[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  inputKeyboard->setValue(report, 8);
  inputKeyboard->notify();
}

void sendKeyReport(uint8_t modifier, uint8_t keycode) {
  if (!inputKeyboard) return;
  uint8_t report[8] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
  inputKeyboard->setValue(report, 8);
  inputKeyboard->notify();
  delay(8);
  releaseAllKeys();
  delay(8);
}

enum ConsumerBit : uint8_t {
  CONSUMER_NEXT        = 0x01,
  CONSUMER_PREV        = 0x02,
  CONSUMER_PLAY_PAUSE  = 0x04,
  CONSUMER_MUTE        = 0x08,
  CONSUMER_VOL_UP      = 0x10,
  CONSUMER_VOL_DOWN    = 0x20,
  CONSUMER_STOP        = 0x40,
};

void sendConsumerReport(uint8_t bitmask) {
  if (!inputConsumer) return;
  uint8_t report[1] = { bitmask };
  inputConsumer->setValue(report, 1);
  inputConsumer->notify();
  delay(8);
  uint8_t zero[1] = { 0 };
  inputConsumer->setValue(zero, 1);
  inputConsumer->notify();
}

void typeChar(char c) {
  uint8_t idx = (uint8_t)c;
  if (idx >= KEYMAP_SIZE) return;
  if (keymap[idx].usage == 0) return;
  sendKeyReport(keymap[idx].modifier, keymap[idx].usage);
}

void typeString(const char* s) {
  for (const char* p = s; *p; p++) typeChar(*p);
}

void runPresetHello() {
  typeString("Hello from Quetzal!");
}

void runPresetNotepadDemo() {
  const uint8_t MOD_LEFT_GUI = 0x08;  // bit 3 of the modifier byte (Windows/Command key)
  sendKeyReport(MOD_LEFT_GUI, keymap[(uint8_t)'r'].usage);  // Windows: Win+R (Run dialog)
  delay(400);
  typeString("notepad");
  typeChar('\n');
  delay(800);
  typeString("This was typed via BLE HID injection.");
}

struct Preset {
  const char* name;
  void (*run)();
};
Preset presets[] = {
  {"Type Test String", runPresetHello},
  {"Win+R Notepad Demo", runPresetNotepadDemo},
};
const int numPresets = sizeof(presets) / sizeof(presets[0]);
int presetIndex = 0;

enum class RemoteTab { KEYS, MEDIA, PRESETS, SCROLL };
RemoteTab currentTab = RemoteTab::KEYS;
bool shiftActive = false;

unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

static bool uiDrawn = false;

// ── Tab bar (occupies the mini status row, y = STATUS_BAR_Y_OFFSET..+HEIGHT) ──
#define TAB_BACK_X 2
#define TAB_KEYS_X 22
#define TAB_MEDIA_X 76
#define TAB_PRESETS_X 130
#define TAB_SCROLL_X 184
#define TAB_END_X 238

void drawTabBar() {
  tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);
  tft.drawBitmap(TAB_BACK_X, STATUS_BAR_Y_OFFSET, bitmap_icon_go_back, ICON_SIZE, ICON_SIZE, UI_CYAN);

  tft.setTextFont(1);
  tft.setCursor(TAB_KEYS_X, STATUS_BAR_Y_OFFSET + 4);
  tft.setTextColor(currentTab == RemoteTab::KEYS ? UI_AMBER : UI_CYAN, DARK_GRAY);
  tft.print("KEYS");

  tft.setCursor(TAB_MEDIA_X, STATUS_BAR_Y_OFFSET + 4);
  tft.setTextColor(currentTab == RemoteTab::MEDIA ? UI_AMBER : UI_CYAN, DARK_GRAY);
  tft.print("MEDIA");

  tft.setCursor(TAB_PRESETS_X, STATUS_BAR_Y_OFFSET + 4);
  tft.setTextColor(currentTab == RemoteTab::PRESETS ? UI_AMBER : UI_CYAN, DARK_GRAY);
  tft.print("PRESETS");

  tft.setCursor(TAB_SCROLL_X, STATUS_BAR_Y_OFFSET + 4);
  tft.setTextColor(currentTab == RemoteTab::SCROLL ? UI_AMBER : UI_CYAN, DARK_GRAY);
  tft.print("SCROLL");

  tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, UI_AMBER);
}

void drawConnStatusLine() {
  tft.fillRect(0, 38, SCREEN_WIDTH, 12, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(isConnected() ? UI_GREEN : DARK_GRAY, TFT_BLACK);
  tft.setCursor(10, 40);
  tft.print(isConnected() ? "Status: PAIRED" : "Status: Advertising...");
}

// ── Keys tab ──
void drawKeysTab() {
  tft.setTextFont(1);
  const char* row0 = shiftActive ? "QWERTYUIOP" : "qwertyuiop";
  const char* row1 = shiftActive ? "ASDFGHJKL" : "asdfghjkl";
  const char* row2 = shiftActive ? "ZXCVBNM" : "zxcvbnm";

  int x = 1;
  for (int i = 0; row0[i]; i++) {
    tft.drawRoundRect(x, 54, 22, 26, 3, UI_CYAN);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.setCursor(x + 6, 62);
    tft.print(row0[i]);
    x += 24;
  }

  x = 13;
  for (int i = 0; row1[i]; i++) {
    tft.drawRoundRect(x, 82, 22, 26, 3, UI_CYAN);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.setCursor(x + 6, 90);
    tft.print(row1[i]);
    x += 24;
  }

  tft.drawRoundRect(1, 110, 32, 26, 3, shiftActive ? UI_AMBER : UI_CYAN);
  tft.setTextColor(shiftActive ? UI_AMBER : UI_CYAN, TFT_BLACK);
  tft.setCursor(6, 118);
  tft.print("SH");

  x = 35;
  for (int i = 0; row2[i]; i++) {
    tft.drawRoundRect(x, 110, 22, 26, 3, UI_CYAN);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.setCursor(x + 6, 118);
    tft.print(row2[i]);
    x += 24;
  }

  tft.drawRoundRect(203, 110, 32, 26, 3, UI_CYAN);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(207, 118);
  tft.print("<-");

  tft.drawRoundRect(3, 138, 140, 26, 3, UI_CYAN);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(55, 146);
  tft.print("SPACE");

  tft.drawRoundRect(147, 138, 90, 26, 3, UI_CYAN);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(170, 146);
  tft.print("ENTER");
}

void handleKeysTabTouch(int x, int y) {
  if (y >= 54 && y < 80) {
    const char* row0 = shiftActive ? "QWERTYUIOP" : "qwertyuiop";
    int i = (x - 1) / 24;
    if (i >= 0 && i < 10 && x >= 1 + i * 24 && x <= 1 + i * 24 + 22) typeChar(row0[i]);
    return;
  }
  if (y >= 82 && y < 108) {
    const char* row1 = shiftActive ? "ASDFGHJKL" : "asdfghjkl";
    int i = (x - 13) / 24;
    if (i >= 0 && i < 9 && x >= 13 + i * 24 && x <= 13 + i * 24 + 22) typeChar(row1[i]);
    return;
  }
  if (y >= 110 && y < 136) {
    if (x >= 1 && x <= 33) { shiftActive = !shiftActive; drawKeysTab(); return; }
    if (x >= 203 && x <= 235) { typeChar(0x08); return; }  // Backspace
    const char* row2 = shiftActive ? "ZXCVBNM" : "zxcvbnm";
    int i = (x - 35) / 24;
    if (i >= 0 && i < 7 && x >= 35 + i * 24 && x <= 35 + i * 24 + 22) typeChar(row2[i]);
    return;
  }
  if (y >= 138 && y < 164) {
    if (x >= 3 && x <= 143) { typeChar(' '); return; }
    if (x >= 147 && x <= 237) { typeChar('\n'); return; }
  }
}

// ── Media tab ──
static const char* MEDIA_LABELS[6] = {"PREV", "PLAY/PAUSE", "NEXT", "VOL -", "MUTE", "VOL +"};
static const int MEDIA_XS[3] = {4, 84, 164};
static const int MEDIA_YS[2] = {58, 120};
#define MEDIA_BTN_W 72
#define MEDIA_BTN_H 54

void drawMediaTab() {
  tft.setTextFont(1);
  int k = 0;
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 3; c++) {
      tft.drawRoundRect(MEDIA_XS[c], MEDIA_YS[r], MEDIA_BTN_W, MEDIA_BTN_H, 4, UI_CYAN);
      tft.setTextColor(UI_CYAN, TFT_BLACK);
      tft.setCursor(MEDIA_XS[c] + 6, MEDIA_YS[r] + MEDIA_BTN_H / 2 - 4);
      tft.print(MEDIA_LABELS[k]);
      k++;
    }
  }
}

void handleMediaTabTouch(int x, int y) {
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 3; c++) {
      if (x >= MEDIA_XS[c] && x <= MEDIA_XS[c] + MEDIA_BTN_W &&
          y >= MEDIA_YS[r] && y <= MEDIA_YS[r] + MEDIA_BTN_H) {
        int idx = r * 3 + c;
        switch (idx) {
          case 0: sendConsumerReport(CONSUMER_PREV); break;
          case 1: sendConsumerReport(CONSUMER_PLAY_PAUSE); break;
          case 2: sendConsumerReport(CONSUMER_NEXT); break;
          case 3: sendConsumerReport(CONSUMER_VOL_DOWN); break;
          case 4: sendConsumerReport(CONSUMER_MUTE); break;
          case 5: sendConsumerReport(CONSUMER_VOL_UP); break;
        }
        return;
      }
    }
  }
}

// ── Presets tab (original scripted-payload feature, preserved) ──
void drawPresetsTab() {
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 62);
  tft.print("Payload:");
  tft.fillRect(10, 74, 220, 10, TFT_BLACK);
  tft.setCursor(10, 74);
  tft.setTextColor(UI_AMBER, TFT_BLACK);
  tft.print(presets[presetIndex].name);

  tft.drawRoundRect(10, 114, 70, 34, 4, UI_CYAN);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(28, 126);
  tft.print("PREV");

  tft.drawRoundRect(85, 114, 70, 34, 4, isConnected() ? UI_AMBER : DARK_GRAY);
  tft.setTextColor(isConnected() ? UI_AMBER : DARK_GRAY, TFT_BLACK);
  tft.setCursor(105, 126);
  tft.print("SEND");

  tft.drawRoundRect(160, 114, 70, 34, 4, UI_CYAN);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(178, 126);
  tft.print("NEXT");
}

void changePresetNext() {
  presetIndex = (presetIndex + 1) % numPresets;
}

void changePresetPrev() {
  presetIndex = (presetIndex - 1 + numPresets) % numPresets;
}

void sendCurrentPreset() {
  if (!isConnected()) return;
  presets[presetIndex].run();
}

void handlePresetsTabTouch(int x, int y) {
  if (y >= 114 && y <= 148) {
    if (x >= 10 && x <= 80) { changePresetPrev(); drawPresetsTab(); return; }
    if (x >= 85 && x <= 155) { sendCurrentPreset(); drawPresetsTab(); return; }
    if (x >= 160 && x <= 230) { changePresetNext(); drawPresetsTab(); return; }
  }
}

// ── Scroll tab (timed auto-scroll for a desktop-browser social feed) ──
// Keyboard-only, per project scope: a BLE HID keyboard/media device can't
// send real touch/swipe gestures - that would need a whole separate BLE
// mouse/pointer HID role, and even then native swipe-gesture mobile apps
// (Instagram/TikTok) generally ignore synthesized pointer input since they
// listen for actual touch events. This drives a Page Down keystroke on a
// timer instead, which works against ordinary browser feeds (X/Twitter,
// Reddit, YouTube, ...) on a paired computer.
const uint8_t KEY_PAGE_DOWN_USAGE = 0x4B;  // USB HID Keyboard/Keypad usage table
const int SCROLL_INTERVALS_S[] = {1, 2, 3, 5, 10};
const int NUM_SCROLL_INTERVALS = sizeof(SCROLL_INTERVALS_S) / sizeof(SCROLL_INTERVALS_S[0]);

bool scrollActive = false;
int scrollIntervalIdx = 1;  // default 2s
unsigned long lastScrollTick = 0;

void drawScrollTab() {
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 60);
  tft.print("Sends Page Down on a timer -");
  tft.setCursor(10, 72);
  tft.print("desktop browser feeds only.");

  tft.fillRect(10, 92, 220, 10, TFT_BLACK);
  tft.setCursor(10, 92);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.print(String("Status: ") + (scrollActive ? "Scrolling..." : "Stopped"));

  tft.drawRoundRect(10, 114, 100, 34, 4, scrollActive ? UI_AMBER : UI_CYAN);
  tft.setTextColor(scrollActive ? UI_AMBER : UI_CYAN, TFT_BLACK);
  tft.setCursor(30, 126);
  tft.print(scrollActive ? "STOP" : "START");

  tft.drawRoundRect(130, 114, 100, 34, 4, scrollActive ? DARK_GRAY : UI_CYAN);
  tft.setTextColor(scrollActive ? DARK_GRAY : UI_CYAN, TFT_BLACK);
  tft.setCursor(145, 126);
  tft.printf("Interval: %ds", SCROLL_INTERVALS_S[scrollIntervalIdx]);
}

void handleScrollTabTouch(int x, int y) {
  if (y < 114 || y > 148) return;
  if (x >= 10 && x <= 110) {
    scrollActive = !scrollActive;
    lastScrollTick = millis();
    drawScrollTab();
    return;
  }
  if (x >= 130 && x <= 230 && !scrollActive) {
    // Interval only adjustable while stopped, to keep a running scroll
    // session's pace predictable.
    scrollIntervalIdx = (scrollIntervalIdx + 1) % NUM_SCROLL_INTERVALS;
    drawScrollTab();
  }
}

void updateScroll() {
  if (!scrollActive || !isConnected()) return;
  unsigned long intervalMs = (unsigned long)SCROLL_INTERVALS_S[scrollIntervalIdx] * 1000;
  if (millis() - lastScrollTick >= intervalMs) {
    sendKeyReport(0, KEY_PAGE_DOWN_USAGE);
    lastScrollTick = millis();
  }
}

// Physical-button shortcuts, active only while on the Presets tab (mirrors
// the original MVP's button-driven preset cycling).
void handleButtons() {
  if (currentTab != RemoteTab::PRESETS) return;

  unsigned long currentMillis = millis();
  if (currentMillis - lastButtonPress < debounceTime) return;

  if (!pcf.digitalRead(BTN_LEFT)) {
    changePresetPrev();
    drawPresetsTab();
    lastButtonPress = currentMillis;
  }
  if (!pcf.digitalRead(BTN_RIGHT)) {
    changePresetNext();
    drawPresetsTab();
    lastButtonPress = currentMillis;
  }
  if (!pcf.digitalRead(BTN_UP)) {
    sendCurrentPreset();
    drawPresetsTab();
    lastButtonPress = currentMillis;
  }
}

void drawContent() {
  tft.fillRect(0, 37, SCREEN_WIDTH, 320 - 37, TFT_BLACK);
  drawConnStatusLine();
  switch (currentTab) {
    case RemoteTab::KEYS: drawKeysTab(); break;
    case RemoteTab::MEDIA: drawMediaTab(); break;
    case RemoteTab::PRESETS: drawPresetsTab(); break;
    case RemoteTab::SCROLL: drawScrollTab(); break;
  }
}

void handleTabBarTouch(int x) {
  if (x >= TAB_BACK_X - 4 && x <= TAB_BACK_X + ICON_SIZE + 4) {
    feature_exit_requested = true;
    return;
  }
  RemoteTab newTab = currentTab;
  if (x >= TAB_KEYS_X && x < TAB_MEDIA_X) newTab = RemoteTab::KEYS;
  else if (x >= TAB_MEDIA_X && x < TAB_PRESETS_X) newTab = RemoteTab::MEDIA;
  else if (x >= TAB_PRESETS_X && x < TAB_SCROLL_X) newTab = RemoteTab::PRESETS;
  else if (x >= TAB_SCROLL_X) newTab = RemoteTab::SCROLL;

  if (newTab != currentTab) {
    currentTab = newTab;
    drawTabBar();
    drawContent();
  }
}

void runUI() {
  if (!uiDrawn) {
    drawTabBar();
    drawContent();
    uiDrawn = true;
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck < 50) return;
  lastTouchCheck = millis();

  if (!feature_active) return;

  // Fire on release, not on every poll tick: a normal tap stays "touched"
  // across several 50ms polls, which used to fire the same keystroke/media
  // command multiple times per tap. Track the last-seen touch point and only
  // dispatch once, on the touched -> released transition.
  static bool wasTouched = false;
  static int lastX = 0, lastY = 0;

  bool touchedNow = ts.touched();
  if (touchedNow) {
    TS_Point p = ts.getPoint();
    lastX = ::map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
    lastY = ::map(p.y, TS_MAXY, TS_MINY, 0, SCREENHEIGHT - 1);
    wasTouched = true;
    return;
  }

  if (!wasTouched) return;
  wasTouched = false;

  if (lastY >= STATUS_BAR_Y_OFFSET && lastY <= STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT + 4) {
    handleTabBarTouch(lastX);
    return;
  }

  switch (currentTab) {
    case RemoteTab::KEYS: handleKeysTabTouch(lastX, lastY); break;
    case RemoteTab::MEDIA: handleMediaTabTouch(lastX, lastY); break;
    case RemoteTab::PRESETS: handlePresetsTabTouch(lastX, lastY); break;
    case RemoteTab::SCROLL: handleScrollTabTouch(lastX, lastY); break;
  }
}

void hidInjectSetup() {
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setTextSize(1);

  uiDrawn = false;
  presetIndex = 0;
  shiftActive = false;
  currentTab = RemoteTab::KEYS;
  advertisingStarted = false;
  scrollActive = false;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);

  if (pServer == nullptr) {
    BLEDevice::init("Quetzal Remote");
    pServer = BLEDevice::createServer();

    // NimBLEDevice owns one process-wide server. Build the HID service only
    // once; recreating it every time this screen opens duplicates services and
    // leaks the previous NimBLEHIDDevice allocation.
    hid = new NimBLEHIDDevice(pServer);
    inputKeyboard = hid->getInputReport(1);  // report ID 1, matches hidReportDescriptor
    inputConsumer = hid->getInputReport(2);  // report ID 2, matches hidReportDescriptor
    hid->setManufacturer("NM-CYD-C5");
    hid->setPnp(0x02, 0xe502, 0xa111, 0x0210);  // sig=USB, arbitrary vendor/product placeholder
    hid->setHidInfo(0x00, 0x01);
    hid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid->getHidService()->getUUID());
    pServer->start();
  }

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->start();
  advertisingStarted = true;

  runUI();
}

void hidInjectLoop() {
  tft.drawLine(0, 19, 240, 19, UI_CYAN);
  handleButtons();

  updateStatusBar();
  updateScroll();  // runs regardless of currentTab, so switching tabs doesn't pause it
  runUI();

  static unsigned long lastStatusRefresh = 0;
  if (millis() - lastStatusRefresh > 1000) {
    drawConnStatusLine();
    lastStatusRefresh = millis();
  }
}

void hidInjectCleanup() {
  if (pServer) {
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    if (adv) adv->stop();
  }
  advertisingStarted = false;
}

}
