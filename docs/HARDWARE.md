# NM-CYD-C5 hardware profile

Quetzal supports the NM-CYD-C5 carrier with an ESP32-C5-WROOM-1 module.
`board_config.h` is the source of truth for these assignments.

## Pin map

| Function | GPIO |
| --- | ---: |
| Shared SPI SCK | 6 |
| Shared SPI MISO | 2 |
| Shared SPI MOSI | 7 |
| TFT CS | 23 |
| TFT DC | 24 |
| TFT reset | Module reset (`-1` in software) |
| TFT backlight | 25 |
| XPT2046 CS | 1 |
| XPT2046 IRQ | Not connected |
| microSD CS | 10 |
| RF-HAT CS | 9 |
| NRF24 CE / CC1101 GDO0 | 8 |
| Onboard addressable RGB LED | 27 |

## Shared RF-HAT slot

The NM-RF-HAT wiring assigns the same CS and control pins to CC1101 and
NRF24L01+. Fit only one module at a time. Quetzal aliases the inherited three
NRF24 instances to this single physical slot and initializes one radio.

Do not attempt simultaneous CC1101/NRF24 use without an external multiplexer and
a corresponding firmware design change.

## Flash and memory

- Physical flash: 16 MB
- Application partition: 4 MB
- PSRAM: disabled in the current build profile
- Bootloader offset: `0x2000`
- Partition table offset: `0x8000`
- OTA data offset: `0xe000`
- Application offset: `0x10000`

The generic `esp32-c5-devkitc-1` PlatformIO profile describes 4 MB flash, so the
project explicitly overrides build and upload flash size and supplies
`partitions_c5_16mb.csv`.

## USB

The build enables native USB CDC at boot. Depending on connection path and host,
the board normally appears as `/dev/ttyACM*` through native USB or `/dev/ttyUSB*`
through the CH340 bridge.

## Hardware cautions

- GPIO 2 and GPIO 7 are strapping pins used by the board's documented SPI bus.
  Avoid external circuits that force unsafe levels during reset.
- GPIO 25 is both a strapping pin and the display backlight assignment on the
  carrier; use the board's intended circuitry.
- Never let two shared-SPI devices drive MISO simultaneously.
- Verify antenna, frequency, power, and regional RF requirements before
  transmitting.
