# P4 SDK Build Verification (Historical)

This records the initial SDK bring-up, not the current release checklist.
For the subsequently owner-tested GUI-task firmware and published alpha image,
see [2.0.75 RC release notes](../../../docs/RELEASE-2.0.75RC.md).

Local Windows build checked and flashed on 2026-10-23. **Boot and Master connection
verified; benchmark and full functional testing pending.** A separate Arduino
profile is installed; the stock SDK is not replaced. Not signed or released.
This is not a 60 FPS claim.

## SDK

- Workspace: `C:/esp/ofe-p4-3.3.12` (separate from the OFE project and Arduino15).
- Candidate: `builder/out/tools/esp32-arduino-libs/esp32p4_es` in that workspace.
- Arduino 3.3.12, IDF 5.5.5, GCC 14.2; exact source/component pins in `versions.txt`
  and `dependencies.lock` beside the candidate's `sdkconfig`.
- PRE-V3, CPU 360 MHz, PSRAM 200 MHz; only cache configuration changes.
- L2 cache 256 KiB, cache line 128 bytes (baseline 128 KiB / 64 bytes).
- `prepare.py check-sdk`: PASS, including baseline inventory, component versions,
  allowed config differences and QIO/DIO generated cache headers.
- All eight exported compiler/linker flag files match the stock SDK when ignoring
  whitespace. All four QIO/DIO 40/80 MHz bootloader ELFs were built.
- Candidate: 3,376 files, 373,275,694 bytes before ZIP compression.
- SDK config SHA-256:
  `ec2d9bdc31bddce37ef22815292b6be1b615f432706ec22bc5b2191c6acffd63`.

Backup in the workspace:
`OFE-P4-PRE-V3-Arduino-3.3.12-L2-256KiB-128B.zip` (102,828,998 bytes).
Full ZIP CRC test: PASS. SHA-256 is also recorded in the adjacent `.zip.sha256`:

```text
4de2a8454b460f9899a27be622a57e7eb390d47b582e7488e66e8be1c5845099
```

The ZIP contains the complete `esp32p4_es` directory, not the multi-GB build
environment. Keep this SDK intact; do not merge individual archives into stock.

## Full OFE Sketch

- Sketch/version: `DisplayModule_P4_800x480`, `1.0.4alpha-P4Prep`.
- Fresh build: `C:/esp/ofe-p4-3.3.12/firmware-check`, `--clean --jobs 2`.
- Board: `esp32:esp32:esp32p4`, PRE-V3, PSRAM enabled, 16 MB flash,
  `app3M_fat9M_16MB`, hardware USB CDC/JTAG, CDC on boot.
- `compiler.sdk.path` overridden to the candidate, using Windows backslashes.
- Full compile and link: PASS. Program storage reported: 1,985,964 bytes (63%).
  Static globals reported: 80,300 bytes. These are not runtime heap measurements.
- Actual application BIN: 1,986,192 bytes. SHA-256:
  `a584271bd01c82297eef108e9729236c6193bc145f3789f5ad50b2efcd497bca`.
- Copied build `sdkconfig` matches the candidate; the ELF map proves custom SDK
  system-library origin and contains no stock `esp32p4_es-libs/3.3.12` path.
- `check_p4_ecc_link.py`: PASS. Strong sketch ECC symbols, old SDK ECC excluded,
  no ECC accelerator calls; RNG handoff uses P4 `esp_fill_random`.
- RGB profile unchanged: PCLK 16 MHz, 20 bounce lines. No UI/graphic changes.
- Source tests: 212 PASS. P4 generator `--check`: PASS.

The build log is `firmware-check-build.log` in the workspace. The detailed ECC
report is `firmware-check/ecc-link-verification.json`.

## Device Upload And First Boot

- Device: ESP32-P4 revision 1.3, 16 MB flash.
- UART0 upload through COM18 using packaged esptool 5.3.1, 921600 baud.
- Full 16 MB original flash backup completed before writing and kept privately.
  It contains device settings and must not be published.
- Wrote matching bootloader at `0x2000`, OTA selection data at `0xe000` and
  application at `0x10000`. All three esptool hash verifications passed.
- Preserved the device's existing partition table: app0 `0x10000`, app1 `0x340000`,
  each 3264 KiB. The application fits. Did not write the different generated
  FAT/3 MB partition table or merged image.
- Post-upload readback `0x8000..0xdfff`: partition table and NVS byte-for-byte
  identical to the original backup. OTA selection data was intentionally reset
  to boot the new app0.
- Normal boot confirmed through UART0 CLI: `FW=1.0.4alpha-P4Prep`, address `0x40`,
  Master online at uptime 20 s, CPU 17%, loop maximum 21 ms, LVGL buffer 48 lines.
- First sample: internal free 129812 B, largest 47092 B, minimum 119504 B;
  PSRAM free 32271700 B of 33554432 B. Not a long-term stability measurement.
- Runtime PERF sample: 2.8 FPS, 0.41 MPix/s, theoretical scan 37.8 Hz. This was
  not the benchmark or a controlled scrolling test.
- Private logs: `cache256-uart0-upload.log`, `cache256-first-boot-uart0.log`.

## Arduino IDE Profile Verification

Checked on 2026-10-23 after the first device test:

- Installed `ofep4:esp32:ofep4` with `tools/install_display_p4_arduino.ps1`.
  Arduino CLI recognizes **OFE Display P4 800x480 (High Performance, PRE-V3)**.
- SDK copy: `C:/Users/User/Documents/Arduino/ofe-p4-high-performance/sdk/esp32p4_es`.
  Structural validation against the stock 3.3.12 PRE-V3 SDK passed before and
  after copying. The stock SDK and existing S3 platform were not modified.
- Full compile/link through the installed FQBN, without a command-line SDK
  override: PASS. Build folder `C:/esp/ofe-p4-3.3.12/arduino-profile-check`;
  log `arduino-profile-check.log` beside it. No device upload in this step.
- Program storage 1,985,966 bytes (59% of the preserved 3264 KiB OTA slot);
  static globals 80,300 bytes. BIN length 1,986,192 bytes, SHA-256:
  `ca11093cfc1851db3eb3c41db141886d6ae92daf2cd15f458a2e2d7591785537`.
- Link map references the installed custom SDK, not the stock P4 SDK. Exported
  `sdkconfig` has the same verified SHA-256 as above. Generated bootloader is
  byte-identical to the one already flashed. The generated partition CSV matches
  the preserved device layout, including NVS and both OTA slots.
- ECC link check: PASS, pinned sketch backport linked, old SDK ECC excluded,
  no ECC accelerator calls. This is not a new hardware pairing test.
- Updated sketch mirrors automatic PERF and benchmark reports to UART0 as well
  as native USB. Profile rejects a stock cache SDK at compile time.
- P4 source/profile/SDK tests: 44 PASS; generated P4 source check: PASS.
- Owner reports artifact-free operation and about 14.8 FPS on the device's
  benchmark with the earlier flashed build. Not a controlled FPS comparison;
  no rendering-backend or panel-clock change was included in this installation.

## Experimental PPA Build Verification

Checked on 2026-10-23. Prepared only at the owner's request; **not flashed**:

- Firmware `1.0.5alpha-P4Prep`, FQBN `ofep4:esp32:ofep4:Renderer=ppa`.
  The installed board profile now provides Software (default) and PPA fills.
- Full Arduino compile/link: PASS, using the installed cache SDK without a
  command-line SDK override. Build `C:/esp/ofe-p4-3.3.12/arduino-ppa-check`;
  log `arduino-ppa-check.log` beside it. The first attempt exposed an absent
  IDF 6 cache-query API; the scoped IDF 5.5 compatibility bridge resolved it.
- All four actual LVGL PPA C files and the P4 C++ compatibility/diagnostic code
  also passed targeted cross-compilation against this SDK before the retry.
- Program storage: 2,013,058 bytes (60%); static globals: 80,508 bytes. Runtime
  RAM overhead, FPS and stability remain to be measured on the physical display.
- BIN: 2,013,280 bytes, SHA-256:
  `5f8e6fdd69d3f4ecdc3d0748b0b7c9d0cfce40a48f30d036185de1b9eb03b590`.
- `check_p4_ppa_link.py`: PASS. LVGL PPA init/fill, the IDF hardware fill API,
  the diagnostic wrapper and the per-address cache compatibility helper are
  linked. `ppa-link-verification.json` explicitly records hardware/FPS unverified.
- `check_p4_ecc_link.py`: PASS. Original sketch ECC origins and P4 RNG handoff
  proven, old SDK ECC objects excluded, no BLE ECC accelerator calls.
- Bootloader byte-identical to the flashed cache256 bootloader. Partition BIN
  byte-identical to the preserved-layout Arduino profile build. SDK config hash
  unchanged from the verified cache256 profile.
- P4 source/profile/SDK tests: 46 PASS. Native diagnostics tests PASS with PPA
  both off and on, including internal/PSRAM/uncached addresses, cache-query errors,
  hardware-API argument/result forwarding and microsecond timer rollover.
- No UI assets/styles, LCD clock, rotation, refresh cadence, OFE transport,
  installed LVGL source, S3 SDK or Master firmware changed. PPA image dispatch
  and threaded rendering remain disabled.

Follow the [PPA comparison procedure](README.md#experimental-ppa-renderer). Do not
infer an FPS improvement or artifact-free operation from these build checks.

## Extended PPA Image Build Verification

Checked on 2026-10-23; no device upload by the agent:

- Firmware `1.0.6alpha-P4Prep`, FQBN `ofep4:esp32:ofep4:Renderer=ppaimage`.
  Adds a third explicit menu option, **PPA fills + images (experimental)**.
  Software remains the default and the original fill-only option is retained.
- Full Arduino compile/link: PASS, build
  `C:/esp/ofe-p4-3.3.12/arduino-ppaimage-check`; log
  `tools/tests/build-checks/p4-ppaimage.log` in the project.
- Program storage 2,013,614 bytes (60%); static globals 80,524 bytes.
  Application SHA-256:
  `ffb24d84c919f98bb6013e4ff297dac9bc13ff9d2b02b36d8be1dd77c8ea7bec`.
- PPA link check PASS: eligible image path, hardware blend API and diagnostic
  wrapper included, using the installed custom SDK. Reports separate fill and
  blend API calls, errors and maximum elapsed time. This does not accelerate
  unsupported text, rounded shapes, shadows, masks or transformed images.
- ECC link check PASS: sketch ECC/RNG origins retained, old SDK ECC excluded.
- Bootloader and partition BINs byte-identical to the previous fill-only build.
- All 46 P4 source/profile/SDK tests PASS. Native tests PASS in software,
  fill-only and fill+image modes, including argument/result forwarding, separate
  error counters and microsecond timer rollover for both APIs.
- No global LVGL source, S3 firmware, panel timing, UI styles/assets or transport
  changes. No new SDK build or partition change.
- Owner reports about 14.2 FPS for the extended test but has no retained log.
  Actual image dispatch/counters and pixel output therefore remain unverified;
  this is not evidence of an FPS gain. A subsequently supplied older log is
  explicitly fill-only and uses 24 MHz / 56.7 Hz scan, not the 16 MHz baseline.

## Still Required

Check startup `[P4 SDK] L2 cache: 256 KiB / 128-byte lines`, then compare the same
benchmark, scrolling, Home refresh, heap/largest block, BLE pairing/cancel/re-pair,
WiFi OTA and RS485. The larger cache consumes additional SRAM; physical stability
and performance beyond the first boot remain unverified. The cache startup line
uses native USB Serial, not the mirrored UART0 CLI. Do not assume application-only
OTA changes bootloader cache setup. Existing IDF 6.0, Arduino, S3 and Master
installations have not been replaced.
