#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// NM-CYD-C5 (ESP32-C5-WROOM-1) is the only board this firmware targets.

#define BOARD_NAME "NM-CYD-C5 / ESP32-C5-WROOM-1"
#define BOARD_HAS_SINGLE_RF_SLOT 1

// The ESP32-C5 is BLE-only (no BR/EDR radio) and this Arduino core builds the
// NimBLE host (CONFIG_BT_NIMBLE_ENABLED, Bluedroid off).
#define BOARD_HAS_BT_CLASSIC 0

// GPIO36 does not exist on the ESP32-C5 and the NM-CYD-C5 has no documented
// battery divider, so battery sensing is disabled (reads a nominal voltage).
#define BOARD_BATTERY_ADC_PIN -1

#define BOARD_BUTTON_UP       6
#define BOARD_BUTTON_DOWN     3
#define BOARD_BUTTON_LEFT     4
#define BOARD_BUTTON_RIGHT    5
#define BOARD_BUTTON_SELECT   7

// Shared SPI: display, XPT2046 touch, SD, and RF-HAT all share this bus.
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

// Single-wire addressable RGB LED (WS2812-style, driven via the Arduino core's
// rgbLedWrite() over RMT - NOT 3 discrete PWM legs, that was an earlier wrong
// guess). GPIO27 is the board definition's own documented built-in RGB LED
// pin (PIN_RGB_LED/RGB_BUILTIN in the esp32c5 variant's pins_arduino.h) -
// confirmed unclaimed by any other BOARD_* pin above.
#define BOARD_RGB_LED_PIN     27

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
