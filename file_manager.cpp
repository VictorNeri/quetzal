#include "file_manager.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "utils.h"
#include "buttons_compat.h"
#include <SD.h>
#include <LittleFS.h>
#include <SPI.h>

// ═══════════════════════════════════════════════════════════════════════════
// File Manager - browse LittleFS (internal flash) and the SD card
// ═══════════════════════════════════════════════════════════════════════════
// SD.h and LittleFS.h both derive their filesystem class from fs::FS, so the
// directory listing/navigation code below is written once against an
// fs::FS* and works for either backend.
//
// SD needs the same shared-SPI-bus dance every other SD/RF-HAT feature on
// this board does (see wifi.cpp's performSDUpdate() for the original fix),
// and afterward the touchscreen's own claim on that bus has to be restored
// or touch input stops working for the rest of the session (see
// hw_detect.cpp's detectPeripherals() for the same lesson learned there).
// LittleFS lives on internal flash and doesn't touch that bus at all.
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;
extern ButtonExpander pcf;

namespace FileManager {

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

enum State { STATE_STORAGE_SELECT, STATE_BROWSING };
State state = STATE_STORAGE_SELECT;

enum Storage { STORAGE_NONE, STORAGE_LITTLEFS, STORAGE_SD };
Storage currentStorage = STORAGE_NONE;
bool littlefsMounted = false;
bool sdMounted = false;

String currentPath = "/";

struct Entry {
  String name;
  bool isDir;
  size_t size;
};
#define MAX_ENTRIES 60
Entry entries[MAX_ENTRIES];
int entryCount = 0;
int currentIndex = 0;
int listStartIndex = 0;
String pendingDeletePath;
unsigned long pendingDeleteAt = 0;

int yshift = 30;
static bool uiDrawn = false;
static int iconX[ICON_NUM] = {210, 10};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_nuke,     // delete selected entry (browsing only)
  bitmap_icon_go_back   // up a level / back
};

fs::FS* activeFs() {
  if (currentStorage == STORAGE_SD) return &SD;
  if (currentStorage == STORAGE_LITTLEFS) return &LittleFS;
  return nullptr;
}

bool mountSD() {
  if (sdMounted) return true;

  // touchscreenSPI and the global SPI object are two separate SPIClass
  // instances, but the C5 has only one physical SPI peripheral (FSPI) - both
  // map to it. Without releasing touch's claim first, SPI.begin() below
  // tries to claim an already-initialized bus and silently fails, so
  // SD.begin() never actually reaches the card.
  touchscreenSPI.end();
  delay(10);

  SPI.begin(BOARD_RADIO_SCK, BOARD_RADIO_MISO, BOARD_RADIO_MOSI, BOARD_SD_CS);
  sdMounted = SD.begin(BOARD_SD_CS);
  setupTouchscreen();  // restore touch's claim on the shared bus either way
  return sdMounted;
}

bool mountLittleFS() {
  if (littlefsMounted) return true;
  // Never auto-format on a mount error: corruption, partition mismatch, or a
  // transient failure must not silently destroy the user's files.
  littlefsMounted = LittleFS.begin(false);
  return littlefsMounted;
}

String humanSize(size_t bytes) {
  if (bytes < 1024) return String(bytes) + "B";
  if (bytes < 1024 * 1024) return String(bytes / 1024.0, 1) + "K";
  return String(bytes / (1024.0 * 1024.0), 1) + "M";
}

String parentPath(const String& path) {
  if (path == "/") return "/";
  int slash = path.lastIndexOf('/');
  if (slash <= 0) return "/";
  return path.substring(0, slash);
}

void listCurrentDir() {
  entryCount = 0;
  currentIndex = 0;
  listStartIndex = 0;
  fs::FS* fsPtr = activeFs();
  if (!fsPtr) return;

  File dir = fsPtr->open(currentPath);
  if (!dir || !dir.isDirectory()) return;

  File f = dir.openNextFile();
  while (f && entryCount < MAX_ENTRIES) {
    String nm = String(f.name());
    int slash = nm.lastIndexOf('/');
    if (slash >= 0) nm = nm.substring(slash + 1);
    if (nm.length() > 0) {
      entries[entryCount].name = nm;
      entries[entryCount].isDir = f.isDirectory();
      entries[entryCount].size = f.size();
      entryCount++;
    }
    f = dir.openNextFile();
  }
  dir.close();
}

void enterStorage(Storage s) {
  currentStorage = s;
  currentPath = "/";
  state = STATE_BROWSING;
  listCurrentDir();
}

void deleteSelected() {
  if (entryCount == 0) return;
  fs::FS* fsPtr = activeFs();
  if (!fsPtr) return;

  String fullPath = (currentPath == "/") ? "/" + entries[currentIndex].name
                                          : currentPath + "/" + entries[currentIndex].name;

  // Require a distinct second press so an accidental tap cannot delete data.
  // Wait for release after arming to prevent one long touch from confirming
  // itself on the next UI poll.
  if (pendingDeletePath != fullPath || millis() - pendingDeleteAt > 5000) {
    pendingDeletePath = fullPath;
    pendingDeleteAt = millis();
    tft.fillRect(0, 37, 240, 30, TFT_BLACK);
    tft.setTextFont(1);
    tft.setTextColor(UI_AMBER, TFT_BLACK);
    tft.setCursor(10, 15 + yshift);
    tft.print("Tap delete again to confirm");
    unsigned long releaseStart = millis();
    while (ts.touched() && millis() - releaseStart < 2000) delay(10);
    return;
  }

  pendingDeletePath = "";
  pendingDeleteAt = 0;
  bool ok = entries[currentIndex].isDir ? fsPtr->rmdir(fullPath) : fsPtr->remove(fullPath);

  tft.fillRect(0, 37, 240, 30, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(ok ? UI_GREEN : TFT_RED, TFT_BLACK);
  tft.setCursor(10, 15 + yshift);
  tft.print(ok ? "Deleted." : "Delete failed (dir not empty?)");
  delay(700);

  listCurrentDir();
}

void drawStorageSelect() {
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 15 + yshift);
  tft.print("Select storage:");

  tft.setTextFont(1);

  tft.setCursor(15, 55 + yshift);
  tft.setTextColor(currentIndex == 0 ? UI_AMBER : UI_CYAN, TFT_BLACK);
  tft.print(String(currentIndex == 0 ? "> " : "  ") + "LittleFS (internal flash)");

  tft.setCursor(15, 80 + yshift);
  tft.setTextColor(currentIndex == 1 ? UI_AMBER : UI_CYAN, TFT_BLACK);
  tft.print(String(currentIndex == 1 ? "> " : "  ") + "SD Card");

  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(15, 110 + yshift);
  tft.print("Tap > to open, < to exit");
}

void updateBrowserList() {
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN);
  tft.setCursor(5, 24);
  String header = (currentStorage == STORAGE_SD ? "SD:" : "FS:") + currentPath;
  if (header.length() > 34) header = header.substring(0, 31) + "...";
  tft.print(header);

  if (entryCount == 0) {
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.setCursor(10, 15 + yshift);
    tft.print("(empty)");
    return;
  }

  for (int i = 0; i < 13; i++) {
    int index = i + listStartIndex;
    if (index >= entryCount) break;

    int yPos = 10 + i * 18;
    tft.fillRect(0, yPos - 2 + yshift, tft.width(), 18, TFT_BLACK);

    String line;
    if (entries[index].isDir) {
      line = entries[index].name + "/";
    } else {
      line = entries[index].name + " (" + humanSize(entries[index].size) + ")";
    }
    if (line.length() > 34) line = line.substring(0, 31) + "...";

    tft.setCursor(10, yPos + yshift);
    tft.setTextColor(index == currentIndex ? UI_AMBER : UI_CYAN, TFT_BLACK);
    tft.print((index == currentIndex ? "> " : "  ") + line);
  }
}

void selectPrev() {
  if (state == STATE_STORAGE_SELECT) {
    currentIndex = (currentIndex == 0) ? 1 : 0;
    drawStorageSelect();
  } else if (currentIndex > 0) {
    currentIndex--;
    if (currentIndex < listStartIndex) listStartIndex--;
    updateBrowserList();
  }
}

void selectNext() {
  if (state == STATE_STORAGE_SELECT) {
    currentIndex = (currentIndex == 0) ? 1 : 0;
    drawStorageSelect();
  } else if (currentIndex < entryCount - 1) {
    currentIndex++;
    if (currentIndex >= listStartIndex + 13) listStartIndex++;
    updateBrowserList();
  }
}

void activateSelected() {
  if (state == STATE_STORAGE_SELECT) {
    if (currentIndex == 0) {
      tft.fillRect(0, 37, 240, 30, TFT_BLACK);
      tft.setTextColor(UI_CYAN, TFT_BLACK);
      tft.setCursor(10, 15 + yshift);
      tft.print("Mounting LittleFS...");
      if (mountLittleFS()) {
        enterStorage(STORAGE_LITTLEFS);
        updateBrowserList();
      } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(10, 35 + yshift);
        tft.print("LittleFS mount failed.");
        delay(1000);
        drawStorageSelect();
      }
    } else {
      tft.fillRect(0, 37, 240, 30, TFT_BLACK);
      tft.setTextColor(UI_CYAN, TFT_BLACK);
      tft.setCursor(10, 15 + yshift);
      tft.print("Mounting SD card...");
      if (mountSD()) {
        enterStorage(STORAGE_SD);
        updateBrowserList();
      } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(10, 35 + yshift);
        tft.print("SD card not found.");
        delay(1000);
        drawStorageSelect();
      }
    }
    return;
  }

  // Browsing: entering a directory is the only "activate" action for now -
  // there's no viewer for file contents yet.
  if (entryCount == 0) return;
  if (entries[currentIndex].isDir) {
    currentPath = (currentPath == "/") ? "/" + entries[currentIndex].name
                                        : currentPath + "/" + entries[currentIndex].name;
    listCurrentDir();
    updateBrowserList();
  }
}

void goBack() {
  if (state == STATE_STORAGE_SELECT) {
    feature_exit_requested = true;
    return;
  }
  if (currentPath == "/") {
    state = STATE_STORAGE_SELECT;
    currentIndex = 0;
    drawStorageSelect();
  } else {
    currentPath = parentPath(currentPath);
    listCurrentDir();
    updateBrowserList();
  }
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
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_CYAN);
      animationState = 2;
      switch (activeIcon) {
        case 0: if (state == STATE_BROWSING) deleteSelected(); updateBrowserList(); break;
        case 1: goBack(); break;
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
      } else {
        // Tapping the list area activates the current selection, matching
        // the RIGHT button; a second tap on an already-focused row would be
        // redundant so this is deliberately simple (no per-row hit test).
        activateSelected();
      }
    }
    lastTouchCheck = millis();
  }
}

void fileManagerSetup() {
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setTextSize(1);

  uiDrawn = false;
  state = STATE_STORAGE_SELECT;
  currentStorage = STORAGE_NONE;
  currentPath = "/";
  currentIndex = 0;
  entryCount = 0;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);

  runUI();
  drawStorageSelect();
}

void fileManagerLoop() {
  tft.drawLine(0, 19, 240, 19, UI_CYAN);
  updateStatusBar();
  runUI();

  static unsigned long lastButtonPress = 0;
  if (millis() - lastButtonPress < 200) return;

  if (!pcf.digitalRead(BTN_UP)) { selectPrev(); lastButtonPress = millis(); }
  if (!pcf.digitalRead(BTN_DOWN)) { selectNext(); lastButtonPress = millis(); }
  if (!pcf.digitalRead(BTN_RIGHT)) { activateSelected(); lastButtonPress = millis(); }
  if (!pcf.digitalRead(BTN_LEFT)) { goBack(); lastButtonPress = millis(); }
}

void fileManagerCleanup() {
  if (sdMounted) {
    SD.end();
    sdMounted = false;
  }
  // LittleFS is left mounted (internal flash, no shared-bus conflict, and
  // other features don't touch it) - no need to unmount on exit.
}

}  // namespace FileManager
