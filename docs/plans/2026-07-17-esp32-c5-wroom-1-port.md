# ESP32-C5-WROOM-1 Port Implementation Plan

> **Archived planning document.** The NM-CYD-C5 port is implemented and the
> current firmware is documented in the repository README and `docs/` guides.
> Paths and intermediate assumptions below are retained as implementation
> history and should not be treated as current build instructions.

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task after Victor confirms the target carrier board/wiring.

**Goal:** Port ESP32-DIV HaleHound firmware from ESP32-WROOM-32U to the NM-CYD-C5 / ESP32-C5-WROOM-1 board while preserving display/touch, WiFi, BLE, SD, firmware-update functionality, and one active RF module at a time where the ESP32-C5 Arduino core supports it. Multi-NRF24 plus simultaneous CC1101 support is explicitly deferred until external RF switching/mux hardware exists.

**Architecture:** First isolate all board-specific pins and ESP32-family APIs behind a small board profile layer. Then add an NM-CYD-C5 profile using the board's documented shared SPI wiring and single RF-HAT slot. Finally migrate incompatible ESP-IDF/Arduino APIs, especially RMT and BLE/WiFi low-level calls, behind compatibility wrappers.

**Tech Stack:** Arduino ESP32 core with ESP32-C5 support, TFT_eSPI, XPT2046_Touchscreen, PCF8574, RF24, ELECHOUSE_CC1101_SRC_DRV or fork, RCSwitch, arduinoFFT, ESP32 BLE Arduino/NimBLE-compatible APIs, esptool.

---

## Known constraints from audit

### Existing firmware assumptions

- Current target is ESP32-WROOM-32U / original ESP32-DIV V1 board.
- Current build target in README is Arduino IDE Board: `ESP32 Dev Module` with Huge APP partition.
- Current pin map is hardcoded repeatedly across `.ino`, `.cpp`, `.h`, and `User_Setup.h`.
- Current SPI design uses:
  - VSPI radios/storage: SCK 18, MISO 19, MOSI 23, shared by NRF24, CC1101, SD
  - HSPI touch: CLK 25, MOSI 32, MISO 35, CS 33
  - TFT pins in `User_Setup.h`: MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST 0, BL 4
- Current NRF24 pins:
  - CE1 16, CSN1 17
  - CE2 26, CSN2 27
  - CE3 4, CSN3 5
- Current CC1101 pins:
  - GDO0/RX symbolic pin 16
  - GDO2/TX symbolic pin 26
  - CSN 27 in cleanup paths
- Current PCF8574 button address: 0x20; button bits: UP 6, DOWN 3, LEFT 4, RIGHT 5, SELECT 7.
- Current RMT code includes legacy `driver/rmt.h` and uses `rmt_item32_t`, `rmt_config_t`, `rmt_driver_install`, `rmt_write_items`, `RMT_CHANNEL_0`.

### ESP32-C5 constraints checked from Espressif docs/datasheet

- ESP32-C5 has 29 GPIOs: GPIO0 through GPIO28.
- GPIO16 through GPIO22 are normally used for SPI flash/PSRAM and are not recommended for general use.
- GPIO13 and GPIO14 are used by USB-JTAG by default; using them as GPIO disables USB-JTAG.
- Strapping pins include GPIO2, GPIO7, GPIO25, GPIO26, GPIO27, GPIO28, plus MTMS/MTDI functions. Avoid driving these externally at reset unless the carrier board is designed for it.
- ESP32-C5 supports 2.4/5 GHz WiFi 6, Bluetooth LE, 802.15.4, general-purpose SPI, and RMT, but the ESP-IDF/Arduino APIs differ from classic ESP32 in several areas.
- ESP32-C5 does not expose GPIO32-GPIO35, so the current XPT2046 touch pins cannot work unchanged.

### High-risk compatibility areas

1. Pins: current firmware uses many GPIOs that do not exist or are reserved/problematic on ESP32-C5.
2. RMT: current legacy RMT API is likely incompatible with ESP32-C5 Arduino core based on ESP-IDF 5.x.
3. BLE: current code uses classic ESP32 BLE Arduino-style APIs; verify ESP32-C5 Arduino support and switch to NimBLE APIs if needed.
4. WiFi promiscuous/deauth injection: low-level `esp_wifi_80211_tx`, promiscuous structs, and channel APIs may compile differently or behave differently on C5.
5. TFT_eSPI: `User_Setup.h` must become C5-specific; current pins include unavailable/problematic pins.
6. SPI bus names: VSPI/HSPI assumptions should not be used directly on C5. Use explicit `SPIClass`/pin arguments or the Arduino C5 default SPI host.

---

## Concrete NM-CYD-C5 board information found

Board repo checked: `/home/v1ct0r/Workspace/NM-CYD-C5`.

Core board facts from README:
- Board: NM-CYD-C5
- Module: ESP32-C5-WROOM-1
- Memory: 16 MB flash + 8 MB PSRAM
- Display: 2.8 inch 240x320 TFT, default ST7789; README says ILI9341 can be swapped/configured
- Touch: XPT2046
- Storage: SD card slot
- USB: two USB-C paths: ESP32-C5 native USB and CH340 USB-UART
- Extra: RGB LED, FPC connector, Grove/extend IO

Known onboard/shared SPI wiring:

| Device  | SCK | MISO | MOSI | CS | IRQ / other |
| --- | ---: | ---: | ---: | ---: | --- |
| Display ST7789 | 6 | 2 | 7 | 23 | DC 24, RST -1/C5 RST, BL 25 |
| Touch XPT2046 | 6 | 2 | 7 | 1 | IRQ -1 / not wired |
| SD Card | 6 | 2 | 7 | 10 | - |
| CC1101 via NM-RF-HAT | 6 | 2 | 7 | 9 | GDO0 8 |
| NRF24 via NM-RF-HAT | 6 | 2 | 7 | 9 | CE 8 |

Important RF-HAT limitation from `connections.md`:
- CC1101, NRF24, and W5500 use the same CS and CE/GDO0 pins on the NM-RF-HAT style mapping.
- They require a hardware switch/mux or only one module populated/active at a time.
- Victor chose the initial port target: **single RF module for now**.
- Initial firmware profile maps all NRF24 chip-select/CE aliases to the single NM-RF-HAT RF slot and maps CC1101 to the same RF slot. This is intentional for compile/bring-up, not simultaneous multi-radio support.
- Original ESP32-DIV behavior with three NRF24 modules plus CC1101 simultaneously is out of scope until external switching/mux hardware is designed.

Other useful pins from board docs:
- RGB LED / WS2812: GPIO27
- Backlight: GPIO25
- GPS / CH9329 style serial: RX 4, TX 5 in README; some PlatformIO variant maps Serial TX/RX as 11/12 depending use case
- I2C/Grove/extend IO appears in two conventions:
  - README CN1: IO9, IO8
  - `pins_arduino.h`: SDA 4, SCL 5; GROVE_SDA 8, GROVE_SCL 9
- FPC2 exposes: IO2, IO6, IO7, IO10, GND, IO4, IO8, IO5, IO9, USB D-, USB D+, GND

Platform/build hints found:
- Recommended PlatformIO platform: `pioarduino/platform-espressif32` Arduino 3.3.6 or newer.
- Example board target: `esp32-c5-devkitc-1` or custom `nm-cyd-c5` board definition.
- Required USB flags:
  - `ARDUINO_USB_CDC_ON_BOOT=1`
  - `ARDUINO_USB_MODE=1`
- TFT flags from board examples:
  - `ST7789_DRIVER=1`
  - `TFT_WIDTH=240`
  - `TFT_HEIGHT=320`
  - `TFT_BL=25`
  - `TFT_RST=-1`
  - `TFT_DC=24`
  - `TFT_MISO=2`
  - `TFT_MOSI=7`
  - `TFT_SCLK=6`
  - `TFT_CS=23`
  - `TOUCH_CS=1`
  - `SPI_FREQUENCY=20000000`
  - `SPI_TOUCH_FREQUENCY=2500000`

Proposed C5 board profile now targets NM-CYD-C5 specifically, not generic ESP32-C5-WROOM-1.

Implementation status as of 2026-07-17:
- Added `board_config.h` with `ESP32_DIV_BOARD_ORIGINAL_V1` and `ESP32_DIV_BOARD_NM_CYD_C5` profiles.
- NM-CYD-C5 profile sets shared SPI pins to SCK 6, MISO 2, MOSI 7; TFT CS/DC/BL to 23/24/25; touch CS to 1; SD CS to 10; RF-HAT CS/CE-GDO0 to 9/8.
- `BOARD_HAS_SINGLE_RF_SLOT=1` documents the first bring-up target: one populated/active RF module. The duplicated NRF24 aliases intentionally collapse onto the single RF-HAT slot.
- PCF8574 access is gated in top-level setup/button-read paths with `BOARD_HAS_PCF8574`; NM-CYD-C5 currently has no documented PCF8574 expander.
- `User_Setup.h`, touch headers, RF pin aliases, SD CS, and button macros now derive from board config instead of raw original-board numbers.

Implementation status as of 2026-07-18 — **both environments now compile and link**
(`pio run -e esp32div-original` and `pio run -e nm-cyd-c5` both SUCCEED):
- **Toolchain:** pinned the C5 platform to pioarduino `55.03.36` (Arduino 3.3.6);
  the floating `stable` tag left the Arduino core package unresolved
  (`FRAMEWORK_DIR=None`). Note: any `platformio.ini` edit intermittently
  re-triggers that resolution glitch — just re-run the build once.
- **Display:** vendored TFT_eSPI 2.5.43 into `lib/TFT_eSPI` and overlaid the
  NM-CYD-C5 `TFT_eSPI_ESP32_C5.c/.h` processor patch (stock TFT_eSPI has no C5
  support). Dropped the registry TFT_eSPI dep; the C5 branch is additive so the
  original build is unaffected.
- **Network:** added the core-3.x `Network` library src to the C5 include path
  (LDF does not chain bundled core libs, matching the existing FS/SPIFFS hack).
- **BLE:** full Bluedroid→NimBLE migration for C5 via `ble_compat.h` (type
  aliases + helper wrappers). Added `BOARD_HAS_BT_CLASSIC` / `BOARD_BLE_STACK_NIMBLE`
  flags. Bluetooth Classic (BR/EDR) is impossible on C5, so `BleSniffer` is
  stubbed there with a UI message; BLE scan/spoof/spam use NimBLE-Arduino 2.5.
- **WiFi / IDF-5:** guarded `tcpip_adapter_init`→`esp_netif_init`,
  `system_event_t`, `esp_event_loop.h`, and `temprature_sens_read`→`temperatureRead`
  by `ESP_IDF_VERSION_MAJOR`. Fixed `byte`/`std::byte` C++17 ambiguity in the
  CC1101 shim and wifi.cpp.
- **RMT (Task 7):** no rewrite needed to build — IDF 5 still ships the legacy
  `driver/rmt.h` (deprecated but functional). subghz.cpp compiles/links unchanged
  on C5. A `driver/rmt_tx.h` port remains future hardening (legacy may drop in IDF 6).
- **Partitions:** added `partitions_c5_16mb.csv` (4 MB app + ~12 MB SPIFFS) for the
  16 MB C5 flash; firmware is ~1.8 MB.

Remaining (hardware bring-up, Task 11 — needs the physical board):
- No PCF8574 on C5, so physical-button input is non-functional; navigation will
  need to be touch-only (compiles, but `pcf.digitalRead` calls do nothing).
- Runtime-verify display init, touch calibration, WiFi scan/monitor/deauth,
  BLE scan/advertise (NimBLE random-address spoofing), CC1101/NRF24, and SubGHz
  RMT pulse timing on real hardware.

---

## Task 1: Add board profile header

**Objective:** Create one source of truth for board-dependent pins and feature flags.

**Files:**
- Create: `board_config.h`
- Modify later: all files with duplicated `#define XPT2046_*`, `BTN_*`, `CE_PIN_*`, `CSN_PIN_*`, `RX_PIN`, `TX_PIN`, `SD_CS_PIN`, TFT pins.

**Step 1: Create `board_config.h`**

```cpp
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Select exactly one board profile.
// Default preserves current ESP32-DIV V1 behavior.
#ifndef ESP32_DIV_BOARD_PROFILE
#define ESP32_DIV_BOARD_PROFILE ESP32_DIV_BOARD_ORIGINAL_V1
#endif

#define ESP32_DIV_BOARD_ORIGINAL_V1 1
#define ESP32_DIV_BOARD_C5_WROOM_1  2

#if ESP32_DIV_BOARD_PROFILE == ESP32_DIV_BOARD_ORIGINAL_V1

#define BOARD_NAME "ESP32-DIV V1 / ESP32-WROOM-32U"

#define BOARD_BUTTON_UP       6
#define BOARD_BUTTON_DOWN     3
#define BOARD_BUTTON_LEFT     4
#define BOARD_BUTTON_RIGHT    5
#define BOARD_BUTTON_SELECT   7
#define BOARD_PCF8574_ADDR    0x20

#define BOARD_TOUCH_CLK       25
#define BOARD_TOUCH_MISO      35
#define BOARD_TOUCH_MOSI      32
#define BOARD_TOUCH_CS        33
#define BOARD_TOUCH_IRQ       34

#define BOARD_RADIO_SCK       18
#define BOARD_RADIO_MISO      19
#define BOARD_RADIO_MOSI      23

#define BOARD_NRF24_CE_1      16
#define BOARD_NRF24_CSN_1     17
#define BOARD_NRF24_CE_2      26
#define BOARD_NRF24_CSN_2     27
#define BOARD_NRF24_CE_3      4
#define BOARD_NRF24_CSN_3     5

#define BOARD_CC1101_GDO0     16
#define BOARD_CC1101_GDO2     26
#define BOARD_CC1101_CSN      27

#define BOARD_SD_CS           5

#elif ESP32_DIV_BOARD_PROFILE == ESP32_DIV_BOARD_C5_WROOM_1

#define BOARD_NAME "ESP32-C5-WROOM-1"
#error "ESP32-C5-WROOM-1 pin map must be filled after carrier board wiring is confirmed."

#else
#error "Unknown ESP32_DIV_BOARD_PROFILE"
#endif

#endif // BOARD_CONFIG_H
```

**Step 2: Verify no compile behavior changes yet**

Expected: no source includes it yet; original firmware behavior unchanged.

**Step 3: Commit**

```bash
git add board_config.h
git commit -m "refactor: add board profile configuration header"
```

---

## Task 2: Replace duplicated PCF8574 button defines

**Objective:** Use board config for PCF8574 address and button bits.

**Files:**
- Modify: `ESP32-DIV.ino`
- Modify: `wifi.cpp`
- Modify: `bluetooth.cpp`
- Modify: `subghz.cpp`

**Steps:**
1. Include `board_config.h` near existing local includes.
2. Replace duplicated button defines:
   - `BTN_UP` -> `BOARD_BUTTON_UP`
   - `BTN_DOWN` -> `BOARD_BUTTON_DOWN`
   - `BTN_LEFT` -> `BOARD_BUTTON_LEFT`
   - `BTN_RIGHT` -> `BOARD_BUTTON_RIGHT`
   - `BTN_SELECT` -> `BOARD_BUTTON_SELECT`
3. Prefer compatibility aliases only temporarily:

```cpp
#include "board_config.h"
#define BTN_UP BOARD_BUTTON_UP
#define BTN_DOWN BOARD_BUTTON_DOWN
#define BTN_LEFT BOARD_BUTTON_LEFT
#define BTN_RIGHT BOARD_BUTTON_RIGHT
#define BTN_SELECT BOARD_BUTTON_SELECT
```

4. Replace `#define pcf_ADDR 0x20` with:

```cpp
#define pcf_ADDR BOARD_PCF8574_ADDR
```

**Verification:**
- Search returns no hardcoded duplicated `#define BTN_UP 6` except in compatibility blocks.
- Original board compiles before C5 profile work continues.

---

## Task 3: Replace touch pin defines

**Objective:** Make XPT2046 pins board-profile-driven and remove impossible GPIO32-GPIO35 assumptions for C5.

**Files:**
- Modify: `Touchscreen.h`
- Modify: `Touchscreen.cpp`
- Modify: `utils.h`
- Modify: `wificonfig.h`
- Modify: `subconfig.h`
- Modify: `bleconfig.h`

**Implementation pattern:**

```cpp
#include "board_config.h"

#define XPT2046_CS    BOARD_TOUCH_CS
#define XPT2046_IRQ   BOARD_TOUCH_IRQ
#define XPT2046_MOSI  BOARD_TOUCH_MOSI
#define XPT2046_MISO  BOARD_TOUCH_MISO
#define XPT2046_CLK   BOARD_TOUCH_CLK
```

**Verification:**
- `grep -R "#define XPT2046_" .` only shows board-config-driven aliases.
- Original board behavior unchanged.

---

## Task 4: Replace radio/SD pin constants

**Objective:** Centralize NRF24, CC1101, and SD pin use before C5 remapping.

**Files:**
- Modify: `bluetooth.cpp`
- Modify: `subghz.cpp`
- Modify: `wifi.cpp`

**Mapping:**
- `CE_PIN_1`, `CE`, `ANA_CE` -> `BOARD_NRF24_CE_1`
- `CSN_PIN_1`, `CSN`, `ANA_CSN` -> `BOARD_NRF24_CSN_1`
- `CE_PIN_2` -> `BOARD_NRF24_CE_2`
- `CSN_PIN_2` -> `BOARD_NRF24_CSN_2`
- `CE_PIN_3` -> `BOARD_NRF24_CE_3`
- `CSN_PIN_3` -> `BOARD_NRF24_CSN_3`
- CC1101 `RX_PIN` / GDO0 -> `BOARD_CC1101_GDO0`
- CC1101 `TX_PIN` / GDO2 -> `BOARD_CC1101_GDO2`
- CC1101 CSN hardcoded `27` -> `BOARD_CC1101_CSN`
- `SD_CS_PIN` -> `BOARD_SD_CS`
- SPI bus begin `SPI.begin(18, 19, 23, 17)` -> `SPI.begin(BOARD_RADIO_SCK, BOARD_RADIO_MISO, BOARD_RADIO_MOSI, BOARD_NRF24_CSN_1)`

**Verification:**
- Search for `pinMode(16`, `pinMode(26`, `pinMode(27`, `SPI.begin(18` returns no direct board-specific use outside board profile or comments.
- Original board build still targets the same physical pins.

---

## Task 5: Add reproducible build setup

**Objective:** Make the project buildable without manual Arduino IDE mystery settings.

**Files:**
- Create: `platformio.ini` or `arduino-cli.yaml` + build script.
- Modify: `README.md` build section.

**Preferred PlatformIO starting point:**

```ini
[platformio]
default_envs = esp32div-v1

[env]
framework = arduino
monitor_speed = 115200
lib_deps =
  bodmer/TFT_eSPI
  paulstoffregen/XPT2046_Touchscreen
  xreef/PCF8574 library
  nrf24/RF24
  sui77/rc-switch
  kosme/arduinoFFT
build_flags =
  -DUSER_SETUP_LOADED=1

[env:esp32div-v1]
platform = espressif32
board = esp32dev
board_build.partitions = huge_app.csv
build_flags =
  ${env.build_flags}
  -DESP32_DIV_BOARD_PROFILE=ESP32_DIV_BOARD_ORIGINAL_V1

[env:esp32-c5-wroom-1]
platform = espressif32
board = esp32-c5-devkitc-1
build_flags =
  ${env.build_flags}
  -DESP32_DIV_BOARD_PROFILE=ESP32_DIV_BOARD_C5_WROOM_1
```

**Note:** exact ESP32-C5 board ID depends on installed PlatformIO/Espressif platform support. If not available, use Arduino CLI with the current Arduino ESP32 core that supports C5.

**Verification:**
- `pio run -e esp32div-v1` builds original target.
- `pio run -e esp32-c5-wroom-1` reaches meaningful C5 compatibility errors instead of missing board setup.

---

## Task 6: Convert TFT_eSPI setup to board profiles

**Objective:** Make TFT pins C5-specific without editing installed library files.

**Files:**
- Modify: `User_Setup.h`
- Possibly create: `TFT_eSPI_User_Setups/Setup_ESP32DIV_V1.h`
- Possibly create: `TFT_eSPI_User_Setups/Setup_ESP32DIV_C5.h`

**Requirements:**
- Preserve ILI9341, 240x320.
- Move TFT pins to board-specific macros or separate setup files.
- C5 profile cannot use GPIO32-GPIO35 and should avoid GPIO13/GPIO14 if USB-JTAG must remain available.

**Verification:**
- TFT compiles for original V1.
- C5 build compiles once pins are known.
- Backlight pin does not conflict with NRF24 CE3 as it currently does on original firmware.

---

## Task 7: Port legacy RMT code to ESP-IDF 5 / C5-compatible RMT

**Objective:** Keep precise OOK/SubGHz transmission on C5.

**Files:**
- Create: `rmt_compat.h`
- Create: `rmt_compat.cpp`
- Modify: `subghz.cpp`

**Approach:**
- For original ESP32, keep legacy `driver/rmt.h` path if easiest.
- For ESP32-C5 / ESP-IDF 5.x, use new RMT TX API:
  - `driver/rmt_tx.h`
  - `rmt_new_tx_channel`
  - `rmt_enable`
  - `rmt_transmit`
  - encoder for pulse symbols
- Expose a tiny internal API:

```cpp
bool rmtCompatInitTx(int gpio, uint32_t resolutionHz);
bool rmtCompatWriteSymbols(const RmtCompatSymbol *symbols, size_t count, bool waitDone);
void rmtCompatDeinit();
```

**Verification:**
- Original board still transmits with old behavior.
- C5 compile no longer includes unsupported legacy RMT types.
- Logic analyzer confirms pulse widths for Linear/CAME/Nice/Chamberlain protocols.

---

## Task 8: Audit BLE APIs and migrate if needed

**Objective:** Keep BLE scanner/spoofer/jammer features compiling on ESP32-C5.

**Files:**
- Modify: `bleconfig.h`
- Modify: `bluetooth.cpp`

**Steps:**
1. Try compile against ESP32-C5 Arduino core.
2. If `BLEDevice.h`/classic BLE Arduino APIs fail, migrate to NimBLE-Arduino or ESP-IDF BLE APIs.
3. Guard unsupported features with clear UI messages rather than silent failure.

**Verification:**
- BLE scan works on C5.
- BLE advertising/spoofing compiles or is disabled with explicit message.

---

## Task 9: Audit WiFi low-level APIs

**Objective:** Keep WiFi scanner/monitor/deauth/captive portal building and behaving on C5.

**Files:**
- Modify: `wificonfig.h`
- Modify: `wifi.cpp`

**High-risk APIs:**
- `esp_wifi_set_promiscuous`
- `esp_wifi_set_promiscuous_rx_cb`
- packet structs from `esp_wifi_types.h`
- `esp_wifi_80211_tx`
- channel setting APIs

**Verification:**
- Packet monitor receives frames.
- Scanner sees APs.
- Captive portal starts AP and DNS server.
- Deauth packet send compiles; runtime legality/effectiveness must be tested in a controlled lab only.

---

## Task 10: Fill real ESP32-C5-WROOM-1 pin map

**Objective:** Replace the placeholder C5 `#error` with the real carrier board wiring.

**Required input from Victor:**
- Exact ESP32-C5 dev board/module carrier board.
- Which display/touch board is used.
- Whether SD card is required.
- Whether all three NRF24 modules are required.
- Whether USB-JTAG must remain usable.
- Wiring table for TFT, XPT2046, PCF8574, NRF24(s), CC1101, SD, backlight, reset.

**Acceptance criteria:**
- No C5 profile pin uses GPIO32-GPIO35.
- No C5 profile pin uses GPIO16-GPIO22.
- No boot strap pin is pulled to an unsafe reset level by an external peripheral.
- If GPIO13/GPIO14 are used, README explicitly says USB-JTAG is disabled.

---

## Task 11: Hardware smoke-test sequence

**Objective:** Validate the port feature-by-feature on real hardware.

**Order:**
1. Boot serial log.
2. TFT initializes.
3. Touch calibration works.
4. PCF8574 buttons work.
5. WiFi scan works.
6. Captive portal starts.
7. BLE scan works.
8. NRF24 scanner initializes one radio at a time.
9. CC1101 initializes and reads RSSI.
10. SubGHz RMT pulse output verified by logic analyzer.
11. Radio mode switching tested repeatedly:
    - NRF24 -> SubGHz -> NRF24
    - WiFi -> NRF24 -> Touch still works
    - SD -> NRF24 -> SD

**Pass condition:**
- No boot loops.
- No watchdog resets after 15 minutes of menu navigation and radio switching.
- Touch remains responsive after every radio mode transition.

---

## Immediate recommendation

Do not start by editing random pins. This firmware is too hardcoded and too RF/SPI-sensitive; that path turns into spaghetti with a soldering iron.

Start with board abstraction and reproducible builds, then do the C5 pin map once the carrier wiring is confirmed.
