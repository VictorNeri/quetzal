#ifndef BUTTONS_COMPAT_H
#define BUTTONS_COMPAT_H

// ═══════════════════════════════════════════════════════════════════════════
// Button input compatibility layer
// ═══════════════════════════════════════════════════════════════════════════
// The NM-CYD-C5 has no PCF8574 I2C expander and no physical buttons, so `pcf`
// is a touch-backed stand-in: it maps on-screen touch zones to the same
// button pins the rest of the firmware already reads via
// `pcf.digitalRead(BTN_*)` / `pcf.pinMode()`.
//
// Zone layout (only while a feature is active, i.e. NOT on the main menu, which
// has its own touch navigation, and skipping the top status-bar/back-icon strip
// handled by runUI()):
//
//     y < 40 px            -> reserved (status bar / back icon)
//     y 40..129            -> UP
//     y 235..319           -> DOWN
//     y 130..234, x < 80   -> LEFT
//     y 130..234, x >=160  -> RIGHT
//     y 130..234, centre   -> SELECT
// ═══════════════════════════════════════════════════════════════════════════

#include "board_config.h"
#include <Arduino.h>
#include "Touchscreen.h"  // ts, feature_active, TS_MIN/MAX*, DISPLAY_WIDTH/HEIGHT

// Presents the PCF8574 subset the firmware uses (begin/pinMode/digitalRead) but
// derives button state from the touchscreen. digitalRead returns LOW (pressed,
// active-low like the pulled-up PCF8574 inputs) when the touch falls in that
// button's zone.
class TouchButtonExpander {
public:
  explicit TouchButtonExpander(uint8_t /*addr*/ = 0) {}

  bool begin() { return true; }
  void pinMode(uint8_t /*pin*/, uint8_t /*mode*/) {}

  uint8_t digitalRead(uint8_t pin) {
    // The main menu drives its own touch navigation while feature_active is
    // false; only synthesize buttons inside features.
    if (!feature_active) return HIGH;
    refresh();
    return (zoneButton() == (int)pin) ? LOW : HIGH;
  }

private:
  uint32_t _lastRead = 0;
  bool _touched = false;
  int _x = 0, _y = 0;

  void refresh() {
    uint32_t now = millis();
    if (now - _lastRead < 15) return;  // cache: touch SPI read at most every 15ms
    _lastRead = now;
    _touched = ts.touched();
    if (_touched) {
      TS_Point p = ts.getPoint();
      _x = ::map(p.x, TS_MINX, TS_MAXX, 0, DISPLAY_WIDTH - 1);
      _y = ::map(p.y, TS_MAXY, TS_MINY, 0, DISPLAY_HEIGHT - 1);
    }
  }

  int zoneButton() const {
    if (!_touched) return -1;
    if (_y < 40) return -1;                 // status bar / back-icon strip
    if (_y < 130) return BOARD_BUTTON_UP;
    if (_y >= 235) return BOARD_BUTTON_DOWN;
    if (_x < 80) return BOARD_BUTTON_LEFT;
    if (_x >= 160) return BOARD_BUTTON_RIGHT;
    return BOARD_BUTTON_SELECT;
  }
};

typedef TouchButtonExpander ButtonExpander;

#endif // BUTTONS_COMPAT_H
