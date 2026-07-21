#ifndef RGB_LIGHT_H
#define RGB_LIGHT_H

#include "board_config.h"
#include <Arduino.h>

namespace RgbLight {
  void rgbLightSetup();
  void rgbLightLoop();
  void rgbLightCleanup();  // leaves the LED as the user set it - no longer forces it off

  // Re-applies the last saved color/brightness/on-off state at boot, so a
  // decorative or flashlight setting survives a power cycle without needing
  // to re-open the RGB Light menu. Call once from setup().
  void loadAndApplyPersisted();
}

#endif  // RGB_LIGHT_H
