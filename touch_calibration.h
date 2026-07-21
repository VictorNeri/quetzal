#ifndef TOUCH_CALIBRATION_H
#define TOUCH_CALIBRATION_H

#include "board_config.h"
#include <Arduino.h>

namespace TouchCalibration {
  // Blocking 2-point calibration wizard: tap two on-screen crosshairs, then
  // saves the result to NVS. Call from the Settings menu.
  void runCalibration();

  // Loads a previously-saved calibration into TS_MINX/TS_MAXX/TS_MINY/TS_MAXY
  // (Touchscreen.h). Leaves the factory defaults in place if none was ever
  // saved. Call once at boot, after setupTouchscreen().
  void loadCalibration();
}

#endif  // TOUCH_CALIBRATION_H
