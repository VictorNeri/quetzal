# Quetzal verification guide

A successful PlatformIO build is required but not sufficient. Complete this
checklist on the target board before publishing firmware.

## Build gate

```bash
$HOME/.platformio/penv/bin/pio run -e nm-cyd-c5
git diff --check
```

Record the generated firmware size and SHA-256 checksum for a release artifact.

## Boot smoke test

1. Capture serial output at 115200 baud.
2. Confirm there is no boot loop or watchdog reset.
3. Confirm the Quetzal splash has correct colors and orientation.
4. Confirm the main menu renders and responds to touch.
5. Leave the device on the main menu for at least 15 minutes.

## Touch and display

1. Run Settings > Touch Calibration and reboot.
2. Verify calibration persists.
3. Test every menu edge and status-bar popup.
4. Exercise brightness and screen timeout.
5. Confirm touch remains active after every storage and radio operation.

## Storage and update

1. Browse, open, and delete a disposable LittleFS file.
2. Mount a known-good FAT-formatted microSD card.
3. Browse and delete a disposable SD file.
4. Return to the menu and verify touch still works.
5. Test SD firmware update with a known-good signed/reproducible image.
6. Test web OTA success and cancellation paths in an isolated network.

## RF-HAT detection

Repeat boot with:

1. No module installed.
2. NRF24L01+ installed.
3. CC1101 installed.

Verify serial detection output, disabled menu items, and absence of boot hangs.

## Wi-Fi

In an authorized lab:

- Scan access points.
- Run packet monitor and channel hopping.
- Start and stop the captive portal.
- Connect as a station and run the local host scanner.
- Verify cancellation during long scans.
- Exercise active test functions only against owned lab equipment.

## BLE and IEEE 802.15.4

- Scan BLE advertisements repeatedly.
- Enumerate GATT on a test peripheral.
- Pair the BLE remote and test keyboard/media reports.
- Disconnect and verify resources are released.
- Run 802.15.4 channel scan and passive sniff on a test Zigbee network.
- Alternate BLE and IEEE 802.15.4 tools repeatedly to check coexistence.

## External radios

With NRF24L01+ installed, exercise scanner, analyzer, WLAN, and Proto Kill setup
and cleanup. With CC1101 installed, validate receive, replay, brute-force setup,
and jammer setup only in a shielded/authorized environment.

Use a logic analyzer to verify Sub-GHz RMT pulse widths before relying on any
protocol encoder.

## Transition stress test

Repeat the following sequence at least ten times:

```text
Wi-Fi scan -> NRF24 tool -> main menu -> SD browser -> main menu
CC1101 tool -> main menu -> touch calibration -> main menu
BLE scan -> Zigbee scan -> BLE remote -> main menu
```

Pass criteria:

- No watchdog resets or boot loops
- No frozen UI
- Touch works after every transition
- Correct RF module remains detected
- No persistent Wi-Fi/BLE state after feature cleanup
- No unexpected heap exhaustion during repeated entry/exit

## Historical stability fixes retained

The inherited firmware previously contained busy button-release loops and
recursive menu polling that could trigger watchdog resets or stack exhaustion.
Current release testing should continue to include rapid touch navigation and
long-duration menu use to catch regressions in those areas.
