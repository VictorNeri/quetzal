#include "rgb_light.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "utils.h"
#include "buttons_compat.h"
#include <Preferences.h>
#include <math.h>

// ═══════════════════════════════════════════════════════════════════════════
// RGB Light - decorative color/brightness control for the board's built-in
// single-wire addressable RGB LED (WS2812-style, GPIO27 - see
// BOARD_RGB_LED_PIN in board_config.h), doubling as a flashlight (white,
// full brightness). Driven via the Arduino core's rgbLedWriteOrdered(),
// which handles the RMT bit-banging itself - no PWM channel setup needed.
//
// The physical LED's actual wire order turned out to be G,B,R rather than
// the library's WS2812B-standard G,R,B default (rgbLedWrite() assumes GRB) -
// confirmed by observation: requesting pure red showed blue and vice versa,
// green was always correct. LED_COLOR_ORDER_GBR below swaps exactly the R/B
// bytes while leaving G alone, which matches that symptom.
//
// Color/brightness/mode are persisted to NVS (Preferences) so the last
// setting survives a reboot - loadAndApplyPersisted() re-applies it at boot
// without needing to re-open this menu. Exiting the menu no longer forces
// the LED off, since a decorative/flashlight setting is meant to stay lit.
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;
extern ButtonExpander pcf;

namespace RgbLight {

#define SCREEN_WIDTH  240
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 1

static bool uiDrawn = false;
static bool persistenceDirty = false;
static const int yshift = 30;

enum Mode : uint8_t { MODE_STATIC = 0, MODE_RAINBOW = 1, MODE_BREATHING = 2 };

static uint8_t baseR = 255, baseG = 255, baseB = 255;
static uint8_t brightnessPct = 100;
static bool ledOn = false;
static Mode mode = MODE_STATIC;
static float rainbowHue = 0.0f;

Preferences prefs;

static int iconX[ICON_NUM] = {210};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_go_back
};

struct Swatch { const char* name; uint8_t r, g, b; };
static const Swatch SWATCHES[] = {
  {"Red",     255, 0,   0},
  {"Green",   0,   255, 0},
  {"Blue",    0,   0,   255},
  {"Yellow",  255, 255, 0},
  {"Cyan",    0,   255, 255},
  {"Magenta", 255, 0,   255},
  {"Orange",  255, 120, 0},
  {"White",   255, 255, 255},
};
#define NUM_SWATCHES 8
#define SWATCH_COLS 4

#define SWATCH_W 50
#define SWATCH_H 38
#define SWATCH_GAP 5
#define SWATCH_LEFT 10
#define SWATCH_TOP (16 + yshift)

#define BAR_X 20
#define BAR_W 200
#define BAR_H 18
#define BAR_Y (SWATCH_TOP + 2 * (SWATCH_H + SWATCH_GAP) + 45)

#define OFF_BTN_X 20
#define OFF_BTN_W 90
#define OFF_BTN_H 32
#define OFF_BTN_Y (BAR_Y + 45)

#define FLASH_BTN_X 130
#define FLASH_BTN_W 90
#define FLASH_BTN_H 32
#define FLASH_BTN_Y OFF_BTN_Y

#define MODE_BTN_Y (OFF_BTN_Y + OFF_BTN_H + 12)
#define MODE_BTN_H 30
#define MODE_BTN_W 72
#define MODE_STATIC_X 4
#define MODE_RAINBOW_X 84
#define MODE_BREATHING_X 164

int swatchX(int i) { return SWATCH_LEFT + (i % SWATCH_COLS) * (SWATCH_W + SWATCH_GAP); }
int swatchY(int i) { return SWATCH_TOP + (i / SWATCH_COLS) * (SWATCH_H + SWATCH_GAP); }

uint16_t rgb888to565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void writeLed(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWriteOrdered(BOARD_RGB_LED_PIN, LED_COLOR_ORDER_GBR, r, g, b);
}

void applyLed() {
  uint8_t r = ledOn ? (uint8_t)((uint16_t)baseR * brightnessPct / 100) : 0;
  uint8_t g = ledOn ? (uint8_t)((uint16_t)baseG * brightnessPct / 100) : 0;
  uint8_t b = ledOn ? (uint8_t)((uint16_t)baseB * brightnessPct / 100) : 0;
  writeLed(r, g, b);
}

void savePersisted() {
  prefs.begin("rgblight", false);
  prefs.putUChar("r", baseR);
  prefs.putUChar("g", baseG);
  prefs.putUChar("b", baseB);
  prefs.putUChar("bright", brightnessPct);
  prefs.putBool("on", ledOn);
  prefs.putUChar("mode", (uint8_t)mode);
  prefs.end();
}

void loadAndApplyPersisted() {
  prefs.begin("rgblight", true);  // read-only
  if (prefs.isKey("on")) {
    baseR = prefs.getUChar("r", baseR);
    baseG = prefs.getUChar("g", baseG);
    baseB = prefs.getUChar("b", baseB);
    brightnessPct = prefs.getUChar("bright", brightnessPct);
    ledOn = prefs.getBool("on", false);
    mode = (Mode)prefs.getUChar("mode", (uint8_t)MODE_STATIC);
  }
  prefs.end();

  // Animated modes only tick while this screen's own loop is running (same
  // as every other animated feature in this firmware); at boot, show the
  // static base color so the LED still reflects the saved setting instead
  // of staying dark until the user reopens the menu.
  applyLed();
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  baseR = r; baseG = g; baseB = b;
  mode = MODE_STATIC;
  ledOn = true;
  applyLed();
  persistenceDirty = true;
}

void turnOff() {
  ledOn = false;
  applyLed();
  persistenceDirty = true;
}

void setMode(Mode m) {
  mode = m;
  ledOn = true;
  if (m == MODE_RAINBOW) rainbowHue = 0.0f;
  if (m == MODE_STATIC) applyLed();
  persistenceDirty = true;
}

void hsvToRgb(float h, uint8_t& r, uint8_t& g, uint8_t& b) {
  float x = 1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f);
  float rf, gf, bf;
  if (h < 60)       { rf = 1; gf = x; bf = 0; }
  else if (h < 120) { rf = x; gf = 1; bf = 0; }
  else if (h < 180) { rf = 0; gf = 1; bf = x; }
  else if (h < 240) { rf = 0; gf = x; bf = 1; }
  else if (h < 300) { rf = x; gf = 0; bf = 1; }
  else              { rf = 1; gf = 0; bf = x; }
  r = (uint8_t)(rf * 255);
  g = (uint8_t)(gf * 255);
  b = (uint8_t)(bf * 255);
}

void updateAnimation() {
  if (!ledOn || mode == MODE_STATIC) return;

  static unsigned long lastTick = 0;
  if (millis() - lastTick < 30) return;
  lastTick = millis();

  if (mode == MODE_RAINBOW) {
    rainbowHue += 2.0f;
    if (rainbowHue >= 360.0f) rainbowHue -= 360.0f;
    uint8_t r, g, b;
    hsvToRgb(rainbowHue, r, g, b);
    writeLed((uint8_t)((uint16_t)r * brightnessPct / 100),
             (uint8_t)((uint16_t)g * brightnessPct / 100),
             (uint8_t)((uint16_t)b * brightnessPct / 100));
  } else if (mode == MODE_BREATHING) {
    float phase = fmodf(millis() / 1500.0f, 2.0f);
    float t = phase < 1.0f ? phase : 2.0f - phase;  // triangle wave 0..1..0
    float level = 0.15f + 0.85f * t;                // floor at 15% so it never fully blacks out
    writeLed((uint8_t)(baseR * (brightnessPct / 100.0f) * level),
             (uint8_t)(baseG * (brightnessPct / 100.0f) * level),
             (uint8_t)(baseB * (brightnessPct / 100.0f) * level));
  }
}

void drawStatusLine() {
  tft.fillRect(0, SWATCH_TOP - 16, SCREEN_WIDTH, 14, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, SWATCH_TOP - 14);
  if (ledOn) {
    const char* modeName = mode == MODE_RAINBOW ? "Rainbow" : mode == MODE_BREATHING ? "Breathing" : "Static";
    tft.printf("LED: ON (%s)  Brightness: %d%%", modeName, brightnessPct);
  } else {
    tft.print("LED: OFF");
  }
}

void drawSwatches() {
  for (int i = 0; i < NUM_SWATCHES; i++) {
    int x = swatchX(i);
    int y = swatchY(i);
    uint16_t c565 = rgb888to565(SWATCHES[i].r, SWATCHES[i].g, SWATCHES[i].b);
    tft.fillRect(x, y, SWATCH_W, SWATCH_H, c565);
    tft.drawRect(x, y, SWATCH_W, SWATCH_H, UI_GUNMETAL);
  }
}

void drawBrightnessBar() {
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(BAR_X, BAR_Y - 12);
  tft.print("Brightness (tap to set):");

  tft.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, UI_CYAN);
  int fillW = (BAR_W - 2) * brightnessPct / 100;
  tft.fillRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, TFT_BLACK);
  if (fillW > 0) tft.fillRect(BAR_X + 1, BAR_Y + 1, fillW, BAR_H - 2, UI_AMBER);
}

void drawButtons() {
  tft.drawRoundRect(OFF_BTN_X, OFF_BTN_Y, OFF_BTN_W, OFF_BTN_H, 4, UI_CYAN);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(OFF_BTN_X + 25, OFF_BTN_Y + 11);
  tft.print("OFF");

  tft.drawRoundRect(FLASH_BTN_X, FLASH_BTN_Y, FLASH_BTN_W, FLASH_BTN_H, 4, UI_AMBER);
  tft.setTextColor(UI_AMBER, TFT_BLACK);
  tft.setCursor(FLASH_BTN_X + 10, FLASH_BTN_Y + 11);
  tft.print("FLASHLIGHT");
}

void drawModeButton(int x, const char* label, bool active) {
  uint16_t c = active ? UI_AMBER : UI_CYAN;
  tft.drawRoundRect(x, MODE_BTN_Y, MODE_BTN_W, MODE_BTN_H, 4, c);
  tft.setTextColor(c, TFT_BLACK);
  int textX = x + (MODE_BTN_W - (int)strlen(label) * 6) / 2;
  tft.setCursor(textX, MODE_BTN_Y + 11);
  tft.print(label);
}

void drawModeButtons() {
  drawModeButton(MODE_STATIC_X, "STATIC", mode == MODE_STATIC);
  drawModeButton(MODE_RAINBOW_X, "RAINBOW", mode == MODE_RAINBOW);
  drawModeButton(MODE_BREATHING_X, "BREATHE", mode == MODE_BREATHING);
}

void drawAll() {
  tft.fillRect(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, 320, TFT_BLACK);
  drawStatusLine();
  drawSwatches();
  drawBrightnessBar();
  drawButtons();
  drawModeButtons();
}

void runUI() {
  static int iconY = STATUS_BAR_Y_OFFSET;

  if (!uiDrawn) {
    tft.drawLine(0, 19, 240, 19, UI_CYAN);
    tft.fillRect(140, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 140, STATUS_BAR_HEIGHT, DARK_GRAY);
    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_CYAN);
    }
    tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, UI_AMBER);
    uiDrawn = true;
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck < 50) return;
  lastTouchCheck = millis();

  if (!ts.touched() || !feature_active) {
    // Coalesce repeated touch samples into one NVS write on release. This
    // keeps sliders responsive without needlessly wearing flash.
    if (persistenceDirty) {
      savePersisted();
      persistenceDirty = false;
    }
    return;
  }

  TS_Point p = ts.getPoint();
  int x = ::map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
  int y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

  // Back icon in the mini status row
  if (y >= STATUS_BAR_Y_OFFSET && y <= STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT + 5) {
    if (x >= iconX[0] - 5 && x <= iconX[0] + ICON_SIZE + 5) {
      feature_exit_requested = true;
      return;
    }
  }

  // Swatch grid
  for (int i = 0; i < NUM_SWATCHES; i++) {
    int sx = swatchX(i), sy = swatchY(i);
    if (x >= sx && x <= sx + SWATCH_W && y >= sy && y <= sy + SWATCH_H) {
      setColor(SWATCHES[i].r, SWATCHES[i].g, SWATCHES[i].b);
      drawStatusLine();
      drawModeButtons();
      return;
    }
  }

  // Brightness bar
  if (x >= BAR_X && x <= BAR_X + BAR_W && y >= BAR_Y && y <= BAR_Y + BAR_H) {
    int pct = (x - BAR_X) * 100 / BAR_W;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (brightnessPct == (uint8_t)pct) return;
    brightnessPct = (uint8_t)pct;
    if (!ledOn && brightnessPct > 0 && (baseR || baseG || baseB)) ledOn = true;
    if (mode == MODE_STATIC) applyLed();
    persistenceDirty = true;
    drawStatusLine();
    drawBrightnessBar();
    return;
  }

  // OFF button
  if (x >= OFF_BTN_X && x <= OFF_BTN_X + OFF_BTN_W && y >= OFF_BTN_Y && y <= OFF_BTN_Y + OFF_BTN_H) {
    turnOff();
    drawStatusLine();
    return;
  }

  // FLASHLIGHT button
  if (x >= FLASH_BTN_X && x <= FLASH_BTN_X + FLASH_BTN_W && y >= FLASH_BTN_Y && y <= FLASH_BTN_Y + FLASH_BTN_H) {
    brightnessPct = 100;
    setColor(255, 255, 255);
    drawStatusLine();
    drawBrightnessBar();
    drawModeButtons();
    return;
  }

  // Mode buttons
  if (y >= MODE_BTN_Y && y <= MODE_BTN_Y + MODE_BTN_H) {
    if (x >= MODE_STATIC_X && x <= MODE_STATIC_X + MODE_BTN_W) {
      setMode(MODE_STATIC);
    } else if (x >= MODE_RAINBOW_X && x <= MODE_RAINBOW_X + MODE_BTN_W) {
      setMode(MODE_RAINBOW);
    } else if (x >= MODE_BREATHING_X && x <= MODE_BREATHING_X + MODE_BTN_W) {
      setMode(MODE_BREATHING);
    } else {
      return;
    }
    drawStatusLine();
    drawModeButtons();
    return;
  }
}

void rgbLightSetup() {
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setTextSize(1);

  uiDrawn = false;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  runUI();
  drawAll();
}

void rgbLightLoop() {
  tft.drawLine(0, 19, 240, 19, UI_CYAN);
  updateStatusBar();
  updateAnimation();
  runUI();
}

void rgbLightCleanup() {
  // Intentionally does NOT turn the LED off - a decorative/flashlight
  // setting is meant to keep going after leaving this menu. The current
  // state is persisted when touch is released, so it also survives a reboot
  // via loadAndApplyPersisted(). Flush once if the screen exited while the
  // finger was still down.
  if (persistenceDirty) {
    savePersisted();
    persistenceDirty = false;
  }
}

}  // namespace RgbLight
