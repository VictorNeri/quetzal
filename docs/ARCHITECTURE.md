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
- ESP-NOW tests use the ESP-IDF peer API in station mode and release the stack
  when their feature screen exits.
- BLE uses NimBLE-Arduino. ESP32-C5 has no Bluetooth Classic controller.
- IEEE 802.15.4 uses the low-level `esp_ieee802154` promiscuous receive API.
- NRF24 tools use RF24 over the external shared SPI bus.
- CC1101 tools use SmartRC-CC1101 plus the C5 RMT compatibility path.

### BLE assessment ownership

`ble_assessment.cpp` owns scanning, one central-role client, subscriptions, and
all active-test state while its nested suite is open. It stops advertising on
entry, configures passive NimBLE scanning for at most 24 results, retains at most 24
bounded advertisement records and 40 application-level characteristic records,
subscribes to at most eight notification sources, and deletes
its client on every exit path. It deliberately keeps NimBLE initialized so the
process-wide HID/server objects used by existing features remain valid.
Suite entry is refused while a BLE HID peer remains connected; pairing checks do
not alter NimBLE's process-wide security policy.

NimBLE's `discoverAttributes()` still builds its own transient remote-attribute
cache before the suite applies its 40-record retention cap. That cache is released
when the suite client is deleted. The application does not retain unbounded
advertisement, characteristic, notification, or value data, but hardware testing
must include a deliberately large remote GATT database to characterize the
third-party stack's peak heap use.

`ble_assessment_logic.cpp` provides UI-independent bounded advertisement
fingerprinting, AD-structure parsing, mesh exposure recognition, and payload
copy helpers. Notification callbacks retain only a bounded hash and length under
a critical section; they never touch the display or filesystem.

Pre-security permission checks and ATT probes use the low-level one-request
`ble_gattc_read` procedure. Replay capture uses low-level `ble_gattc_read_long`
to determine the complete value length and reject values over 64 bytes. Neither
path invokes `NimBLERemoteValueAttribute::readValue()`'s automatic security retry,
so read-only auditors cannot silently initiate pairing.

Every tool requires an explicit target tap. Inventory refreshes remap a selection
only when the same BLE address is rediscovered and otherwise invalidate it.
Active pairing, notification subscription, ATT-read, connection-resilience, and
replay workflows bind authorization to that address and require separate
authorization and confirmation taps. Pairing security is started asynchronously
and completed or cancelled by the foreground state machine under a fixed deadline.
Notification monitoring polls STOP and updates its security snapshot directly from
the NimBLE authentication callback. Each event is attributed to its source
characteristic and counted in the pre-secured or secured bucket at callback time;
the UI also records the transition count and latest characteristic whose CCCD
subscription caused an observed encryption transition.
Replay performs no
connection, discovery, or value capture until target-bound authorization is
recorded, then binds the captured value and
displayed characteristic UUID to the exact peer and rejects values over 64 bytes
instead of replaying a truncated prefix. Client disconnect completion is observed
through NimBLE callbacks and the post-callback invalid connection handle before reuse.
Request, duration, payload, and attempt caps are constants. STOP is checked
between synchronous NimBLE procedures; an in-flight procedure remains subject
to NimBLE's own timeout and cannot be preempted by the UI.

### Wi-Fi assessment ownership

The Assessment Suite is split into three focused modules:

- `wifi_80211.cpp` performs allocation-free, bounded 802.11, RSN, WPS, BSS
  Load, and EAPOL parsing.
- `wifi_capture.cpp` is the suite's sole promiscuous callback owner. The callback
  only validates and copies bounded snapshots into a fixed ring; parsing, TFT,
  filesystem access, and channel changes remain in the foreground loop.
- `wifi_assessment.cpp` owns the ordered tool menu, native AP inventory,
  radiotap PCAP writer, explicit rogue baseline, bounded maps and counters, and
  the two-stage authorization flow for the active resilience test.

Only one suite tool owns the Wi-Fi radio at a time. Every transition disables
promiscuous mode and clears its callback before scanning, changing channel, or
returning to the parent menu. EAPOL PCAP writes use LittleFS to avoid contending
with touch/display on the shared SD SPI host.

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
