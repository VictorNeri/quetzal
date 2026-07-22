#include <Arduino.h>
#include "board_config.h"
#include <TFT_eSPI.h>
#include <Wire.h>
#include "buttons_compat.h"  // touch-backed ButtonExpander (NM-CYD-C5 has no physical buttons)

#include "Touchscreen.h"
#include "wificonfig.h"
#include "bleconfig.h"
#include "ble_gatt_enum.h"
#include "ble_hid_inject.h"
#include "zigbee.h"
#include "hw_detect.h"
#include "file_manager.h"
#include "rgb_light.h"
#include "host_scanner.h"
#include "espnow_test.h"
#include "wifi_assessment.h"
#include "touch_calibration.h"
#include "subconfig.h"
#include "utils.h"
#include "shared.h"
#include "icon.h"

TFT_eSPI tft = TFT_eSPI();

ButtonExpander pcf;

#define BTN_UP     BOARD_BUTTON_UP
#define BTN_DOWN   BOARD_BUTTON_DOWN
#define BTN_LEFT   BOARD_BUTTON_LEFT
#define BTN_RIGHT  BOARD_BUTTON_RIGHT
#define BTN_SELECT BOARD_BUTTON_SELECT

bool feature_exit_requested = false;

const int NUM_MENU_ITEMS = 8;
const char *menu_items[NUM_MENU_ITEMS] = {
    "WiFi",
    "Bluetooth",
    "2.4GHz",
    "SubGHz",
    "ESP-NOW",
    "Tools",
    "Setting",
    "About"};

const unsigned char *bitmap_icons[NUM_MENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_ble,
    bitmap_icon_signals,
    bitmap_icon_antenna,
    bitmap_icon_signal,
    bitmap_icon_bash,
    bitmap_icon_setting,
    bitmap_icon_question};

int current_menu_index = 0;
bool is_main_menu = false;


const int NUM_SUBMENU_ITEMS = 9;
const char *submenu_items[NUM_SUBMENU_ITEMS] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Deauth Detector",
    "WiFi Scanner",
    "Captive Portal",
    "Host Scanner",
    "Assessment Suite",
    "Back to Main Menu"};


const int bluetooth_NUM_SUBMENU_ITEMS = 7;
const char *bluetooth_submenu_items[bluetooth_NUM_SUBMENU_ITEMS] = {
    "BLE Jammer",
    "BLE Spoofer",
    "Sour Apple",
    "GATT Enum",
    "BLE Scanner",
    "BLE Remote",
    "Back to Main Menu"};


const int nrf_NUM_SUBMENU_ITEMS = 7;
const char *nrf_submenu_items[nrf_NUM_SUBMENU_ITEMS] = {
    "Scanner",
    "Spectrum Analyzer",
    "WLAN Jammer",
    "Proto Kill",
    "Zigbee Scan",
    "Zigbee Sniff",
    "Back to Main Menu"};


const int subghz_NUM_SUBMENU_ITEMS = 5;
const char *subghz_submenu_items[subghz_NUM_SUBMENU_ITEMS] = {
    "Replay Attack",
    "Brute Force",
    "SubGHz Jammer",
    "Saved Profile",
    "Back to Main Menu"};


const int tools_NUM_SUBMENU_ITEMS = 5;
const char *tools_submenu_items[tools_NUM_SUBMENU_ITEMS] = {
    "Serial Monitor",
    "Update Firmware",
    "File Manager",
    "RGB Light",
    "Back to Main Menu"};


const int settings_NUM_SUBMENU_ITEMS = 5;
const char *settings_submenu_items[settings_NUM_SUBMENU_ITEMS] = {
    "Brightness",
    "Screen Timeout",
    "Device Info",
    "Touch Calibration",
    "Back to Main Menu"};


const int espnow_NUM_SUBMENU_ITEMS = 3;
const char *espnow_submenu_items[espnow_NUM_SUBMENU_ITEMS] = {
    "Broadcast Test",
    "Receive Test",
    "Back to Main Menu"};


const int about_NUM_SUBMENU_ITEMS = 1;
const char *about_submenu_items[about_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

int current_submenu_index = 0;
bool in_sub_menu = false;

const char **active_submenu_items = nullptr;
int active_submenu_size = 0;

// Which external RF-HAT chip (if any) each submenu entry needs. Entries are
// dimmed and blocked from entry in displaySubmenu()/handleButtons() when the
// required chip wasn't found by detectPeripherals() at boot (hw_detect.h).
const HwReq bluetooth_submenu_hwreq[bluetooth_NUM_SUBMENU_ITEMS] = {
    HW_NRF24,  // BLE Jammer (NRF24-based despite the name)
    HW_NONE,   // BLE Spoofer
    HW_NONE,   // Sour Apple
    HW_NONE,   // GATT Enum
    HW_NONE,   // BLE Scanner
    HW_NONE,   // BLE Remote
    HW_NONE};  // Back to Main Menu

const HwReq nrf_submenu_hwreq[nrf_NUM_SUBMENU_ITEMS] = {
    HW_NRF24,  // Scanner
    HW_NRF24,  // Spectrum Analyzer
    HW_NRF24,  // WLAN Jammer
    HW_NRF24,  // Proto Kill
    HW_NONE,   // Zigbee Scan (onboard 802.15.4)
    HW_NONE,   // Zigbee Sniff (onboard 802.15.4)
    HW_NONE};  // Back to Main Menu

const HwReq subghz_submenu_hwreq[subghz_NUM_SUBMENU_ITEMS] = {
    HW_CC1101,  // Replay Attack
    HW_CC1101,  // Brute Force
    HW_CC1101,  // SubGHz Jammer
    HW_CC1101,  // Saved Profile
    HW_NONE};   // Back to Main Menu

const HwReq *active_submenu_hwreq = nullptr;


const unsigned char *wifi_submenu_icons[NUM_SUBMENU_ITEMS] = {
    bitmap_icon_wifi,         // Packet Monitor
    bitmap_icon_antenna,      // Beacon Spammer
    bitmap_icon_wifi_jammer,  // WiFi Deauther
    bitmap_icon_eye2,         // Deauth Detector
    bitmap_icon_jammer,       // WiFi Scanner
    bitmap_icon_bash,         // Captive Portal
    bitmap_icon_scanner,      // Host Scanner
    bitmap_icon_analyzer,     // Assessment Suite
    bitmap_icon_go_back
};

const unsigned char *bluetooth_submenu_icons[bluetooth_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_ble_jammer,  // BLE Jammer
    bitmap_icon_spoofer,     // BLE Spoofer
    bitmap_icon_apple,       // Sour Apple
    bitmap_icon_list,        // GATT Enum
    bitmap_icon_graph,       // BLE Scanner
    bitmap_icon_key,         // BLE Remote
    bitmap_icon_go_back
};

const unsigned char *nrf_submenu_icons[nrf_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_scanner,      // Scanner
    bitmap_icon_analyzer,     // Spectrum Analyzer
    bitmap_icon_wifi_jammer,  // WLAN Jammer
    bitmap_icon_kill,         // Proto Kill
    bitmap_icon_signal,       // Zigbee Scan
    bitmap_icon_eye2,         // Zigbee Sniff
    bitmap_icon_go_back
};

const unsigned char *subghz_submenu_icons[subghz_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_antenna,   // Replay Attack
    bitmap_icon_key,       // Brute Force
    bitmap_icon_no_signal, // SubGHz Jammer
    bitmap_icon_list,      // Saved Profile
    bitmap_icon_go_back
};

const unsigned char *tools_submenu_icons[tools_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_bash,     // Serial Monitor
    bitmap_icon_follow,   // Update Firmware
    bitmap_icon_floppy,   // File Manager
    bitmap_icon_led,      // RGB Light
    bitmap_icon_go_back
};

const unsigned char *settings_submenu_icons[settings_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_led,      // Brightness
    bitmap_icon_eye2,     // Screen Timeout
    bitmap_icon_stat,     // Device Info
    bitmap_icon_setting,  // Touch Calibration
    bitmap_icon_go_back
};

const unsigned char *espnow_submenu_icons[espnow_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_signal,
    bitmap_icon_eye2,
    bitmap_icon_go_back
};

const unsigned char *about_submenu_icons[about_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};


const unsigned char **active_submenu_icons = nullptr;

void updateActiveSubmenu() {
    switch (current_menu_index) {
        case 0: // WiFi
            active_submenu_items = submenu_items;
            active_submenu_size = NUM_SUBMENU_ITEMS;
            active_submenu_icons = wifi_submenu_icons;
            active_submenu_hwreq = nullptr;
            break;
        case 1: // Bluetooth
            active_submenu_items = bluetooth_submenu_items;
            active_submenu_size = bluetooth_NUM_SUBMENU_ITEMS;
            active_submenu_icons = bluetooth_submenu_icons;
            active_submenu_hwreq = bluetooth_submenu_hwreq;
            break;
        case 2: // 2.4GHz (NRF)
            active_submenu_items = nrf_submenu_items;
            active_submenu_size = nrf_NUM_SUBMENU_ITEMS;
            active_submenu_icons = nrf_submenu_icons;
            active_submenu_hwreq = nrf_submenu_hwreq;
            break;
        case 3: // SubGHz
            active_submenu_items = subghz_submenu_items;
            active_submenu_size = subghz_NUM_SUBMENU_ITEMS;
            active_submenu_icons = subghz_submenu_icons;
            active_submenu_hwreq = subghz_submenu_hwreq;
            break;
        case 4: // ESP-NOW
            active_submenu_items = espnow_submenu_items;
            active_submenu_size = espnow_NUM_SUBMENU_ITEMS;
            active_submenu_icons = espnow_submenu_icons;
            active_submenu_hwreq = nullptr;
            break;
        case 5: // Tools
            active_submenu_items = tools_submenu_items;
            active_submenu_size = tools_NUM_SUBMENU_ITEMS;
            active_submenu_icons = tools_submenu_icons;
            active_submenu_hwreq = nullptr;
            break;
        case 6: // Settings
            active_submenu_items = settings_submenu_items;
            active_submenu_size = settings_NUM_SUBMENU_ITEMS;
            active_submenu_icons = settings_submenu_icons;
            active_submenu_hwreq = nullptr;
            break;
        case 7: // About
            active_submenu_items = about_submenu_items;
            active_submenu_size = about_NUM_SUBMENU_ITEMS;
            active_submenu_icons = about_submenu_icons;
            active_submenu_hwreq = nullptr;
            break;

        default:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            active_submenu_hwreq = nullptr;
            break;
    }
}

bool isButtonPressed(int buttonPin) {
  // On the C5 `pcf` is the touch-backed ButtonExpander, so this works there too.
  return !pcf.digitalRead(buttonPin);
}

float currentBatteryVoltage = 0.0;  // Initialize to 0, updated in loop() by readBatteryVoltage()
unsigned long last_interaction_time = 0;


/*
#define BACKLIGHT_PIN 4

const unsigned long BACKLIGHT_TIMEOUT = 100000;

void manageBacklight() {
  if (millis() - last_interaction_time > BACKLIGHT_TIMEOUT) {
    digitalWrite(BACKLIGHT_PIN, LOW);
  } else {
    digitalWrite(BACKLIGHT_PIN, HIGH);
  }
}
*/


int last_submenu_index = -1;
bool submenu_initialized = false;
int last_menu_index = -1;
bool menu_initialized = false;


// Items whose required RF-HAT chip wasn't detected at boot render gray
// (both icon and text) regardless of selection, instead of the usual
// cyan/orange, so it reads as visibly disabled.
bool submenuItemHwAvailable(int i) {
    if (active_submenu_hwreq == nullptr) return true;
    return hwReqSatisfied(active_submenu_hwreq[i]);
}

void displaySubmenu() {
    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);
    tft.setTextSize(1);

    if (!submenu_initialized) {
        tft.fillScreen(TFT_BLACK);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            uint16_t itemColor = submenuItemHwAvailable(i) ? UI_CYAN : GRAY;
            tft.setTextColor(itemColor, TFT_BLACK);
            tft.drawBitmap(10, yPos, active_submenu_icons[i], 16, 16, itemColor);
            tft.setCursor(30, yPos);
            if (i < active_submenu_size - 1) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[i]);
        }

        submenu_initialized = true;
        last_submenu_index = -1;
    }

    if (last_submenu_index != current_submenu_index) {
        if (last_submenu_index >= 0) {
            int prev_yPos = 30 + last_submenu_index * 30;
            if (last_submenu_index == active_submenu_size - 1) prev_yPos += 10;

            uint16_t prevColor = submenuItemHwAvailable(last_submenu_index) ? UI_CYAN : GRAY;
            tft.setTextColor(prevColor, TFT_BLACK);
            tft.drawBitmap(10, prev_yPos, active_submenu_icons[last_submenu_index], 16, 16, prevColor);
            tft.setCursor(30, prev_yPos);
            if (last_submenu_index < active_submenu_size - 1) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[last_submenu_index]);
        }

        int new_yPos = 30 + current_submenu_index * 30;
        if (current_submenu_index == active_submenu_size - 1) new_yPos += 10;

        uint16_t curColor = submenuItemHwAvailable(current_submenu_index) ? UI_AMBER : GRAY;
        tft.setTextColor(curColor, TFT_BLACK);
        tft.drawBitmap(10, new_yPos, active_submenu_icons[current_submenu_index], 16, 16, curColor);
        tft.setCursor(30, new_yPos);
        if (current_submenu_index < active_submenu_size - 1) {
            tft.print("| ");
        }
        tft.print(active_submenu_items[current_submenu_index]);

        last_submenu_index = current_submenu_index;
    }

    drawStatusBar(currentBatteryVoltage, true);
}

const int COLUMN_WIDTH = 120;
const int X_OFFSET_LEFT = 10;
const int X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH;
const int Y_START = 30;
const int Y_SPACING = 75;

void displayMenu() {

const uint16_t icon_colors[NUM_MENU_ITEMS] = {
  0xFFFF, // WiFi
  0xFFFF, // Bluetooth
  0xFFFF, // 2.4GHz
  0xFFFF, // SubGHz
  0xFFFF, // ESP-NOW
  0xFFFF, // Tools
  0x8410, // Setting
  0xFFFF  // About
};

    submenu_initialized = false;
    last_submenu_index = -1;
    tft.setTextFont(2);

    if (!menu_initialized) {
        // Plain black background; a proper themed background is still TBD.
        tft.fillScreen(TFT_BLACK);

        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            // Clear/transparent button - just border, no fill
            tft.drawRoundRect(x_position, y_position, 100, 60, 5, UI_CYAN);
            tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[i], 16, 16, UI_CYAN);

            tft.setTextColor(UI_CYAN);  // Transparent background
            int textWidth = 6 * strlen(menu_items[i]);
            int textX = x_position + (100 - textWidth) / 2;
            int textY = y_position + 30;
            tft.setCursor(textX, textY);
            tft.print(menu_items[i]);
        }
        menu_initialized = true;
        last_menu_index = -1;
    }

    if (last_menu_index != current_menu_index) {
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_menu_index) {
                // Deselected - redraw border in cyan (erase magenta border)
                tft.drawRoundRect(x_position, y_position, 100, 60, 5, UI_CYAN);
                tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[last_menu_index], 16, 16, UI_CYAN);
                tft.setTextColor(UI_CYAN);
                int textWidth = 6 * strlen(menu_items[last_menu_index]);
                int textX = x_position + (100 - textWidth) / 2;
                int textY = y_position + 30;
                tft.setCursor(textX, textY);
                tft.print(menu_items[last_menu_index]);
            }
        }

        int column = current_menu_index / 4;
        int row = current_menu_index % 4;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        // Selected button - amber border and text
        tft.drawRoundRect(x_position, y_position, 100, 60, 5, UI_AMBER);
        tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[current_menu_index], 16, 16, UI_AMBER);
        tft.setTextColor(UI_AMBER);
        int textWidth = 6 * strlen(menu_items[current_menu_index]);
        int textX = x_position + (100 - textWidth) / 2;
        int textY = y_position + 30;
        tft.setCursor(textX, textY);
        tft.print(menu_items[current_menu_index]);

        last_menu_index = current_menu_index;
    }
    drawStatusBar(currentBatteryVoltage, true);
}


void handleWiFiSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 8) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            PacketMonitor::ptmSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                PacketMonitor::ptmLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BeaconSpammer::beaconSpamSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BeaconSpammer::beaconSpamLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Deauther::deautherSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                Deauther::deautherLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            DeauthDetect::deauthdetectSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                DeauthDetect::deauthdetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WifiScan::wifiscanSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                WifiScan::wifiscanLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }


        if (current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            CaptivePortal::cportalSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                CaptivePortal::cportalLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 6) {
            current_submenu_index = 6;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            HostScanner::hostScannerSetup();
            while (current_submenu_index == 6 && !feature_exit_requested) {
                current_submenu_index = 6;
                in_sub_menu = true;
                HostScanner::hostScannerLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 7) {
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WifiAssessment::setup();
            while (current_submenu_index == 7 && !feature_exit_requested) {
                in_sub_menu = true;
                WifiAssessment::loop();
                delay(1);
            }
            WifiAssessment::cleanup();
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 8) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    PacketMonitor::ptmSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        PacketMonitor::ptmLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BeaconSpammer::beaconSpamSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BeaconSpammer::beaconSpamLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Deauther::deautherSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        Deauther::deautherLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    DeauthDetect::deauthdetectSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        DeauthDetect::deauthdetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WifiScan::wifiscanSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        WifiScan::wifiscanLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    CaptivePortal::cportalSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        CaptivePortal::cportalLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 6) {
                    current_submenu_index = 6;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    HostScanner::hostScannerSetup();
                    while (current_submenu_index == 6 && !feature_exit_requested) {
                        current_submenu_index = 6;
                        in_sub_menu = true;
                        HostScanner::hostScannerLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 7) {
                    current_submenu_index = 7;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WifiAssessment::setup();
                    while (current_submenu_index == 7 && !feature_exit_requested) {
                        in_sub_menu = true;
                        WifiAssessment::loop();
                        delay(1);
                    }
                    WifiAssessment::cleanup();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
                break;
            }
        }
    }
}


void handleBluetoothSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 6) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleJammer::blejamSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                BleJammer::blejamLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSpoofer::spooferSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BleSpoofer::spooferLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SourApple::sourappleSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                SourApple::sourappleLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            GattEnum::gattEnumSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                GattEnum::gattEnumLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    GattEnum::gattEnumCleanup();  // Cleanup BT/BLE resources
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                GattEnum::gattEnumCleanup();  // Cleanup BT/BLE resources
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleScan::bleScanSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                BleScan::bleScanLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleHidInject::hidInjectSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                BleHidInject::hidInjectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    BleHidInject::hidInjectCleanup();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                BleHidInject::hidInjectCleanup();
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 6) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleJammer::blejamSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleJammer::blejamLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSpoofer::spooferSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BleSpoofer::spooferLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SourApple::sourappleSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        SourApple::sourappleLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    GattEnum::gattEnumSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        GattEnum::gattEnumLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            GattEnum::gattEnumCleanup();  // Cleanup BT/BLE resources
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        GattEnum::gattEnumCleanup();  // Cleanup BT/BLE resources
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleScan::bleScanSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        BleScan::bleScanLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleHidInject::hidInjectSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        BleHidInject::hidInjectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            BleHidInject::hidInjectCleanup();
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        BleHidInject::hidInjectCleanup();
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}


void handleNRFSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 6) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Scanner::scannerSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                Scanner::scannerLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        // Spectrum Analyzer (index 1)
        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Analyzer::analyzerSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                Analyzer::analyzerLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        // WLAN Jammer (index 2)
        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WLANJammer::wlanjammerSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                WLANJammer::wlanjammerLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProtoKill::prokillSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                ProtoKill::prokillLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ZigbeeScan::zigbeeScanSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                ZigbeeScan::zigbeeScanLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    ZigbeeScan::zigbeeScanCleanup();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                ZigbeeScan::zigbeeScanCleanup();
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ZigbeeSniffer::zigbeeSnifferSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                ZigbeeSniffer::zigbeeSnifferLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    ZigbeeSniffer::zigbeeSnifferCleanup();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                ZigbeeSniffer::zigbeeSnifferCleanup();
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 6) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Scanner::scannerSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        Scanner::scannerLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    // Touch: Spectrum Analyzer
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Analyzer::analyzerSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        Analyzer::analyzerLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    // Touch: WLAN Jammer
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WLANJammer::wlanjammerSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        WLANJammer::wlanjammerLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProtoKill::prokillSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        ProtoKill::prokillLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ZigbeeScan::zigbeeScanSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        ZigbeeScan::zigbeeScanLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            ZigbeeScan::zigbeeScanCleanup();
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        ZigbeeScan::zigbeeScanCleanup();
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ZigbeeSniffer::zigbeeSnifferSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        ZigbeeSniffer::zigbeeSnifferLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            ZigbeeSniffer::zigbeeSnifferCleanup();
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        ZigbeeSniffer::zigbeeSnifferCleanup();
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}


void handleSubGHzSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 4) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            pinMode(BOARD_CC1101_CSN, OUTPUT);
            pinMode(BOARD_NRF24_CSN_1, OUTPUT);
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            replayat::ReplayAttackSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                replayat::ReplayAttackLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            pinMode(BOARD_CC1101_CSN, OUTPUT);
            pinMode(BOARD_NRF24_CSN_1, OUTPUT);
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            subbrute::subBruteSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                subbrute::subBruteLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            pinMode(BOARD_CC1101_CSN, OUTPUT);
            pinMode(BOARD_NRF24_CSN_1, OUTPUT);
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            subjammer::subjammerSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                subjammer::subjammerLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }


        if (current_submenu_index == 3) {
            pinMode(BOARD_CC1101_CSN, OUTPUT);
            pinMode(BOARD_NRF24_CSN_1, OUTPUT);
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SavedProfile::saveSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                SavedProfile::saveLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 4) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;


                } else if (current_submenu_index == 0) {
                    pinMode(BOARD_CC1101_GDO2, INPUT);
                    pinMode(BOARD_CC1101_GDO0, INPUT);
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    replayat::ReplayAttackSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        replayat::ReplayAttackLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    pinMode(BOARD_CC1101_GDO2, INPUT);
                    pinMode(BOARD_CC1101_GDO0, INPUT);
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    subbrute::subBruteSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        subbrute::subBruteLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    pinMode(BOARD_CC1101_GDO2, INPUT);
                    pinMode(BOARD_CC1101_GDO0, INPUT);
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SavedProfile::saveSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        SavedProfile::saveLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    pinMode(BOARD_CC1101_GDO2, INPUT);
                    pinMode(BOARD_CC1101_GDO0, INPUT);
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    subjammer::subjammerSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        subjammer::subjammerLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}


// ==================== SETTINGS MENU ====================
int brightness_level = 255;  // Default full brightness
int screen_timeout_seconds = 60;  // Default 60 seconds

void displayBrightnessControl() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(ORANGE, TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setCursor(60, 20);
    tft.print("BRIGHTNESS");

    // Draw brightness bar
    tft.drawRect(30, 80, 180, 30, UI_CYAN);
    int bar_width = ::map(brightness_level, 0, 255, 0, 176);
    tft.fillRect(32, 82, bar_width, 26, ORANGE);

    // Show percentage
    tft.setCursor(90, 130);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    int percent = ::map(brightness_level, 0, 255, 0, 100);
    tft.printf("%d%%", percent);

    tft.setCursor(30, 200);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.print("LEFT/RIGHT: Adjust");
    tft.setCursor(30, 220);
    tft.print("SELECT: Save & Exit");

    drawStatusBar(currentBatteryVoltage, true);
}

void brightnessControlLoop() {
    displayBrightnessControl();

    while (!feature_exit_requested) {
        if (isButtonPressed(BTN_LEFT)) {
            brightness_level = max(10, brightness_level - 25);
            analogWrite(4, brightness_level);  // TFT_BL is pin 4
            displayBrightnessControl();
            delay(150);
        }
        if (isButtonPressed(BTN_RIGHT)) {
            brightness_level = min(255, brightness_level + 25);
            analogWrite(4, brightness_level);
            displayBrightnessControl();
            delay(150);
        }
        if (isButtonPressed(BTN_SELECT)) {
            feature_exit_requested = true;
            delay(200);
            break;
        }
        delay(50);
    }
}

void displayScreenTimeout() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(ORANGE, TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setCursor(40, 20);
    tft.print("SCREEN TIMEOUT");

    tft.setCursor(80, 100);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    if (screen_timeout_seconds == 0) {
        tft.print("OFF");
    } else {
        tft.printf("%ds", screen_timeout_seconds);
    }
    tft.setTextSize(1);

    tft.setCursor(30, 200);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.print("LEFT/RIGHT: Adjust");
    tft.setCursor(30, 220);
    tft.print("SELECT: Save & Exit");

    drawStatusBar(currentBatteryVoltage, true);
}

void screenTimeoutLoop() {
    displayScreenTimeout();
    int timeout_options[] = {0, 15, 30, 60, 120, 300};
    int num_options = 6;
    int current_option = 3;  // Default to 60s

    // Find current option index
    for (int i = 0; i < num_options; i++) {
        if (timeout_options[i] == screen_timeout_seconds) {
            current_option = i;
            break;
        }
    }

    while (!feature_exit_requested) {
        if (isButtonPressed(BTN_LEFT)) {
            current_option = max(0, current_option - 1);
            screen_timeout_seconds = timeout_options[current_option];
            displayScreenTimeout();
            delay(200);
        }
        if (isButtonPressed(BTN_RIGHT)) {
            current_option = min(num_options - 1, current_option + 1);
            screen_timeout_seconds = timeout_options[current_option];
            displayScreenTimeout();
            delay(200);
        }
        if (isButtonPressed(BTN_SELECT)) {
            feature_exit_requested = true;
            delay(200);
            break;
        }
        delay(50);
    }
}

void displayDeviceInfo() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(ORANGE, TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setCursor(60, 10);
    tft.print("DEVICE INFO");

    tft.setTextColor(UI_CYAN, TFT_BLACK);
    int y = 50;

    tft.setCursor(10, y); tft.print("Device: Quetzal");
    y += 25;
    tft.setCursor(10, y); tft.print(BOARD_NAME);
    y += 25;
    tft.setCursor(10, y); tft.printf("Free Heap: %d", ESP.getFreeHeap());
    y += 25;
    tft.setCursor(10, y); tft.printf("CPU Freq: %dMHz", ESP.getCpuFreqMHz());
    y += 25;
    tft.setCursor(10, y); tft.printf("Flash: %dMB", ESP.getFlashChipSize() / 1024 / 1024);
    y += 25;

    // Battery voltage
    float voltage = readBatteryVoltage();
    tft.setCursor(10, y); tft.printf("Battery: %.2fV", voltage);

    tft.setCursor(50, 280);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.print("SELECT: Back");

    drawStatusBar(currentBatteryVoltage, true);

    while (!feature_exit_requested) {
        if (isButtonPressed(BTN_SELECT)) {
            feature_exit_requested = true;
            delay(200);
            break;
        }
        delay(50);
    }
}

void handleSettingsSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = settings_NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= settings_NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        // Back to Main Menu
        if (current_submenu_index == 4) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        // Brightness
        if (current_submenu_index == 0) {
            feature_active = true;
            feature_exit_requested = false;
            brightnessControlLoop();
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
        }

        // Screen Timeout
        if (current_submenu_index == 1) {
            feature_active = true;
            feature_exit_requested = false;
            screenTimeoutLoop();
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
        }

        // Device Info
        if (current_submenu_index == 2) {
            feature_active = true;
            feature_exit_requested = false;
            displayDeviceInfo();
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
        }

        // Touch Calibration
        if (current_submenu_index == 3) {
            feature_active = true;
            feature_exit_requested = false;
            TouchCalibration::runCalibration();
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
        }
    }

    // Touch handler for Settings menu
    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                // Back to Main Menu
                if (current_submenu_index == 4) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 0) {
                    // Touch: Brightness
                    feature_active = true;
                    feature_exit_requested = false;
                    brightnessControlLoop();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                } else if (current_submenu_index == 1) {
                    // Touch: Screen Timeout
                    feature_active = true;
                    feature_exit_requested = false;
                    screenTimeoutLoop();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                } else if (current_submenu_index == 2) {
                    // Touch: Device Info
                    feature_active = true;
                    feature_exit_requested = false;
                    displayDeviceInfo();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                } else if (current_submenu_index == 3) {
                    // Touch: Touch Calibration
                    feature_active = true;
                    feature_exit_requested = false;
                    TouchCalibration::runCalibration();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
                break;
            }
        }
    }
}
// ==================== END SETTINGS ====================


void handleToolsSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 4) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Terminal::terminalSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                Terminal::terminalLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            FirmwareUpdate::updateSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                FirmwareUpdate::updateLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            FileManager::fileManagerSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                FileManager::fileManagerLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    FileManager::fileManagerCleanup();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                FileManager::fileManagerCleanup();
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            RgbLight::rgbLightSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                RgbLight::rgbLightLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    RgbLight::rgbLightCleanup();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                        delay(10); yield();
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                RgbLight::rgbLightCleanup();
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 4) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;

                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Terminal::terminalSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        Terminal::terminalLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    FirmwareUpdate::updateSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        FirmwareUpdate::updateLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                    }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    FileManager::fileManagerSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        FileManager::fileManagerLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            FileManager::fileManagerCleanup();
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        FileManager::fileManagerCleanup();
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    RgbLight::rgbLightSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        RgbLight::rgbLightLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            RgbLight::rgbLightCleanup();
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                                delay(10); yield();
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        RgbLight::rgbLightCleanup();
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}


void restoreEspNowSubmenu() {
    in_sub_menu = true;
    is_main_menu = false;
    submenu_initialized = false;
    feature_active = false;
    feature_exit_requested = false;
    displaySubmenu();
    delay(200);
}

void runEspNowTest(bool broadcastMode) {
    feature_active = true;
    feature_exit_requested = false;
    EspNowTest::espNowTestSetup(broadcastMode);
    while (!feature_exit_requested) {
        EspNowTest::espNowTestLoop();
        delay(5);
    }
    EspNowTest::espNowTestCleanup();
    restoreEspNowSubmenu();
}

void activateEspNowSubmenuItem(int index) {
    current_submenu_index = index;
    last_interaction_time = millis();
    if (index == espnow_NUM_SUBMENU_ITEMS - 1) {
        in_sub_menu = false;
        feature_active = false;
        feature_exit_requested = false;
        displayMenu();
        is_main_menu = false;
        return;
    }
    runEspNowTest(index == 0);
}

void handleEspNowSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        const int selected = current_submenu_index;
        delay(200);
        while (isButtonPressed(BTN_SELECT)) {
            delay(10);
            yield();
        }
        activateEspNowSubmenuItem(selected);
        return;
    }

    if (!ts.touched() || feature_active) return;
    TS_Point point = ts.getPoint();
    int x = ::map(point.x, TS_MINX, TS_MAXX, 0, 239);
    int y = ::map(point.y, TS_MAXY, TS_MINY, 0, 319);

    for (int i = 0; i < active_submenu_size; i++) {
        int yPos = 30 + i * 30;
        if (i == active_submenu_size - 1) yPos += 10;
        const int height = i == active_submenu_size - 1 ? 40 : 30;
        if (x < 10 || x > 220 || y < yPos || y > yPos + height) continue;

        current_submenu_index = i;
        displaySubmenu();
        unsigned long releaseStart = millis();
        while (ts.touched() && millis() - releaseStart < 1500) delay(10);
        activateEspNowSubmenuItem(i);
        return;
    }
}

void handleAboutPage() {

  tft.setTextColor(ORANGE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextFont(2);

  const char* title = "[About This Project]";
  tft.setCursor(10, 90);
  tft.println(title);

  int lineHeight = 30;
  int text_x = 10;
  int text_y = 130;
  tft.setCursor(text_x, text_y);
  tft.println("- Quetzal");
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.println(String("- ") + BOARD_NAME);
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.println("- Descended from ESP32-DIV/");
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.println("  HaleHound (see README)");
  text_y += lineHeight;


    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 0) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 0) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
            }
        }
    }
}


void handleButtons() {
    if (in_sub_menu) {
        switch (current_menu_index) {
            case 0: handleWiFiSubmenuButtons(); break;
            case 1: handleBluetoothSubmenuButtons(); break;
            case 2: handleNRFSubmenuButtons(); break;
            case 3: handleSubGHzSubmenuButtons(); break;
            case 4: handleEspNowSubmenuButtons(); break;
            case 5: handleToolsSubmenuButtons(); break;
            case 6: handleSettingsSubmenuButtons(); break;
            case 7: handleAboutPage(); break;
            default: break;
        }
    } else {

        if (isButtonPressed(BTN_UP) && !is_main_menu) {
            current_menu_index--;
            if (current_menu_index < 0) {
                current_menu_index = NUM_MENU_ITEMS - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN) && !is_main_menu) {
            current_menu_index++;
            if (current_menu_index >= NUM_MENU_ITEMS) {
                current_menu_index = 0;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_LEFT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index >= 4) {
                current_menu_index = row;
            } else if (current_menu_index == 0) {
                current_menu_index = 3;
            } else {
                current_menu_index = row - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_RIGHT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index < 4) {
                current_menu_index = row + 4;
            } else if (current_menu_index == 7) {
                current_menu_index = 0;
            } else {
                current_menu_index = row + 5;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_SELECT)) {
            last_interaction_time = millis();
            updateActiveSubmenu();
            delay(200);

            if (active_submenu_items && active_submenu_size > 0) {
                current_submenu_index = 0;
                in_sub_menu = true;
                submenu_initialized = false;
                displaySubmenu();
            }

            if (is_main_menu) {
                is_main_menu = false;
                displayMenu();
            } else {
                is_main_menu = true;
            }
        }

        static unsigned long lastTouchTime = 0;
        const unsigned long touchFeedbackDelay = 100;

        if (ts.touched() && !feature_active && (millis() - lastTouchTime >= touchFeedbackDelay)) {
            TS_Point p = ts.getPoint();
            delay(10);

            int x, y;
            x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
            y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

            for (int i = 0; i < NUM_MENU_ITEMS; i++) {
                int column = i / 4;
                int row = i % 4;
                int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
                int y_position = Y_START + row * Y_SPACING;

                int button_x1 = x_position;
                int button_y1 = y_position;
                int button_x2 = x_position + 100;
                int button_y2 = y_position + 60;

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    current_menu_index = i;
                    last_interaction_time = millis();
                    displayMenu();

                    unsigned long startTime = millis();
                    while (ts.touched() && (millis() - startTime < touchFeedbackDelay)) {
                        delay(10);
                    }

                    if (ts.touched()) {
                        updateActiveSubmenu();

                        if (active_submenu_items && active_submenu_size > 0) {
                            current_submenu_index = 0;
                            in_sub_menu = true;
                            submenu_initialized = false;
                            displaySubmenu();
                        } else {

                            if (is_main_menu) {
                                is_main_menu = false;
                                displayMenu();
                            } else {
                                is_main_menu = true;
                            }
                        }
                    }
                    delay(200);
                    break;
                }
            }
        }
    }
}


void setup() {
  Serial.begin(115200);

  tft.init();
  // The NM-CYD-C5 ST7789 init hardcodes INVON and the TFT_INVERSION_OFF build
  // flag does not reach TFT_eSPI here, so set inversion off explicitly (colours
  // otherwise render as a photo-negative).
  tft.invertDisplay(false);
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  setupTouchscreen();
  TouchCalibration::loadCalibration();  // applies a saved calibration, if any
  RgbLight::loadAndApplyPersisted();    // re-applies the last saved LED color/state

  loading(100, ORANGE, 0, 0, 2, true);

  tft.fillScreen(TFT_BLACK);

  displayLogo(UI_MAGENTA, 3500);  // 3.5 seconds

  //pinMode(36, INPUT);
  //pinMode(BACKLIGHT_PIN, OUTPUT);
  //digitalWrite(BACKLIGHT_PIN, HIGH);

  Serial.println("[Board] Using touch-backed button input (no physical buttons)");

  detectPeripherals();  // probes the RF-HAT slot; gates hardware-dependent menus below

  displayMenu();
  currentBatteryVoltage = readBatteryVoltage();  // Update now that ADC is initialized
  drawStatusBar(currentBatteryVoltage, false);
  last_interaction_time = millis();
}

void loop() {
  handleButtons();
  //manageBacklight();
  updateStatusBar();
}
