# Ethernet Priority and Network Status

Version: `2.0.97RC-P4-Preview` for the Waveshare P4 master. The shared web status
also distinguishes WiFi, setup AP and offline state on the S3. No hardware was
flashed automatically; the owner reported successful flashing/setup of the
previous P4 preview.

## Behavior

- Ethernet is preferred only after it has a valid IP address, not just a cable
  link. After 1.5 seconds of quiet/stable LAN, WiFi is stopped with
  `WiFi.mode(WIFI_OFF)`. Saved credentials are not erased.
- On LAN/IP loss, saved WiFi is started automatically. Without saved credentials
  the setup AP and captive DNS return. Existing retry/static-IP behavior stays
  in place. LAN comes back into preference when it obtains an IP again.
- BLE pairing, master OTA and module OTA defer WiFi shutdown. A quiet interval
  after those operations protects their final acknowledgements. The C6 is not
  manually powered down or reset by the policy; its SDK keeps BLE/WiFi ownership
  separate. Actual C6 radio behavior still needs a physical check.
- An explicitly requested WiFi scan can temporarily enable STA without auto
  association on LAN. After the scan, WiFi is suspended again. A 30-second hold
  limit also handles an abandoned scan UI. Scans are rejected while BLE/OTA is
  busy. Failed WiFi shutdowns are retried with a five-second backoff.
- The HTTP tick uses acknowledged policy state instead of repeatedly issuing a
  hosted `getMode` RPC. MQTT socket reconnection remains in its own task.

## Display

All master web pages share one fixed-size network-icon slot next to Bluetooth:

- Green Ethernet-port symbol: active Ethernet.
- Green WiFi symbol: active WiFi station.
- Yellow WiFi symbol: setup AP.
- Gray symbol: offline or unavailable status; tooltip distinguishes those cases.

Tooltips include the active IP and, for Ethernet, WiFi-off state. Bluetooth keeps
its own activity indication. The live status card shows Ethernet/IP rather than
an irrelevant RSSI bar. Network/System reports the selected transport and WiFi
shutdown state. The post-save redirect prefers the Ethernet IP when active.

The compact header endpoint's legacy `wifi` field still means display
provisioning is possible. Icons use explicit `network_transport`,
`wifi_connected`, `wifi_enabled` and `ethernet_connected` fields instead.

## Check on Hardware

1. Boot with LAN and saved WiFi: Ethernet web UI works; after a short interval,
   the master disappears as a WiFi station and the header shows Ethernet.
2. Start/cancel Bluetooth pairing on LAN, then pair a display. BLE must work
   while master WiFi is off; check for resets and correct saved pairing.
3. Unplug LAN: saved WiFi reconnects, or the setup AP returns if unconfigured.
   Restore LAN: Ethernet becomes active and WiFi switches off again.
4. Exercise an explicit WiFi scan and module/master OTA. Verify no shutdown
   occurs during those operations and that their final acknowledgements arrive.
5. Verify MQTT availability/discovery and control after each network change.

LAN and WiFi can have different IP addresses. Existing browser sessions and
clients configured with a literal old IP do not automatically migrate; use the
reachable address/hostname or update the client configuration. Display pairing
still requires saved WiFi credentials even when the master itself uses LAN.

Native tests exercise the actual WiFi lifecycle and policy, including guards,
credentials, recovery, scans, backoff and millisecond rollover. Browser tests
exercise the actual shared header/status scripts and screenshots at
320/390/768/1440 pixels. These are not physical radio/failover verification.

## Verified Build

- Full P4 PRE-V3 build passed on Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5.
- App BIN: 3,258,144 bytes; fits the 3,342,336-byte OTA slot.
- 306 Python regressions, actual S3/P4 WiFi lifecycle native tests, existing
  P4 BLE/network/OTA native tests and runtime/MQTT worker tests passed.
- S3 compiler checks passed with MQTT worker enabled and disabled.
- Actual shared header/status JavaScript passed browser checks and screenshots
  at 320, 390, 768 and 1440 pixels; existing asynchronous WiFi-scan JS passed.
- ELF/BIN verification proved current P4 version/chip ID and software ECC/RNG
  backport origins; no old SDK ECC was linked.
- Local binaries: `MasterP4/OpenFumeExtractorMaster_P4/build/prev3/` (ignored by Git).

App BIN SHA-256:
`8b69e9bb7285adc8f0e49d89614b98676127a3428a26ee6aa2f5aac23be110cb`

No flash or GitHub upload was performed. Physical LAN-loss/recovery and
Bluetooth operation with master WiFi off remain to be verified on the board.
