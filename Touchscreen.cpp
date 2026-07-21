#include "Touchscreen.h"

// NM-CYD-C5 (ESP32-C5): only one usable SPI peripheral (FSPI); the display,
// touch, SD and RF-HAT all share it. HSPI (bus 1) does not exist on the C5 and
// SPIClass(HSPI).begin() there fails with "SPI bus index 1 is out of range",
// which crashes the firmware when the touch controller is used.
SPIClass touchscreenSPI = SPIClass(FSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
bool feature_active = false;

// Factory-default raw-ADC calibration bounds - overwritten at boot by
// TouchCalibration::loadCalibration() if a saved calibration exists in NVS.
int16_t TS_MINX = 300;
int16_t TS_MAXX = 3800;
int16_t TS_MINY = 3800;
int16_t TS_MAXY = 300;

void setupTouchscreen() {
    // End first to force GPIO matrix reconfiguration (releases any existing HSPI mapping)
    touchscreenSPI.end();
    delay(5);
    // Reinitialize HSPI with touch pins
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchscreenSPI);
    ts.setRotation(0);
}
