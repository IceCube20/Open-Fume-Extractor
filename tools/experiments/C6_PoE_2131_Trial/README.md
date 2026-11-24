# C6 2.13.1 Compatibility Trial: PoE P4 Revision 3.2

Experimental, not OFE-approved. **Compiled, not flashed or hardware-tested.**
Owner's direct C6 UART recovery is not yet verified: **do not flash this trial yet**.
Only for Waveshare ESP32-P4-WIFI6-POE-ETH, silicon revision 3.2,
32 MiB flash and 32 MiB PSRAM. Not for PRE-V3 boards or P4 displays.

## Scope

This first A/B trial changes the **C6 only**. Arduino-ESP32 3.3.12,
IDF 5.5.5 and the P4-side ESP-Hosted driver 2.12.13 stay unchanged.
It tests whether the newer C6 works with our existing MCU; it does **not**
exercise host-side 2.13.1 fixes or establish full-SDK compatibility.
The stable MCU updater, SDK installation and online approval catalog are untouched.

The tool accepts only these pinned official C6 images, both from immutable
Espressif commit `0c2733048bd001c87b0d6912843de5f259b2a13e`:

| Purpose | C6 | Bytes | SHA-256 |
| --- | --- | --- | --- |
| Test | 2.13.1 | 1244160 | `2d8a98801680f1feccdc8c20ace05617c425d4f8f5041fd5b17cb8dc802c643f` |
| Restore | 2.12.13 | 1226976 | `f8509949826c197a4fa3c7958943b4e8985cffdfdf4204c2847d8953905121c9` |

TLS certificate validation, exact size, ESP image header/C6 chip ID and full
SHA-256 are checked before writing. No arbitrary URL/file or unknown installed
version is accepted. A download alone never flashes. No automatic flashing on boot.

## Before Flashing Anything

- Use a spare board, back up the OFE configuration and keep the working MCU-P4
  PoE application ready to reinstall. Disconnect the OFE bus/controlled equipment.
- Verify a **direct C6 UART recovery path** for this exact board. P4 USB is not
  automatically C6 UART. SDIO restoration depends on working C6 OTA/RPC and is
  therefore not a guaranteed recovery from broken firmware or a power failure.
- Use reliable power and Ethernet with Internet access. TLS needs a valid clock;
  the tool attempts NTP and refuses the download if time is unavailable.
- The tool temporarily replaces the **P4 application**, not the C6 bootloader.
  Do not use whole-chip erase. Its partition addresses match our PoE MCU, keeping
  the OFE NVS address unchanged. Still take a backup; this is a trial.

## Build, Without Upload

Install/keep Arduino-ESP32 **3.3.12**. From the OFE project root:

```powershell
./tools/experiments/build_c6_poe_trial.ps1
# For the P4 native USB CDC connector instead of a UART bridge:
./tools/experiments/build_c6_poe_trial.ps1 -NativeUsb
```

The script checks P4 chip ID and image revision bounds 3.1..3.99. It builds only;
it neither uploads nor changes Arduino packages. The app is in
`tools/tests/build-checks/c6-poe-2131-trial-default/` (or `-cdc/`).
Do not feed this P4 trial application to the C6 firmware file chooser.

Arduino IDE: open `C6_PoE_2131_Trial.ino`, select **ESP32P4 Dev Module**,
**post-V3**, **32 MB flash**, **PSRAM enabled**, **custom partition scheme**,
and the serial mode matching the cable. Open serial monitor at 115200 baud.
Do not upload until the backup and direct C6 recovery prerequisites are met.

## Explicit Serial Commands

After manually uploading the temporary tool to the P4, inspect `INFO` first.
The detected C6 must be exactly 2.12.13 or 2.13.1. Unknown versions are refused.
`SCAN` checks WiFi scanning without storing credentials.

1. Send `FETCH TEST`. Wait for the size/hash verification and **NOT flashed**.
2. Only after independently verifying the direct C6 UART recovery path, send
   `RECOVERY CHECKED`. This explicit owner confirmation expires at each reboot;
   the tool cannot electrically verify the wiring itself. Then, after checking
   the target, send `FLASH TEST 2.13.1`.
3. After success the P4 restarts into this tool. `INFO` must confirm C6 2.13.1.
4. Reinstall the normal MCU-P4 PoE application, with whole-chip erase disabled.
   Its host remains 2.12.13; a newer-C6 warning is expected, not proof of failure.
5. Run the comparison checklist below. Do not publish an approval from a compile
   or one successful boot.

For return to baseline: manually reinstall this temporary tool on the P4,
send `FETCH BASELINE`, then **`RESTORE BASELINE 2.12.13`**. After restart verify
the version. The per-boot `RECOVERY CHECKED` confirmation is required here too.
Then reinstall the normal MCU application. The production MCU
updater intentionally does not offer this downgrade. A failed OTA attempt locks
all other commands except `RESTART`; partial transfers are never activated.
If C6 RPC/OTA is no longer responsive, use the previously verified direct UART path.

## Compare Against 2.12.13

Record MCU version, board, P4 host version, C6 version and serial logs for each run.

- Cold boot, C6 version RPC and WiFi scan.
- WiFi connection, AP restart/reconnect, DHCP/static addressing and NTP.
- LAN preferred, cable removal/reconnect and WiFi fallback.
- Display BLE pairing, reconnection, and WiFi/BLE simultaneous operation.
- Module WiFi OTA speed/errors, MQTT and native HA updates.
- Internal heap/minimum, loop peaks, SDIO/RPC errors and at least a 24-hour run.
- Successful restoration to 2.12.13, including another cold boot and BLE test.

Keep release approvals unchanged until the exact board/SDK combination has
passed owner hardware tests. A separate rebuilt 2.13.1 host SDK would need its
own comparison; no SDK archive is rebuilt by this tool.

## Local Verification (2026-11-23)

- Stock Arduino-ESP32 3.3.12, post-V3, 32 MiB flash, PSRAM enabled, UART console:
  compile/link passed. App 1196032 bytes; static globals reported 32888 bytes.
- App and bootloader ESP32-P4 chip ID/revision bounds 3.1..3.99 verified.
- Both official C6 downloads were independently checked in memory against the
  recorded size, C6 chip ID and SHA-256. No device was flashed or hardware-tested.
- Native transfer-policy tests passed: exact target allowlist, explicit restore,
  board/SDK/hash/version gates, write/end/activation failures, timeouts and wrap.
- Native USB CDC is a separate build option, not yet build-verified here.
- Owner's physical C6 UART recovery and all radio/performance tests remain unverified.

Upstream references: [2.13.1 changelog](https://github.com/espressif/esp-hosted-mcu/blob/v2.13.1/CHANGELOG.md),
[OFE review issue](https://github.com/IceCube20/Open-Fume-Extractor/issues/1).
