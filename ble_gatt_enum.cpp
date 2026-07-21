#include "ble_gatt_enum.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "utils.h"
#include "buttons_compat.h"
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════
// GATT Service/Characteristic Enumerator
// ═══════════════════════════════════════════════════════════════════════════
// Scans for nearby BLE peripherals (same passive-scan flow as BleScan), then
// connects to a selected device as a GATT client and lists its services and
// characteristics. This is the first central/client-role feature in the
// firmware - BleScan only ever does passive advertisement scanning.
//
// UI conventions (list/detail view, icon layout, button semantics) mirror
// BleScan's in bluetooth.cpp: BTN_RIGHT enumerates the selected device /
// returns from the characteristic list, BTN_LEFT rescans / disconnects.
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;
extern ButtonExpander pcf;

namespace GattEnum {

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

enum State { STATE_SCANNING, STATE_DEVICE_LIST, STATE_CONNECTING, STATE_CHAR_LIST, STATE_ERROR };

BLEScan* bleScan = nullptr;
BLEScanResults bleResults;
BLEClient* client = nullptr;
State state = STATE_SCANNING;

int currentIndex = 0;
int listStartIndex = 0;
bool screenNeedsUpdate = true;
bool fullScreenUpdate = true;
String errorMessage;

int yshift = 30;

unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

static bool uiDrawn = false;

static int iconX[ICON_NUM] = {210, 10};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_undo,
  bitmap_icon_go_back // Added back icon
};

// Flattened rows built after a successful connect: one row per service
// header plus one row per characteristic underneath it.
struct GattRow {
  bool isService;
  String label;
};
#define MAX_GATT_ROWS 64
GattRow gattRows[MAX_GATT_ROWS];
int gattRowCount = 0;

void displayScanning() {
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 15 + yshift);
  tft.print("[*] Scanning");

  for (int i = 0; i < 2; i++) {
    for (int j = 0; j <= i; j++) {
      tft.print(".");
      delay(500);
    }
  }
  tft.setCursor(10, 25 + yshift);
  tft.print("[+] Scan complete!");

  delay(100);
  state = STATE_DEVICE_LIST;
  screenNeedsUpdate = true;
  fullScreenUpdate = true;
}

void startScan() {
  state = STATE_SCANNING;
  screenNeedsUpdate = true;
  fullScreenUpdate = true;
  displayScanning();
  // getResults(duration, isContinue) blocks until the scan completes and
  // returns the populated results - start()+getResults() would return
  // nothing, since start() is fire-and-forget (see BleScan::startBLEScan()
  // in bluetooth.cpp for the full explanation).
  bleResults = bleScan->getResults(5000, false);
  currentIndex = 0;
  listStartIndex = 0;
  screenNeedsUpdate = true;
  fullScreenUpdate = true;
}

void disconnectClient() {
  if (client != nullptr) {
    if (client->isConnected()) client->disconnect();
    BLEDevice::deleteClient(client);
    client = nullptr;
  }
}

// Connects to the selected device and walks its services/characteristics
// into gattRows[]. Runs synchronously (blocks the UI loop) like the rest of
// this firmware's other BLE operations.
void enumerateSelectedDevice() {
  if (bleResults.getCount() <= 0) return;

  state = STATE_CONNECTING;
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 15 + yshift);
  tft.print("[*] Connecting...");

  const BLEAdvertisedDevice& device = *bleResults.getDevice(currentIndex);

  disconnectClient();
  client = BLEDevice::createClient();

  if (!client->connect(device.getAddress())) {
    errorMessage = "Connect failed";
    BLEDevice::deleteClient(client);
    client = nullptr;
    state = STATE_ERROR;
    screenNeedsUpdate = true;
    fullScreenUpdate = true;
    return;
  }

  tft.setCursor(10, 25 + yshift);
  tft.print("[*] Reading GATT table...");

  gattRowCount = 0;
  const std::vector<BLERemoteService*>& services = client->getServices(true);
  for (auto* service : services) {
    if (gattRowCount >= MAX_GATT_ROWS) break;
    gattRows[gattRowCount].isService = true;
    gattRows[gattRowCount].label = String(service->getUUID().toString().c_str());
    gattRowCount++;

    const std::vector<BLERemoteCharacteristic*>& chars = service->getCharacteristics(true);
    for (auto* ch : chars) {
      if (gattRowCount >= MAX_GATT_ROWS) break;
      String flags = "";
      if (ch->canRead()) flags += "R";
      if (ch->canWrite()) flags += "W";
      if (ch->canNotify()) flags += "N";
      if (ch->canIndicate()) flags += "I";
      gattRows[gattRowCount].isService = false;
      gattRows[gattRowCount].label = String(ch->getUUID().toString().c_str()) +
                                      (flags.length() ? " [" + flags + "]" : "");
      gattRowCount++;
    }
  }

  if (gattRowCount == 0) {
    gattRows[0].isService = true;
    gattRows[0].label = "(no services found)";
    gattRowCount = 1;
  }

  currentIndex = 0;
  listStartIndex = 0;
  state = STATE_CHAR_LIST;
  screenNeedsUpdate = true;
  fullScreenUpdate = true;
}

void handleButtons() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastButtonPress < debounceTime) return;

  int listCount = (state == STATE_CHAR_LIST) ? gattRowCount : bleResults.getCount();

  if (!pcf.digitalRead(BTN_UP)) {
    if ((state == STATE_DEVICE_LIST || state == STATE_CHAR_LIST) && currentIndex > 0) {
      currentIndex--;
      delay(200);
      if (currentIndex < listStartIndex) listStartIndex--;
      screenNeedsUpdate = true;
      fullScreenUpdate = false;
    }
    lastButtonPress = currentMillis;
  }

  if (!pcf.digitalRead(BTN_DOWN)) {
    if ((state == STATE_DEVICE_LIST || state == STATE_CHAR_LIST) && currentIndex < listCount - 1) {
      currentIndex++;
      delay(200);
      if (currentIndex >= listStartIndex + 14) listStartIndex++;
      screenNeedsUpdate = true;
      fullScreenUpdate = false;
    }
    lastButtonPress = currentMillis;
  }

  if (!pcf.digitalRead(BTN_RIGHT)) {
    delay(200);
    if (state == STATE_DEVICE_LIST) {
      enumerateSelectedDevice();
    }
    lastButtonPress = currentMillis;
  }

  if (!pcf.digitalRead(BTN_LEFT)) {
    delay(200);
    if (state == STATE_CHAR_LIST || state == STATE_ERROR) {
      disconnectClient();
      currentIndex = 0;
      listStartIndex = 0;
      state = STATE_DEVICE_LIST;
      screenNeedsUpdate = true;
      fullScreenUpdate = true;
    } else if (state == STATE_DEVICE_LIST) {
      startScan();
    }
    lastButtonPress = currentMillis;
  }
}

void updateDeviceList() {
  if (fullScreenUpdate) {
    tft.fillRect(0, 37, 240, 320, TFT_BLACK);
    tft.fillRect(35, 20, 140, 16, DARK_GRAY);
    tft.setTextColor(UI_CYAN);
    tft.setCursor(35, 24);
    tft.print("Select, then >");
  }

  int deviceCount = bleResults.getCount();
  if (deviceCount <= 0) {
    if (fullScreenUpdate) {
      tft.fillRect(0, 20, 140, 16, DARK_GRAY);
      tft.setTextColor(UI_CYAN);
      tft.setCursor(5, 24);
      tft.print("No Devices Found");
    }
    return;
  }

  for (int i = 0; i < 14; i++) {
    int index = i + listStartIndex;
    if (index >= deviceCount) break;

    int yPos = 15 + i * 18;
    tft.fillRect(0, yPos - 2 + yshift, tft.width(), 18, TFT_BLACK);

    const BLEAdvertisedDevice& device = *bleResults.getDevice(index);
    String deviceName = device.getName().length() > 0 ? device.getName().c_str() : "Unknown Device";

    tft.setCursor(10, yPos + yshift);
    if (index == currentIndex) {
      tft.setTextColor(ORANGE, TFT_BLACK);
      tft.print("> " + deviceName);
    } else {
      tft.setTextColor(UI_CYAN, TFT_BLACK);
      tft.print("  " + deviceName);
    }
  }
}

void updateCharList() {
  if (fullScreenUpdate) {
    tft.fillRect(0, 37, 240, 320, TFT_BLACK);
    tft.fillRect(35, 20, 140, 16, DARK_GRAY);
    tft.setTextColor(UI_CYAN);
    tft.setCursor(35, 24);
    tft.print("GATT Table (< back)");
  }

  for (int i = 0; i < 14; i++) {
    int index = i + listStartIndex;
    if (index >= gattRowCount) break;

    int yPos = 15 + i * 18;
    tft.fillRect(0, yPos - 2 + yshift, tft.width(), 18, TFT_BLACK);

    tft.setCursor(gattRows[index].isService ? 5 : 15, yPos + yshift);
    tft.setTextFont(1);
    if (index == currentIndex) {
      tft.setTextColor(ORANGE, TFT_BLACK);
      tft.print((gattRows[index].isService ? "S " : "  ") + gattRows[index].label);
    } else {
      tft.setTextColor(gattRows[index].isService ? UI_MAGENTA : UI_CYAN, TFT_BLACK);
      tft.print((gattRows[index].isService ? "S " : "  ") + gattRows[index].label);
    }
  }
}

void displayError() {
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(10, 15 + yshift);
  tft.print("[!] " + errorMessage);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 35 + yshift);
  tft.print("Touch < to go back");
}

void runUI() {

  static int iconY = STATUS_BAR_Y_OFFSET;

  if (!uiDrawn) {
    tft.drawLine(0, 19, 240, 19, UI_CYAN);
    tft.fillRect(140, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 140, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_CYAN);
      }
    }
    tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
    uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_CYAN);
      animationState = 2;

      switch (activeIcon) {
        case 0: // rescan (device list only)
          if (state == STATE_DEVICE_LIST) startScan();
          break;
        case 1: // back icon: exit to submenu, or step back from the char list
          if (state == STATE_CHAR_LIST || state == STATE_ERROR) {
            disconnectClient();
            currentIndex = 0;
            listStartIndex = 0;
            state = STATE_DEVICE_LIST;
            screenNeedsUpdate = true;
            fullScreenUpdate = true;
          } else {
            feature_exit_requested = true;
          }
          break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  const unsigned long touchCheckInterval = 50;

  if (millis() - lastTouchCheck >= touchCheckInterval) {
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
      } else if (state == STATE_DEVICE_LIST) {
        // Tapping anywhere in the list area enumerates the selected device,
        // mirroring BTN_RIGHT.
        enumerateSelectedDevice();
      }
    }
    lastTouchCheck = millis();
  }
}

void gattEnumSetup() {
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect(0, 20, 140, 16, DARK_GRAY);

  uiDrawn = false;
  gattRowCount = 0;
  client = nullptr;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);
  runUI();

  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);

  BLEDevice::init("");
  bleScan = BLEDevice::getScan();
  bleScan->setActiveScan(true);

  startScan();
}

void gattEnumLoop() {
  tft.drawLine(0, 19, 240, 19, UI_CYAN);
  handleButtons();

  updateStatusBar();
  runUI();

  if (screenNeedsUpdate) {
    screenNeedsUpdate = false;
    switch (state) {
      case STATE_SCANNING:      displayScanning(); break;
      case STATE_DEVICE_LIST:   updateDeviceList(); break;
      case STATE_CONNECTING:    break; // enumerateSelectedDevice() draws its own progress text
      case STATE_CHAR_LIST:     updateCharList(); break;
      case STATE_ERROR:         displayError(); break;
    }
    if (fullScreenUpdate) fullScreenUpdate = false;
  }
}

// Called when the submenu dispatcher exits this feature (mirrors
// BleSniffer::blesnifferCleanup()) so a stray connection doesn't linger.
void gattEnumCleanup() {
  disconnectClient();
}

}
