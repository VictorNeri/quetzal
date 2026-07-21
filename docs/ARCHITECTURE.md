# Quetzal architecture

## Overview

Quetzal is a single PlatformIO/Arduino firmware image for the NM-CYD-C5. The
main sketch owns startup, the top-level menu, status UI, and feature dispatch.
Feature modules expose setup/loop/cleanup entry points and share a small set of
global device objects inherited from the original firmware architecture.

## Startup sequence

`setup()` performs the following high-level work:

1. Initialize serial output and the board profile.
2. Initialize the ST7789 display and XPT2046 touchscreen.
3. Load persisted touch and RGB settings.
4. Probe the single RF-HAT slot for NRF24L01+ and CC1101.
5. Restore touchscreen ownership of the shared SPI peripheral.
6. Draw the Quetzal splash and main menu.

Hardware probes are timeout-bounded. In particular, the CC1101 probe avoids the
upstream driver reset loop, which can wait forever when no chip is present.

## Shared SPI ownership

The ESP32-C5 exposes one usable general-purpose SPI peripheral in this design.
The following devices share GPIO 6/2/7 for SCK/MISO/MOSI:

- ST7789 display
- XPT2046 touchscreen
- microSD card
- NRF24L01+ or CC1101 in the RF-HAT slot

Chip-select isolates devices electrically, but Arduino `SPIClass` instances
also maintain software ownership. A feature that switches from touch to SD or
an RF module must:

1. End the touchscreen SPI instance.
2. Initialize the global SPI instance with the board pin map.
3. Perform the operation.
4. End or release the global SPI instance where appropriate.
5. Call `setupTouchscreen()` before returning to interactive UI.

Skipping the final step leaves the board without input because it has no
physical navigation buttons.

## Input compatibility layer

`buttons_compat.h` presents the original PCF8574-style button interface while
backing it with touchscreen hit regions. This keeps inherited feature loops
usable without a physical button expander. Calibration values are mutable and
loaded from NVS by `touch_calibration.cpp`.

## External RF detection

`hw_detect.cpp` probes both supported RF chips at boot and records availability.
Menu entries declare an `HwReq` requirement. Entries whose device is absent are
dimmed and blocked before their setup function runs.

Because both modules share CS GPIO 9 and CE/GDO0 GPIO 8, detection indicates the
module fitted in the one physical slot; it does not enable concurrent operation.

## Radio stacks

- Wi-Fi uses Arduino-ESP32 networking plus selected low-level ESP-IDF Wi-Fi APIs.
- BLE uses NimBLE-Arduino. ESP32-C5 has no Bluetooth Classic controller.
- IEEE 802.15.4 uses the low-level `esp_ieee802154` promiscuous receive API.
- NRF24 tools use RF24 over the external shared SPI bus.
- CC1101 tools use SmartRC-CC1101 plus the C5 RMT compatibility path.

Features are responsible for disabling conflicting radio use and restoring UI
resources on exit. Long-running scans poll touchscreen state for cancellation
where supported.

## Persistence and storage

Preferences/NVS stores touchscreen calibration and RGB light settings.
LittleFS uses internal flash and does not consume the shared SPI bus. SD access
must follow the shared-SPI lifecycle above.

The 16 MB partition table provides a 4 MB application slot and the remaining
space for firmware data/storage. PSRAM is deliberately disabled because the
board profile used by the current toolchain does not safely initialize the
module's advertised PSRAM configuration.

## Build dependencies

`platformio.ini` pins pioarduino platform `55.03.36` and Arduino core 3.3.6.
TFT_eSPI 2.5.43 is vendored with an ESP32-C5 processor implementation. Other
libraries are resolved through PlatformIO.

The sole build environment is `nm-cyd-c5`.
