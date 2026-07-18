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

#define TS_MINX 300
#define TS_MAXX 3800
#define TS_MINY 300
#define TS_MAXY 3800
#define DISPLAY_WIDTH 240  
#define DISPLAY_HEIGHT 320 

extern bool feature_active;

void setupTouchscreen(); 

#endif
