# P4 master: ESP-Hosted C6 update design

Status: implemented as an explicit staged updater in 2.0.100RC-P4-Preview.
See [implementation and safety limits](Master-C6-Update-2.0.100RC.md).
The design below records the original requirements. No automatic flashing is
enabled; the first physical C6 update and recovery test remain pending.

## Verified SDK interfaces

The installed Arduino-ESP32 3.3.12 / IDF 5.5.5 P4 SDK identifies its host
component as ESP-Hosted 2.12.13. That is not proof of the version currently
running on a user's C6. Read the actual host/slave versions before updating.

Arduino exposes `hostedGetHostVersion`, `hostedGetSlaveVersion`,
`hostedGetSlaveTargetName`, `hostedHasUpdate`, `hostedGetUpdateURL` and
`hostedBeginUpdate` / `hostedWriteUpdate` / `hostedEndUpdate` /
`hostedActivateUpdate`. The transfer uses the existing SDIO connection.
Activation reboots the coprocessor. Older slave versions need explicit handling;
the SDK has a special activation path for versions before 2.6.0.

Sources:
- [Arduino 3.3.12 Hosted HAL](https://github.com/espressif/arduino-esp32/blob/3.3.12/cores/esp32/esp32-hal-hosted.c)
- [Arduino 3.3.12 Hosted OTA example implementation](https://github.com/espressif/arduino-esp32/blob/3.3.12/libraries/ESP_HostedOTA/src/ESP_HostedOTA.cpp)

Do not directly adopt `updateEspHostedSlave()`: that example disables TLS
certificate verification and does not provide OFE's operation guards, bounded
timeouts, web progress, staged validation or recovery workflow.

## Proposed first implementation

1. P4-only section on Updates: actual C6 version, expected compatible version,
   readiness, progress and error stage. S3 masters do not show this section.
2. Explicit admin confirmation and authenticated/CSRF-protected endpoints.
   Initially require stable Ethernet for starting and completing the update.
3. Allow only a reviewed C6 SDIO WiFi+Bluetooth image matching the host SDK,
   chip, board wiring and existing C6 OTA partition layout. Do not select the
   newest ESP-Hosted major release automatically. An SDK-generated URL is a
   candidate source, not sufficient proof that an image exists or is suitable.
4. Stage the complete image before writing. Check bounded length, target header,
   expected version and a trusted manifest/hash or signature. A downloaded hash
   alone is not authentication: use verified TLS/trusted release metadata.
5. Acquire exclusive maintenance/Hosted ownership. Reject concurrent BLE
   pairing, WiFi scanning and master/module updates. The normal LAN policy
   must not stop or deinitialize the shared Hosted controller during this
   operation, even if WiFi was previously off because LAN is preferred.
6. Write small bounded chunks through the official Hosted APIs in an isolated
   worker, with progress snapshots and timeouts. Keep the RS485 control owner
   responsive; never run a long transfer in its scheduler. Abort must not call
   END/ACTIVATE on incomplete data. The exposed API has no general OTA-abort
   function, so recovery semantics need testing rather than a fictitious cancel.
7. Activate only a fully validated complete image. Expect loss of C6 RPC/WiFi
   and Bluetooth during its reboot. Use OFE's controlled restart policy for
   any required P4 restart and communicate the interruption before starting.
8. Persist pending verification, then confirm actual C6 version and Hosted
   connectivity after restart. Test WiFi reconnect and BLE pairing separately.
   A successful write or activation reply alone is not update verification.

## Hardware gates before enabling the button

- Inventory the current C6 firmware and its partition/rollback support.
- Verify that the exact Waveshare board has a usable C6 recovery/programming
  path. Do not promise recovery through the P4's USB port without testing it.
- Prove the expected signed/hashed image on this board and SDK combination.
- Test interrupted staging, write failure, Ethernet loss, C6 reboot, repeated
  updates and return to normal LAN/WLAN/BLE ownership.
- Do not promise automatic C6 rollback until the slave firmware actually
  supports it and interrupted-update behavior has been tested.
