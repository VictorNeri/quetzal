#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Board profile IDs. Select with -DESP32_DIV_BOARD_PROFILE=<id>.
#define ESP32_DIV_BOARD_ORIGINAL_V1 1
#define ESP32_DIV_BOARD_NM_CYD_C5   2

#ifndef ESP32_DIV_BOARD_PROFILE
#define ESP32_DIV_BOARD_PROFILE ESP32_DIV_BOARD_ORIGINAL_V1
#endif

#if ESP32_DIV_BOARD_PROFILE == ESP32_DIV_BOARD_ORIGINAL_V1

#define BOARD_NAME "ESP32-DIV V1 / ESP32-WROOM-32U"
#define BOARD_HAS_PCF8574 1
#define BOARD_HAS_SINGLE_RF_SLOT 0
#define BOARD_DISPLAY_DRIVER_ILI9341 1
#define BOARD_DISPLAY_DRIVER_ST7789 0

// Bluetooth capabilities. The original ESP32-WROOM-32U has a BR/EDR + BLE radio
// running the Bluedroid host stack.
#define BOARD_HAS_BT_CLASSIC 1
#define BOARD_BLE_STACK_NIMBLE 0

#define BOARD_PCF8574_ADDR    0x20
#define BOARD_BUTTON_UP       6
#define BOARD_BUTTON_DOWN     3
#define BOARD_BUTTON_LEFT     4
#define BOARD_BUTTON_RIGHT    5
#define BOARD_BUTTON_SELECT   7

#define BOARD_TOUCH_CLK       25
#define BOARD_TOUCH_MISO      35
#define BOARD_TOUCH_MOSI      32
#define BOARD_TOUCH_CS        33
#define BOARD_TOUCH_IRQ       34

#define BOARD_RADIO_SCK       18
#define BOARD_RADIO_MISO      19
#define BOARD_RADIO_MOSI      23

#define BOARD_NRF24_CE_1      16
#define BOARD_NRF24_CSN_1     17
#define BOARD_NRF24_CE_2      26
#define BOARD_NRF24_CSN_2     27
#define BOARD_NRF24_CE_3      4
#define BOARD_NRF24_CSN_3     5

#define BOARD_CC1101_GDO0     16
#define BOARD_CC1101_GDO2     26
#define BOARD_CC1101_CSN      27

#define BOARD_SD_CS           5

#define BOARD_TFT_MISO        12
#define BOARD_TFT_MOSI        13
#define BOARD_TFT_SCLK        14
#define BOARD_TFT_CS          15
#define BOARD_TFT_DC          2
#define BOARD_TFT_RST         0
#define BOARD_TFT_BL          4

#elif ESP32_DIV_BOARD_PROFILE == ESP32_DIV_BOARD_NM_CYD_C5

#define BOARD_NAME "NM-CYD-C5 / ESP32-C5-WROOM-1"
#define BOARD_HAS_PCF8574 0
#define BOARD_HAS_SINGLE_RF_SLOT 1
#define BOARD_DISPLAY_DRIVER_ILI9341 0
#define BOARD_DISPLAY_DRIVER_ST7789 1

// Bluetooth capabilities. The ESP32-C5 is BLE-only (no BR/EDR radio) and this
// Arduino core builds the NimBLE host (CONFIG_BT_NIMBLE_ENABLED, Bluedroid off).
// Classic-BT features must be compiled out; BLE features use NimBLE-Arduino.
#define BOARD_HAS_BT_CLASSIC 0
#define BOARD_BLE_STACK_NIMBLE 1

// NM-CYD-C5 has no PCF8574 button expander in the board docs. Keep the
// button aliases defined so legacy code compiles, but gate PCF access with
// BOARD_HAS_PCF8574.
#define BOARD_PCF8574_ADDR    0x20
#define BOARD_BUTTON_UP       6
#define BOARD_BUTTON_DOWN     3
#define BOARD_BUTTON_LEFT     4
#define BOARD_BUTTON_RIGHT    5
#define BOARD_BUTTON_SELECT   7

// NM-CYD-C5 shared SPI: display, XPT2046 touch, SD, and RF-HAT all share bus.
#define BOARD_TOUCH_CLK       6
#define BOARD_TOUCH_MISO      2
#define BOARD_TOUCH_MOSI      7
#define BOARD_TOUCH_CS        1
#define BOARD_TOUCH_IRQ       -1

#define BOARD_RADIO_SCK       6
#define BOARD_RADIO_MISO      2
#define BOARD_RADIO_MOSI      7

// Single RF module slot for NM-RF-HAT. CC1101 and NRF24 share the same CS and
// GDO0/CE pins on the documented hat; only one should be active/populated at a time.
#define BOARD_NRF24_CE_1      8
#define BOARD_NRF24_CSN_1     9
#define BOARD_NRF24_CE_2      8
#define BOARD_NRF24_CSN_2     9
#define BOARD_NRF24_CE_3      8
#define BOARD_NRF24_CSN_3     9

#define BOARD_CC1101_GDO0     8
#define BOARD_CC1101_GDO2     8
#define BOARD_CC1101_CSN      9

#define BOARD_SD_CS           10

#define BOARD_TFT_MISO        2
#define BOARD_TFT_MOSI        7
#define BOARD_TFT_SCLK        6
#define BOARD_TFT_CS          23
#define BOARD_TFT_DC          24
#define BOARD_TFT_RST         -1
#define BOARD_TFT_BL          25

#else
#error "Unknown ESP32_DIV_BOARD_PROFILE"
#endif

// Legacy aliases used throughout the original firmware. New code should use
// BOARD_* names directly, but these keep the first porting pass small.
#ifndef BTN_UP
#define BTN_UP BOARD_BUTTON_UP
#endif
#ifndef BTN_DOWN
#define BTN_DOWN BOARD_BUTTON_DOWN
#endif
#ifndef BTN_LEFT
#define BTN_LEFT BOARD_BUTTON_LEFT
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT BOARD_BUTTON_RIGHT
#endif
#ifndef BTN_SELECT
#define BTN_SELECT BOARD_BUTTON_SELECT
#endif

#endif // BOARD_CONFIG_H
