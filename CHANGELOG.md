# Changelog

All notable changes to Quetzal are documented here.

## Unreleased

### Added

- Quetzal branding, splash artwork, and terminal-inspired UI palette
- NM-CYD-C5-only hardware profile for ESP32-C5-WROOM-1
- Boot-time NRF24L01+/CC1101 detection and hardware-gated menus
- BLE GATT enumerator
- BLE HID keyboard and media remote
- Native ESP32-C5 IEEE 802.15.4 channel scanner and passive sniffer
- Wi-Fi local host and common-port scanner
- ESP-NOW broadcast and receive diagnostics
- LittleFS and SD file manager
- Persistent RGB light modes
- Persistent touchscreen calibration
- Status-bar connectivity popup
- Reproducible PlatformIO build and ESP32-C5 Linux flash helper
- Architecture, hardware, and physical-test documentation

### Changed

- Ported Bluetooth functionality from Bluedroid compatibility assumptions to
  NimBLE-Arduino for the BLE-only ESP32-C5
- Centralized board pins and capabilities in `board_config.h`
- Adapted display, touch, storage, and external radios to the shared SPI bus
- Replaced the original multi-radio assumptions with one NM-RF-HAT slot
- Updated the 16 MB flash layout and disabled unsafe PSRAM initialization
- Renamed the main sketch to `quetzal.ino`
- Normalized source line endings and explicitly scoped repeated macros
- Switched PlatformIO to the supported `build_src_filter` setting
- Replaced the unavailable IR Remote menu with ESP-NOW tests

### Fixed

- Web OTA now uses server-side Basic authentication with a fresh random password
  and exits cleanly without recursively registering handlers
- BLE HID services are constructed once instead of duplicated on each visit
- ESP32-C5 802.15.4 callback buffers are released back to the radio driver
- Sub-GHz RMT output is routed to CC1101 GDO0, the module's TX data input
- RGB setting changes are coalesced into one NVS write per touch gesture
- Touch calibration times out and rejects implausibly small sample spans
- Host Scanner now uses ICMP discovery, the active subnet mask, bounded scan
  ranges, and paged results instead of a TCP-port-80 timing heuristic

### Removed

- Original-board build target and physical-button/PCF8574 dependency
- Prebuilt firmware binaries and obsolete cross-platform flash bundles
- Bluetooth Classic-only functionality unavailable on ESP32-C5
- Legacy splash assets and duplicate upstream documentation

### Known limitations

- Runtime RF behavior still requires physical-board validation.
- CC1101 and NRF24L01+ cannot be active simultaneously on the current hardware.
