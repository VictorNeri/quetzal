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
- Verify the displayed subnet mask, ICMP-discovered hosts, common open ports,
  result paging, and the 254-address cap on a subnet larger than `/24`.
- Tap a result to verify every common open port is available in its detail view,
  and verify truncation is reported when more than 64 hosts respond.
- Verify cancellation during long scans.
- Open Assessment Suite and exercise tools in their displayed order:
  1. Confirm Config Auditor reports auth/cipher/WPS and captures PMF flags from
     beacons on a lab WPA2/WPA3 AP.
  2. Reconnect a lab client while EAPOL Capture is open; verify M1-M4 indicators
     and open the generated LittleFS PCAP in Wireshark as radiotap/802.11.
  3. Generate association, deauthentication, and disassociation traffic and
     verify Management Analyzer counters and reason/status fields.
  4. Enroll a rogue-detector baseline, compare an unchanged AP, then test a lab
     clone with an intentionally different security fingerprint.
  5. Generate client traffic and verify AP/Client Mapper relationships and RSSI.
  6. Compare Passive WPS Scanner results with a WPS-enabled lab router.
  7. Verify Channel Survey advances only through the country-configured 2.4 GHz
     channels and labels its counts as observed traffic rather than true CCA
     airtime.
  8. After mapping a lab client, verify Deauth Resilience requires two explicit
     confirmations, sends at most 10 unicast frames, stops on exit, and reports
     TX acceptance/post-test activity without claiming guaranteed disruption.
- Exit and re-enter every assessment repeatedly; confirm promiscuous capture,
  callbacks, files, and Wi-Fi channel state do not leak between tools.
- Exercise active test functions only against owned lab equipment.

## ESP-NOW

Use a second ESP32 configured for Wi-Fi channel 1:

- Run Broadcast Test and confirm one `QUETZAL-TEST` frame arrives per second.
- Run Receive Test and confirm sender MAC, RSSI, byte count, and payload update.
- Exit and re-enter both modes repeatedly to verify ESP-NOW cleanup/re-init.

## BLE and IEEE 802.15.4

- Scan BLE advertisements repeatedly.
- Enumerate GATT on a test peripheral.
- Pair the BLE remote and test keyboard/media reports.
- Open BLE > Assessment Suite and select an owned test peripheral, then exercise
  the tools in their displayed order:
  - Before selection, verify every tool refuses to start. Rescan after selection
    and verify it is preserved only when the exact address is rediscovered.
  1. Verify Security Auditor reports address type, connection security, service/
     characteristic bounds, pre-security reads, and declared write/notify flags.
  2. Verify GATT Permissions sends no writes and distinguishes accepted
     pre-security reads from characteristics that require encryption, without
     triggering NimBLE's automatic pairing retry.
  3. Run Privacy Analyzer against stable-public, static-random, and rotating-RPA
     test devices; confirm same-address and matching-fingerprint observations.
  4. Confirm Pairing Resilience requires two taps, starts one asynchronous
     pairing/security request, reports encryption/authentication/bond/key size,
     stops from the bottom control, expires at its fixed deadline, and never
     guesses passkeys, changes process-wide security policy, or deletes bonds.
  5. Enroll a Rogue Peripheral baseline, compare an unchanged device, test an
     intentionally changed advertising fingerprint, and treat a missing baseline
     device as inconclusive.
  6. Confirm Notification Monitor requires two taps and that the operator-facing
     documentation discloses its CCCD subscription writes. Verify it subscribes to
     at most eight sources for ten seconds and retains only event count, reported
     payload length, and a hash of at most the first 64 bytes. Verify the
     unencrypted/encrypted event counts, the latest
     source UUID, the latest observed CCCD security transition, and immediate STOP
     from the bottom control. Trigger a notification immediately after subscribing
     to verify the pre-registered source slot attributes it correctly.
  7. Confirm ATT Robustness requires two taps, issues at most 24 read-only probes,
     checks the 15-second deadline between procedures, supports bottom-screen STOP,
     and performs a recovery reconnect.
  8. Confirm Connection Resilience requires two taps, performs at most ten
     connect/disconnect attempts no faster than one every 700 ms, and stops on touch.
  9. Test Mesh Auditor with provisioning/proxy UUIDs and AD types 0x29-0x2b.
  10. Confirm Replay Tester lets the user cycle readable+writable candidates and
      inspect the UUID only after target-bound authorization, captures at most 64
      bytes, requires a separate write confirmation,
      writes once only to the same selected target/characteristic, and aborts if
      the connection or retained characteristic is no longer valid. Present a
      readable/writable value over 64 bytes and verify replay rejects it instead
      of sending a truncated prefix. Test values of exactly 64 and 65 bytes,
      including a negotiated MTU below 66, to exercise long-read continuation.
- Exit active tools during scanning, subscription, connection, and post-test states;
  verify scan/client/subscription cleanup and that BLE HID still works afterward.
- Connect a BLE HID peer and verify Assessment Suite refuses to start until that
  peer disconnects.
- Account for NimBLE's synchronous procedure behavior: STOP is handled between
  procedures, while an in-flight operation may wait for the stack timeout.
- Force repeated rapid disconnect/reconnect cycles and verify a new connection is
  not attempted until both the disconnect callback has run and the previous
  NimBLE connection handle is invalid.
- During Notification Monitor, trigger security after CCCD registration and
  verify the authentication callback changes subsequent event classification
  without losing the event's source characteristic.
- Exercise a peripheral with an unusually large GATT database while monitoring
  free heap to characterize NimBLE's transient discovery cache; Quetzal's own
  retained characteristic records must remain capped at 40.
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
