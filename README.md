# Quetzal

Quetzal is touchscreen firmware for authorized wireless security research on the
NM-CYD-C5 (ESP32-C5-WROOM-1). It combines Wi-Fi, Bluetooth Low Energy,
IEEE 802.15.4, Sub-GHz, and 2.4 GHz tooling in one portable interface.

> Use Quetzal only on systems and radio environments you own or have explicit
> permission to test. Jamming, deauthentication, credential capture, and RF
> transmission may be restricted or prohibited in your jurisdiction.

## Supported hardware

Quetzal targets one board profile:

| Component | Hardware |
| --- | --- |
| MCU | ESP32-C5-WROOM-1, RISC-V |
| Carrier | NM-CYD-C5 |
| Display | 2.8-inch 240x320 ST7789 TFT |
| Touch | XPT2046 resistive touchscreen |
| Storage | microSD and internal LittleFS |
| External RF | NM-RF-HAT with either CC1101 or NRF24L01+ |
| Onboard radios | 2.4/5 GHz Wi-Fi, BLE 5, IEEE 802.15.4 |

The display, touch controller, SD card, and RF-HAT share one SPI peripheral.
Quetzal explicitly releases and restores that bus when switching devices.
Only one external RF module may be fitted and active at a time because CC1101
and NRF24L01+ share the RF-HAT chip-select and control pins.

See [docs/HARDWARE.md](docs/HARDWARE.md) for the complete pin map and hardware
constraints.

## Features

### Wi-Fi

- Packet monitor with channel hopping
- Beacon spammer
- Targeted deauthentication testing
- Deauthentication detector
- Access-point scanner
- Captive portal
- Subnet-aware host scanner using ICMP discovery and common-port checks
  (large subnets are capped to the nearest 254 addresses)

### ESP-NOW

- Broadcast test sending a `QUETZAL-TEST` frame once per second
- Receive test showing sender MAC, RSSI, frame length, and payload

### Bluetooth Low Energy

- BLE scanner
- GATT service and characteristic enumeration
- BLE advertising/device spoofing
- Sour Apple advertisement testing
- BLE HID keyboard and media remote
- NRF24-based 2.4 GHz BLE interference testing when an NRF24L01+ is present

ESP32-C5 does not support Bluetooth Classic. Classic-only upstream features are
not available.

### IEEE 802.15.4 / Zigbee reconnaissance

- Channel activity scanner for channels 11-26
- Passive MAC-layer frame sniffer
- Header metadata including frame type, sequence, PAN/address information,
  RSSI, and LQI

The sniffer does not decrypt Zigbee NWK/APS payloads or act as a full protocol
dissector.

### External 2.4 GHz radio

Requires an NRF24L01+ in the NM-RF-HAT slot:

- Channel scanner
- Spectrum analyzer
- WLAN interference testing
- Proto Kill mode

### Sub-GHz

Requires a CC1101 in the NM-RF-HAT slot:

- Signal capture and replay
- Protocol brute-force workflows
- Sub-GHz interference testing
- Saved signal profiles

### Device tools

- SD/LittleFS file manager with two-tap deletion confirmation
  - LittleFS mount failures are non-destructive; Quetzal never auto-formats a
    partition that fails to mount. Provision or repair it explicitly with
    PlatformIO filesystem tooling.
- Firmware update from SD card or authenticated web OTA
- Serial monitor
- Persistent touchscreen calibration
- Display brightness and timeout controls
- Persistent RGB light with static, flashlight, rainbow, and breathing modes
- Boot-time RF-HAT detection with unavailable menu entries disabled
- Status bar with Wi-Fi, BLE, temperature, and device information

## Build

Quetzal uses PlatformIO and pins the pioarduino ESP32 platform release required
by the ESP32-C5 Arduino 3.3.6 core.

```bash
pio run -e nm-cyd-c5
```

If PlatformIO is installed in its default private environment but `pio` is not
on `PATH`:

```bash
$HOME/.platformio/penv/bin/pio run -e nm-cyd-c5
```

Build artifacts are written under `.pio/build/nm-cyd-c5/`. The important files
for manual flashing are:

- `bootloader.bin`
- `partitions.bin`
- `firmware.bin`

The project vendors a patched TFT_eSPI under `lib/TFT_eSPI` because upstream
TFT_eSPI does not currently provide the required ESP32-C5 processor backend.

## Flash and monitor

The Linux helper builds when needed, discovers the serial port, and writes the
ESP32-C5 images at the correct offsets:

```bash
./flash_c5_linux.sh
```

PlatformIO can also upload and monitor directly:

```bash
pio run -e nm-cyd-c5 -t upload
pio device monitor -e nm-cyd-c5
```

Web OTA uses HTTP Basic authentication. The device displays the `admin`
username and a fresh random password whenever the OTA server starts. HTTP Basic
credentials are sent without transport encryption, so
run web OTA only on an isolated, trusted network; the old client-side
`admin/admin` check is not used.

The flash helper uses the ESP32-C5 bootloader offset (`0x2000`) and detects the
physical 16 MB flash size. Do not reuse classic ESP32 offsets.

## Project layout

```text
quetzal.ino              Main UI, menus, setup, and feature dispatch
board_config.h           NM-CYD-C5 pin map and hardware capabilities
Touchscreen.*            Shared XPT2046 setup and calibration values
wifi.cpp                 Wi-Fi tools, captive portal, and update flows
bluetooth.cpp            BLE and NRF24 tools inherited from the main firmware
subghz.cpp               CC1101 capture/transmit workflows
zigbee.cpp               Native ESP32-C5 IEEE 802.15.4 reconnaissance
ble_gatt_enum.cpp        BLE GATT client enumerator
ble_hid_inject.cpp       BLE HID keyboard/media remote
host_scanner.cpp         Local network host and port scanner
file_manager.cpp         LittleFS and SD browser
hw_detect.cpp            Timeout-bounded RF-HAT detection
rgb_light.cpp            Persistent onboard RGB light control
touch_calibration.cpp    Persistent XPT2046 calibration
lib/TFT_eSPI             Vendored display library with ESP32-C5 support
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for subsystem lifecycle and
shared-resource details.

## Development and verification

Before committing firmware changes:

```bash
$HOME/.platformio/penv/bin/pio run -e nm-cyd-c5
git diff --check
```

A successful compile verifies the complete firmware image but cannot validate
radio timing, display orientation, touch accuracy, or shared-bus behavior.
Follow [docs/TESTING.md](docs/TESTING.md) on physical hardware before publishing
a release.

Source files use UTF-8 and LF line endings, enforced by `.editorconfig` and
`.gitattributes`.

## Known limitations

- NM-CYD-C5 is the only supported board.
- CC1101 and NRF24L01+ cannot operate simultaneously in the single RF-HAT slot.
- Runtime radio and SPI transitions require physical-device testing.
- IEEE 802.15.4 coexistence with an active BLE workflow needs additional
  long-duration hardware validation.
- Some operations are intentionally synchronous and temporarily block other UI
  work while scanning or connecting.
- Host discovery depends on ICMP replies; devices that block echo requests may
  not appear in Host Scanner results.
- Host Scanner probes at most 254 addresses and retains detailed port results
  for the first 64 responsive hosts, reporting when the list is truncated.

## History and attribution

Quetzal descends from [ESP32-DIV](https://github.com/cifertech/ESP32-DIV) by
CiferTech and the [HaleHound edition](https://github.com/JesseCHale/ESP32-DIV)
by Jesse C. Hale. Victor Neri ported that codebase to NM-CYD-C5 and developed
the current Quetzal firmware. The upstream attribution chain is retained in
[LICENSE](LICENSE).

Repository: https://github.com/VictorNeri/quetzal

## License

MIT with retained upstream attribution and responsible-use notices. See
[LICENSE](LICENSE).
