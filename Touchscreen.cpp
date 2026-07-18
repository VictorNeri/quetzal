#include "Touchscreen.h"

// Original ESP32-DIV: touch gets its own HSPI bus (NRF24/CC1101 use VSPI).
// NM-CYD-C5 (ESP32-C5): only one usable SPI peripheral (FSPI); the display,
// touch, SD and RF-HAT all share it. HSPI (bus 1) does not exist on the C5 and
// SPIClass(HSPI).begin() there fails with "SPI bus index 1 is out of range",
// which crashes the firmware when the touch controller is used.
#if BOARD_DISPLAY_DRIVER_ST7789
SPIClass touchscreenSPI = SPIClass(FSPI);
#else
SPIClass touchscreenSPI = SPIClass(HSPI);
#endif
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ); 
bool feature_active = false; 

void setupTouchscreen() {
    // End first to force GPIO matrix reconfiguration (releases any existing HSPI mapping)
    touchscreenSPI.end();
    delay(5);
    // Reinitialize HSPI with touch pins
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchscreenSPI);
    ts.setRotation(0);
}
