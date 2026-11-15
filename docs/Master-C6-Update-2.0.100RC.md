# P4 master: C6 updates and LAN activity history

The P4-only Updates section now supports staged updates of the ESP32-C6
WiFi/Bluetooth coprocessor over the existing SDIO connection. There is no
automatic installation. S3 masters do not expose these routes or controls.

## Approved firmware

- Host SDK: Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5 / ESP-Hosted 2.12.13.
- Official image: [esp32c6-v2.12.13.bin](https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.12.13.bin).
- Target: ESP32-C6, `network_adapter`, application version 2.12.13, IDF 5.5.5.
- Exact length: 1,226,976 bytes.
- SHA-256: `f8509949826c197a4fa3c7958943b4e8985cffdfdf4204c2847d8953905121c9`.

The image was downloaded over verified HTTPS and examined with Espressif's
image parser: chip ID, application descriptor, image checksum and embedded
validation hash are valid. This is a pinned approved image, not a general C6
flasher. Developer mode cannot bypass the chip, size, hash or version guards.
No vendor binary is redistributed in this repository.

## Operation

1. Open Updates over stable Ethernet, after the normal LAN policy has stopped
   WiFi. Load the approved image from Espressif in the browser, or select the
   same BIN locally. Browser downloads omit credentials and do not receive
   OFE's CSRF token. Internet access is not needed on the P4 itself.
2. Upload into a bounded PSRAM buffer on the P4. Size, header and SHA-256 are
   checked before the image becomes Ready. No C6 flash API runs during staging.
   Incomplete/abandoned stages expire after 120 seconds; discard is safe here.
3. Confirm availability of C6 UART recovery, then explicitly choose Update C6
   and accept the restart confirmation. The authenticated, CSRF-protected
   install endpoint rechecks ownership, Ethernet and operation guards.
4. A separate low-priority worker acquires Hosted ownership and queries the
   current C6 version. Unknown/pre-2.6 firmware, host mismatch and downgrades
   are rejected. An already-current C6 is not written again.
5. Record pending verification in NVS, write 2 KiB SDIO chunks, finalize and
   activate only after the complete transfer. The transfer deadline is checked
   between SDK calls; individual SDK RPC timeouts also apply. The RS485 owner
   continues running.
6. The P4 requests a controlled restart after activation. The next boot checks
   the C6 version against the journal before displaying version confirmation.
   This confirms the version, not an exhaustive WiFi/Bluetooth hardware test.

The UI preserves an owned staging token across a browser reload. Another
session cannot accidentally discard or install someone else's stage. Network
changes, BLE pairing/scans and master/module updates are excluded while C6
maintenance is active. Ordinary extraction controls remain available.

## Failures and recovery

There is no general abort API after C6 programming starts. Partial or failed
data writes never proceed to END/ACTIVATE. A failed/uncertain BEGIN or subsequent
error keeps maintenance locked and requests an explicit master restart rather
than silently resuming other radio operations. Restore Ethernet if necessary,
then restart. No automatic rollback is promised.

Waveshare documents a separate C6 UART recovery path: put C6_IO9 low at power-on,
put the P4 into download mode too, and use C6_U0RXD/C6_U0TXD with suitable UART
programming hardware. See the [board FAQ](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT/FAQ).
Verify wiring, logic levels and the recovery image before an on-board update.
The acknowledgment checkbox is not proof that recovery wiring was tested.

## LAN LEDs

Both LEDs now sit above the LAN port. Link is steady green while the physical
link is up. The activity callback retains 64 atomic 20 ms traffic bins; status
responses carry the recent packet bitmap. Browser rendering replays that
history with about 700 ms delay instead of blinking once per 500 ms poll.
No extra network polling was added. Physical PHY LED timing cannot be copied
exactly: these are sampled ESP-NETIF events, including the UI's own traffic.

## Local board settings

Optional `src/MasterBoardLocalConfig.h` is included before board defaults and
is neither generated/copied nor published. The existing local external status
LED setting was migrated into this file unchanged. At 2.0.100, shared defaults
kept the external LED off; from 2.0.101 the public P4 default is enabled at the
owner's request. Do not edit generated board headers for local hardware.

## Verification scope

Native tests exercise actual staging, vendor-image SHA-256 checks using the
Windows crypto backend, token/memory lifetime, task failure, NVS journal and
post-reboot version checks. Transfer tests inject LAN loss, write/end/activate
failures, timeouts and rollover, proving incomplete transfers do not activate.
Browser tests cover German/English responsive layouts, staged-only loading,
CSRF, reload ownership, recovery confirmation and error handling. The first
physical C6 update and WiFi/Bluetooth smoke tests remain pending. No device
was flashed by these tests.

Final local PRE-V3 build: `2.0.100RC-P4-Preview`, Arduino-ESP32 3.3.12.
The application occupies 3,309,538 of 3,342,336 slot bytes (32,798 bytes spare);
reported static RAM is 68,180 bytes. This is limited flash headroom, not free
runtime heap. The local external-LED override is enabled for this binary.
Application SHA-256:
`9b14e1bd885d2c9eb9fa7cb595c229e0b48f4e04bbf8b8ea36ec453309a05a25`.
The final ELF/BIN linkage check confirms sketch-owned software BLE ECC and
the pinned RNG handoff; the four Hosted OTA APIs and SHA-256 are linked.
All 318 Python regressions, native P4/network/C6 tests, both browser suites,
and S3 compiler checks with the MQTT worker enabled/disabled passed.
