#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H

#include "board_config.h"

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#define XPT2046_CS    BOARD_TOUCH_CS
#define XPT2046_IRQ   BOARD_TOUCH_IRQ
#define XPT2046_MOSI  BOARD_TOUCH_MOSI
#define XPT2046_MISO  BOARD_TOUCH_MISO
#define XPT2046_CLK   BOARD_TOUCH_CLK

extern SPIClass touchscreenSPI;
extern XPT2046_Touchscreen ts;

// XPT2046 raw-ADC calibration bounds. The mapping sites use X as
// map(raw, TS_MINX, TS_MAXX, ...) and Y as map(raw, TS_MAXY, TS_MINY, ...).
// The NM-CYD-C5 panel/digitizer is mounted 180 degrees from the original
// ESP32-DIV, so swap min<->max on both axes to flip touch the right way up.
#if BOARD_DISPLAY_DRIVER_ST7789
#define TS_MINX 3800
#define TS_MAXX 300
#define TS_MINY 3800
#define TS_MAXY 300
#else
#define TS_MINX 300
#define TS_MAXX 3800
#define TS_MINY 300
#define TS_MAXY 3800
#endif
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

extern bool feature_active;

void setupTouchscreen(); 

#endif
