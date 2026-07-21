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
// Runtime variables (not #define) so TouchCalibration can adjust and persist
// them - every existing call site keeps working unchanged either way, since
// they're only ever used as plain int arguments.
extern int16_t TS_MINX;
extern int16_t TS_MAXX;
extern int16_t TS_MINY;
extern int16_t TS_MAXY;
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

extern bool feature_active;

void setupTouchscreen();

#endif
