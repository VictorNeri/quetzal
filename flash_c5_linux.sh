#!/bin/bash
# =============================================================================
#        ESP32-DIV HaleHound Edition - NM-CYD-C5 (ESP32-C5) Flash Script
# =============================================================================
# Flashes the ESP32-C5 build produced by:  pio run -e nm-cyd-c5
#
# Do NOT use flash_linux.sh for this board: that script targets --chip esp32
# (Xtensa) with an 0x1000 bootloader offset. The ESP32-C5 is RISC-V and its
# bootloader lives at 0x2000, so the classic script produces a board that never
# boots (dark screen).
#
# Offsets below come from the C5 ESP-IDF sdkconfig and this project's
# partitions_c5_16mb.csv:
#   0x2000  bootloader.bin        (CONFIG_BOOTLOADER_OFFSET_IN_FLASH)
#   0x8000  partitions.bin        (CONFIG_PARTITION_TABLE_OFFSET)
#   0xe000  boot_app0.bin         (otadata init -> boot ota_0)
#   0x10000 firmware.bin          (app, ota_0)
# =============================================================================
set -euo pipefail

echo "================================================================================"
echo "        ESP32-DIV HaleHound Edition - NM-CYD-C5 (ESP32-C5) Flash Script"
echo "================================================================================"
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/.pio/build/nm-cyd-c5"

# --- Build if the firmware artifacts are missing -----------------------------
if [ ! -f "$BUILD_DIR/firmware.bin" ]; then
  echo "No build found in $BUILD_DIR"
  if command -v pio >/dev/null 2>&1; then
    echo "Building the nm-cyd-c5 environment first..."
    pio run -e nm-cyd-c5
  else
    echo "ERROR: firmware not built and 'pio' is not on PATH."
    echo "Run:  pio run -e nm-cyd-c5   (or install PlatformIO), then re-run this script."
    exit 1
  fi
fi

# --- Locate esptool ----------------------------------------------------------
# Invoke esptool through an interpreter that actually has the module + pyserial.
# PlatformIO's bundled penv python is the reliable choice; the raw esptool.py has
# a `#!/usr/bin/env python` shebang that fails on python3-only systems.
ESPTOOL_CMD=()
PIO_PYTHON="$HOME/.platformio/penv/bin/python"
if [ -x "$PIO_PYTHON" ] && "$PIO_PYTHON" -m esptool version >/dev/null 2>&1; then
  ESPTOOL_CMD=("$PIO_PYTHON" -m esptool)
elif command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL_CMD=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL_CMD=(esptool)
elif python3 -c "import esptool" >/dev/null 2>&1; then
  ESPTOOL_CMD=(python3 -m esptool)
else
  echo "ERROR: esptool not found. Install it (pip install esptool) or use: pio run -e nm-cyd-c5 -t upload"
  exit 1
fi

# --- Locate boot_app0.bin (ships with the Arduino framework) -----------------
BOOT_APP0="$(find "$HOME/.platformio/packages" -path '*framework-arduinoespressif32*/tools/partitions/boot_app0.bin' 2>/dev/null | head -1 || true)"
if [ -z "$BOOT_APP0" ]; then
  echo "ERROR: boot_app0.bin not found in the Arduino framework package."
  echo "Fall back to:  pio run -e nm-cyd-c5 -t upload"
  exit 1
fi

# --- Pick the serial port ----------------------------------------------------
# On the NM-CYD-C5 the ESP32-C5 native USB (USB-CDC) usually enumerates as
# /dev/ttyACM*, while the CH340 USB-UART bridge appears as /dev/ttyUSB*.
echo "Available ports:"
PORTS="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true)"
if [ -n "$PORTS" ]; then echo "$PORTS"; else echo "  (none found)"; fi
echo ""
read -r -p "Enter port (e.g., /dev/ttyACM0): " PORT

echo ""
echo "Flashing ESP32-C5 build on $PORT ..."
echo ""

# --flash_size detect: the esp32-c5-devkitc-1 board profile builds the bootloader
# header for 4 MB flash; without this esptool keeps that value and the bootloader
# rejects the 16 MB partition table ("offset 0x10000 ... exceeds flash chip size
# 0x400000"). detect reads the real 16 MB chip and patches the header.
"${ESPTOOL_CMD[@]}" --chip esp32c5 --port "$PORT" --baud 921600 write_flash -z --flash_size detect \
    0x2000  "$BUILD_DIR/bootloader.bin" \
    0x8000  "$BUILD_DIR/partitions.bin" \
    0xe000  "$BOOT_APP0" \
    0x10000 "$BUILD_DIR/firmware.bin"

echo ""
echo "================================================================================"
echo "Flash complete! Press RESET on your device or unplug/replug USB."
echo "Watch it boot with:  pio device monitor -e nm-cyd-c5"
echo "================================================================================"
