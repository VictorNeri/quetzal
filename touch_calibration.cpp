#include "touch_calibration.h"
#include "shared.h"
#include "Touchscreen.h"
#include "utils.h"
#include <Preferences.h>

// ═══════════════════════════════════════════════════════════════════════════
// Touch Calibration - classic 2-point linear calibration for the resistive
// XPT2046 touch controller. Two crosshairs (inset from the edges, since
// resistive touch near the bezel is unreliable) are tapped in turn; from the
// two (raw ADC, known screen coordinate) samples, the raw ADC values that
// WOULD land exactly on screen x=0/239 and y=0/319 are solved for via linear
// extrapolation and written into TS_MINX/TS_MAXX/TS_MINY/TS_MAXY
// (Touchscreen.h) - every touch handler in the firmware already reads those
// as plain int arguments, so nothing else needs to change to pick this up.
// Persisted to NVS (Preferences) so it survives a reboot.
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

namespace TouchCalibration {

Preferences prefs;

void loadCalibration() {
  prefs.begin("touchcal", true);  // read-only
  if (prefs.isKey("minx")) {
    TS_MINX = prefs.getShort("minx", TS_MINX);
    TS_MAXX = prefs.getShort("maxx", TS_MAXX);
    TS_MINY = prefs.getShort("miny", TS_MINY);
    TS_MAXY = prefs.getShort("maxy", TS_MAXY);
  }
  prefs.end();
}

void saveCalibration() {
  prefs.begin("touchcal", false);
  prefs.putShort("minx", TS_MINX);
  prefs.putShort("maxx", TS_MAXX);
  prefs.putShort("miny", TS_MINY);
  prefs.putShort("maxy", TS_MAXY);
  prefs.end();
}

void drawCrosshair(int x, int y, uint16_t color) {
  tft.drawLine(x - 10, y, x + 10, y, color);
  tft.drawLine(x, y - 10, x, y + 10, color);
  tft.drawCircle(x, y, 6, color);
}

// Waits for a touch, averages the raw ADC reading over ~150ms for noise
// reduction, then waits for release so the same tap can't be read twice.
bool sampleRawTouch(int& rawX, int& rawY) {
  unsigned long waitStart = millis();
  while (!ts.touched()) {
    if (millis() - waitStart >= 30000) return false;
    delay(10);
  }

  long sumX = 0, sumY = 0;
  int samples = 0;
  unsigned long start = millis();
  while (millis() - start < 150) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      sumX += p.x;
      sumY += p.y;
      samples++;
    }
    delay(10);
  }
  rawX = samples ? (int)(sumX / samples) : 0;
  rawY = samples ? (int)(sumY / samples) : 0;

  waitStart = millis();
  while (ts.touched() && millis() - waitStart < 5000) delay(10);
  delay(150);
  return samples > 0;
}

void runCalibration() {
  const int P1X = 30, P1Y = 30;
  const int P2X = 210, P2Y = 290;

  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(15, 120);
  tft.print("Touch Calibration");
  tft.setTextFont(1);
  tft.setCursor(15, 150);
  tft.print("Tap each crosshair");
  tft.setCursor(15, 164);
  tft.print("precisely as it appears.");
  delay(1500);

  tft.fillScreen(TFT_BLACK);
  drawCrosshair(P1X, P1Y, UI_AMBER);
  int rawX1, rawY1;
  if (!sampleRawTouch(rawX1, rawY1)) {
    tft.setTextColor(RED, TFT_BLACK);
    tft.setCursor(15, 140);
    tft.print("Calibration timed out.");
    delay(1500);
    return;
  }

  tft.fillScreen(TFT_BLACK);
  drawCrosshair(P2X, P2Y, UI_AMBER);
  int rawX2, rawY2;
  if (!sampleRawTouch(rawX2, rawY2)) {
    tft.setTextColor(RED, TFT_BLACK);
    tft.setCursor(15, 140);
    tft.print("Calibration timed out.");
    delay(1500);
    return;
  }

  // A very small raw span amplifies noise into unusable screen bounds even
  // when the two samples are not exactly equal.
  if (abs(rawX2 - rawX1) < 500 || abs(rawY2 - rawY1) < 500) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(1);
    tft.setTextColor(RED, TFT_BLACK);
    tft.setCursor(15, 140);
    tft.print("Calibration failed -");
    tft.setCursor(15, 154);
    tft.print("taps were too close together.");
    delay(2000);
    return;
  }

  // screenX = kx * (rawX - TS_MINX)  =>  TS_MINX = rawX1 - P1X/kx,
  // TS_MAXX = TS_MINX + 239/kx (the raw values that would land on x=0/239).
  float kx = (float)(P2X - P1X) / (float)(rawX2 - rawX1);
  int newMinX = rawX1 - (int)(P1X / kx);
  int newMaxX = newMinX + (int)(239 / kx);

  // Y is mapped inverted (map(rawY, TS_MAXY, TS_MINY, 0, 319)): TS_MAXY is
  // "raw value at screen y=0", TS_MINY is "raw value at screen y=319".
  float ky = (float)(P2Y - P1Y) / (float)(rawY2 - rawY1);
  int newMaxY = rawY1 - (int)(P1Y / ky);
  int newMinY = newMaxY + (int)(319 / ky);

  TS_MINX = (int16_t)newMinX;
  TS_MAXX = (int16_t)newMaxX;
  TS_MINY = (int16_t)newMinY;
  TS_MAXY = (int16_t)newMaxY;
  saveCalibration();

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(UI_GREEN, TFT_BLACK);
  tft.setCursor(15, 140);
  tft.print("Calibration saved!");
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(15, 160);
  tft.print("Tap anywhere to continue.");
  unsigned long waitStart = millis();
  while (!ts.touched() && millis() - waitStart < 10000) delay(10);
  waitStart = millis();
  while (ts.touched() && millis() - waitStart < 5000) delay(10);
  delay(150);
}

}  // namespace TouchCalibration
