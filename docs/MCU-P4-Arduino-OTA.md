# MCU-P4 Arduino IDE Exports And OTA Revision Bounds

## Why Selecting V3 Was Not Enough

The owner selected the correct V3 SDK. The exported image still had chip ID 18
with revision limits 0..65535. Stock Arduino-ESP32 3.3.12 selects the compiled
SDK through its Chip Revision menu but its two `elf2image` recipes do not pass
that SDK's revision bounds to esptool. Consequently an OTA receiver cannot
distinguish a PRE-V3 build from a V3 build using that image header.

The MCU's fail-closed chip/revision guard is unchanged. Do not bypass it or
relabel a build from the wrong SDK.

## Separate Arduino IDE Profiles

Run `tools/install_mcu_p4_arduino.ps1`, then restart Arduino IDE. The installed
platform is `OFE MCU P4` and offers:

- `OFE MCU-P4 (Waveshare DEV KIT, PRE-V3, 16 MB)`:
  `ofemcu:esp32:ofemcu_p4`, 360 MHz, SDK `esp32p4_es-libs/3.3.12`,
  image revisions 1..199.
- `OFE MCU-P4 PoE (Waveshare WIFI6-POE-ETH, V3.1+, 32 MB)`:
  `ofemcu:esp32:ofemcu_p4_poe`, 400 MHz, SDK `esp32p4-libs/3.3.12`,
  image revisions 301..399, including the owner's V3.2 chip.

Both profiles pin PSRAM, QIO/80 MHz flash, bridge UART serial, and a 5 MiB OTA
app limit. Use the matching generated MCU sketch and its `partitions.csv`.
Build and export normally from Arduino IDE. There is no independent revision
menu that can silently select a different SDK or leave image bounds unset.

The installer creates only `Documents/Arduino/hardware/ofemcu/esp32`. It keeps
its own platform/board files there and shares the pinned stock core resources
through checked junctions. The Boards Manager ESP32 core, SDK binaries, S3 and
display board profiles are not modified. Keep Arduino-ESP32 3.3.12 installed.
An unowned existing runtime or unexpected junction is rejected.

`tools/tests/check_mcu_p4_arduino.ps1` compiles a minimal sketch against both
actual SDKs, asserts SDK revision/CPU/PSRAM selection in C++, and verifies the
chip ID, flash size and bounds in the emitted app AND bootloader. Both profiles
passed. Expanded Arduino build properties were checked as well.

## Owner's Existing Export

For the reported export, the SDK config and ELF map identified the V3 SDK.
Re-exporting its ELF with stock esptool arguments and its original DIO/80 MHz,
32 MB settings reproduced the original `.bin` byte-for-byte:

`9be8de6594ef9a872d866cadc6b41b1b5df2690f683d45d41ac2a97e2517fa75`.

Re-exporting that same ELF with `--min-rev-full 301 --max-rev-full 399`
and `--elf-sha256-offset 0xb0` produced:

`tools/tests/build-checks/p4-poe/ide-reexport-2126/MCU-P4-PoE-2.1.26RC-Developer.ota.bin`

SHA-256:
`24a3efd6384a17e38233da4de94b4ce8378468c34cbfe3c86c0026672d759c7d`.

It is 3,566,512 bytes. All application segments and their checksum are
unchanged; only the image revision metadata and image validation hash differ.
Esptool verifies the resulting checksum and SHA-256. The original IDE export
is preserved. This is an OTA app, not a complete USB-flash image. No device was
flashed and nothing was published.

## Separate C6 Issue

The user confirmed that MCU 2.1.26 restores date/time. WLAN scan works on the
C6, but version RPC fails with `version_error=-1`; C6 update preflight returns
`C6_UNAVAILABLE` with `written=0`. This proves no firmware data reached our C6
write loop. It does not identify the installed C6 version or prove it is older.
The Arduino image metadata correction does not fix that separate RPC failure.

The pinned SDK routes `esp_hosted_get_coprocessor_fwversion()` through a
transport readiness check and a version RPC. ESP_FAIL (-1) does not distinguish
transport-not-ready from every RPC failure. Serial SDK logs from startup and
the failing attempt are needed before choosing a recovery/update path. Do not
replace unknown C6 version with the host's 2.12.13 or bypass version checks.

Primary references:

- ESP-Hosted v2.12.13 host API:
  <https://github.com/espressif/esp-hosted-mcu/blob/v2.12.13/host/api/src/esp_hosted_api.c>
- RPC implementation:
  <https://github.com/espressif/esp-hosted-mcu/blob/v2.12.13/host/drivers/rpc/wrap/rpc_wrap.c>
- Correct PoE board recovery documentation (not the older DEV KIT page):
  <https://docs.waveshare.com/ESP32-P4-WIFI6-POE-ETH/FAQ>
