#include "hw_detect.h"
#include "shared.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include <RF24.h>
#include "Touchscreen.h"

extern TFT_eSPI tft;

bool hwNrf24Present = false;
bool hwCc1101Present = false;

// Minimal, timeout-bounded CC1101 presence check. Deliberately does NOT use
// the SmartRC library's Init()/Reset(): those spin on
// `while (digitalRead(MISO_PIN));` with no timeout, waiting for the chip to
// pull MISO low to signal "ready" - if no CC1101 is actually populated (e.g.
// an NRF24 is in the slot instead, or nothing is), MISO never goes low and
// that loop hangs forever, freezing boot before it ever reaches the menu.
// This reads the PARTNUM status register directly with a bounded wait
// instead, so a missing chip just reports "not found" and boot continues.
bool probeCc1101() {
  pinMode(BOARD_CC1101_CSN, OUTPUT);
  digitalWrite(BOARD_CC1101_CSN, HIGH);
  pinMode(BOARD_RADIO_MISO, INPUT);

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(BOARD_CC1101_CSN, LOW);

  unsigned long start = millis();
  bool ready = false;
  while (millis() - start < 50) {
    if (digitalRead(BOARD_RADIO_MISO) == LOW) {
      ready = true;
      break;
    }
  }

  bool present = false;
  if (ready) {
    SPI.transfer(0x30 | 0xC0);  // PARTNUM, burst-mode status read
    uint8_t partnum = SPI.transfer(0x00);
    present = (partnum == 0x00);  // CC1101 PARTNUM is always 0x00
  }

  digitalWrite(BOARD_CC1101_CSN, HIGH);
  SPI.endTransaction();
  return present;
}

void detectPeripherals() {
  // Both probes explicitly set the board's real shared-bus pins first - the
  // RF24 and CC1101 libraries each default to their own hardcoded pins
  // (matching the original ESP32-WROOM board) if not told otherwise, which
  // don't match this board's wiring (see BleJammer/ProtoKill/SubGHz fixes).
  SPI.begin(BOARD_RADIO_SCK, BOARD_RADIO_MISO, BOARD_RADIO_MOSI, BOARD_NRF24_CSN_1);

  RF24 probeRadio(BOARD_NRF24_CE_1, BOARD_NRF24_CSN_1, 1000000);
  hwNrf24Present = probeRadio.begin() && probeRadio.isChipConnected();
  probeRadio.powerDown();

  hwCc1101Present = probeCc1101();

  Serial.printf("[HW] NRF24L01+: %s\n", hwNrf24Present ? "detected" : "not found");
  Serial.printf("[HW] CC1101: %s\n", hwCc1101Present ? "detected" : "not found");

  // SPIClass::begin() is a no-op once its internal _spi handle is set - it
  // just returns true without re-attaching pins (see SPI.cpp). Since this is
  // the first-ever call to the global SPI object (happens once, unconditionally,
  // at every boot), leaving it "begun" would permanently poison every later
  // SPI.begin() call in the firmware (mountSD, BleJammer, SubGHz, ...) into a
  // silent no-op for the rest of the session - none of them would actually
  // reattach the shared bus's pins again. Release it here so the next real
  // caller gets a true first-time init.
  SPI.end();

  // The C5 has only one usable SPI peripheral - display, touch, SD and the
  // RF-HAT all share it. The probes above reconfigured that shared bus for
  // the global SPI object, which knocks out the touchscreen's own claim on
  // it; every other feature that touches this bus re-initializes touch
  // afterward for the same reason (see bluetooth.cpp), and this is the one
  // path in setup() that didn't - without it, touch (the only input this
  // board has) stops responding right after boot.
  setupTouchscreen();
}

void blockFeatureIfMissing(HwReq req) {
  if (hwReqSatisfied(req)) return;

  const char* chipName = (req == HW_NRF24) ? "NRF24L01+" : "CC1101";
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(10, 40);
  tft.print(String(chipName) + " not detected");
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 60);
  tft.print("Check the RF-HAT module.");
  delay(1200);
  feature_exit_requested = true;
}
