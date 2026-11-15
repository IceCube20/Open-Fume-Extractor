# ELECROW 5-Inch ESP32-P4 Preparation

## Hostname Identity Fix (1.0.8alpha-P4Prep)

The local P4 sketch now uses `esp_efuse_mac_get_default()` for the hostname
suffix, matching the P4's factory hardware identity. It must not request
`ESP_MAC_WIFI_STA` from the P4: networking runs on the hosted C6, and a local
WiFi-MAC lookup can fail and produce the old `-000000` fallback.
The hostname remains `OFE-Display-P4-800x480-XXXXXX`, with the last three
factory-MAC bytes in normal hexadecimal order. This is the P4 identity, not
the C6's on-air WiFi MAC. Hostname assignment still precedes STA startup.

Recompile and flash the P4 sketch, then restart it to renew its DHCP hostname.
The generator includes this fix; S3 sources, pairing credentials, graphics and
panel timing are unchanged. Source tests and the native production-method
test passed; this change has not been flashed or hardware-tested by the agent.
The existing GitHub 2.0.75 release remains unchanged at P4 1.0.7alpha-P4Prep.

## Earlier Bring-Up Notes

**Status: preparatory port, version 1.0.6alpha-P4Prep. Software ECC + P4 RNG and the board owner's artifact-free RGB test profile integrated. Fill-only PPA tested by the owner without an FPS gain; the extended fill + image mode still requires hardware testing.**
No release binary is supplied, signed or published. Do not flash this to an
ESP32-S3 display. Existing S3 UI/driver code, SDKs and libraries remain unchanged.
The shared protocol now includes `CAP_DISPLAY_P4` (bit 28), and the Master has
P4-specific naming, firmware selection and image-header checks.

Open the independent sketch:

`Module/DisplayModule_P4_800x480/DisplayModule_P4_800x480.ino`

## Exact Board

This port targets **ELECROW CrowPanel Advanced 5-inch ESP32-P4, 800x480, V1.0**.
The manufacturer's 5-inch Arduino sources select **RGB/ST7262 + GT911**, not
MIPI-DSI. Some generic product descriptions and other-size examples mention
MIPI; do not use the 7/9/10.1-inch EK79007 configuration for this board.

Verified primary sources:

- [Official 5-inch repository](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen)
- [RGB, backlight and board configuration, Arduino lesson 07](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/tree/master/example/V1.0/Arduino_Code_5inch/Lesson07-Turn_on_the_screen)
- [UART connector, Arduino lesson 04](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/tree/master/example/V1.0/Arduino_Code_5inch/Lesson04-Serial_port_usage)
- [C6 SDIO configuration, Arduino lesson 16](https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/tree/master/example/V1.0/Arduino_Code_5inch/Lesson16-Wi-Fi_function)

The manifest pins the specific reviewed source-blob hashes. Board electrical
revision and delivered C6 firmware still need confirmation on arrival. Pin
values are implemented locally; no manufacturer source library is installed
or replaced, and no C6 firmware update is requested automatically.

## Prepared and Deferred

| Area | Preparation |
| --- | --- |
| OFE interface | Copied from the current stable 800x480 UI, including original fonts, icons, layouts and Home cadence |
| Display | P4 `esp_lcd` RGB driver; manufacturer pinout/timings; RGB565 single PSRAM framebuffer with internal bounce buffers |
| Touch | GT911 over shared I2C; factory orientation, address probing, existing touch behavior |
| Backlight | STC8 I2C power/PWM registers, including screensaver dimming and automatic light-off logic |
| OFE bus | Existing protocol and display commands, UART RX48/TX47 |
| WLAN | Existing authenticated OFE tunnel through ESP-Hosted C6; pins set before driver initialization |
| Pairing | RS485 provisioning, manual WLAN/Master entry and direct Master-to-display Bluetooth |
| Bluetooth | C6 ESP-Hosted + SDK NimBLE peripheral; same GATT UUIDs and two-sided numeric confirmation as S3 |
| Master OTA | RS485 and authenticated WLAN bulk path enabled; dedicated P4 target and chip validation |
| USB programming | Initial build/upload route; actual board and chip revision must match |
| Graphics acceleration | Optional PPA opaque fills or fills + eligible RGB565/RGB888 images via the separate High Performance board menu; unsupported tasks retain software rendering; software remains the default |

The signature target is **`DISPLAY_P4_800X480`**, never the S3
`DISPLAY_800X480`. Update the Master to this P4-aware source before OTA use.
The P4-aware Master source is **2.0.61RC** (with the usual build-profile suffix).
The Master checks both the signed package target and the ESP image's chip ID;
the P4 additionally checks the streamed chip ID and OFE marker before committing
the inactive application slot. Existing SHA-256/Ed25519 verification is retained.
This task does not create production signatures, publish binaries or change keys.
Firmware-update progress for other modules remains part of the copied UI.

Do not use an older Master to update the P4: it does not know the new MCU bit.
Older firmware can still transport ordinary display commands, but is not an
approved OTA host. Runtime connection, provisioning and controls remain untested
on physical P4 hardware.

## Bluetooth Lifecycle and Security

The P4 uses the SDK's native NimBLE APIs and Arduino's `hostedInitBLE()` /
`hostedDeinitBLE()` C6 transport lifecycle, not the S3's NimBLE-Arduino controller
initialization. Radio pins must already be configured. WiFi's shared SDIO
reference remains active when pairing stops. No co-processor OTA is performed.
OFE1 advertisements use display kind 3; the updated Master recognizes it as
`Display P4 800x480`. Kinds 0, 1 and 2 retain their existing S3 meaning.

Pairing is explicitly started on the display. Advertising expires after 120
seconds. Once a peer connects, it gets a separate bounded 120-second attempt;
the Master likewise starts a fresh attempt after selecting a display, rather
than consuming its search time. The Bluetooth stack can enforce an earlier
security-procedure timeout. Press **Code matches** on both the Master web page
and the display after comparing the six-digit code. Only one peer
is accepted. Writes require an authenticated, encrypted connection with a
16-byte key, an approved comparison and a configuration addressed to this UID.
The result becomes successful only after the existing storage callback verifies
persistence. Reject/cancel/disconnect and invalid input do not commit credentials.
Host callbacks never wait for the touchscreen. Stop/deinit run in the worker,
not the host callback; a failed teardown prevents unsafe reinitialization.

Master 2.0.75RC and P4 1.0.1alpha-P4Prep distinguish an early connection/security
failure from an actual OFE pairing deadline. On failure the UI retains the
phase and numeric BLE error, without exposing WLAN credentials or keys. If
pairing still fails after both confirmations, report the exact message from
both devices; an early disconnect alone does not prove a timeout or C6 defect.

### Known P4 SDK Secure-Connections Failure

[Espressif issue 241](https://github.com/espressif/esp-hosted-mcu/issues/241)
describes the same-code-then-failure symptom with a P4 host and C6 co-processor.
The reported cause is P4 TinyCrypt/ECC handling, not a missing confirmation or
an inherently broken SDIO transport. Its upstream correction is
[ESP-IDF 9fd7cb7e606](https://github.com/espressif/esp-idf/commit/9fd7cb7e606).
P4 status `1035` (`0x040B`) indicates a local DHKey-check failure; peer status
`1291` (`0x050B`) indicates the peer reported that failure. Those codes identify
the failing security stage, but do not alone prove a particular SDK defect.

The installed Arduino 3.3.12 P4 SDK reports ESP-IDF `v5.5.5 b774170ff46` and
ESP-Hosted `2.12.13`. Its extracted `libbt.a` still has the old unconditional
hardware-multiply path in `EccPoint_mult`, without the upstream scalar-width
guard/software fallback. From **1.0.2alpha-P4Prep**, the P4 sketch includes the
official correction as four local C translation units. They replace the matching
TinyCrypt link objects, not the entire SDK. All five upstream source/header
files are byte-for-byte pinned to the above commit; hashes and licenses are in
`src/tinycrypt/`. No installed SDK, S3 source or C6 firmware is changed.

The backport deliberately accepts only **Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5**.
A different SDK fails at compile time until the ABI and final link are reviewed;
do not remove that guard merely to compile after a Boards Manager update.
Encryption, Secure Connections, numeric comparison and MITM protection stay
enabled. There is no insecure pairing fallback.

The board owner reported `Phase 1, BLE -1001` with 1.0.2. That code was the
combined local ECC self-test failure, before advertising; it does not identify
which computation failed and is not a new C6 connection timeout. The previous
native tests modeled the accelerator and therefore could not prove its behavior
on the actual board.

Version **1.0.3alpha-P4Prep** selects the unchanged upstream **software ECC**
branches for public-key generation, point validation and DH. The scoped policy
header is included only in the four vendored ECC wrapper translation units.
It does not disable hardware crypto elsewhere in the SDK. Scalar regularization
and optional RNG blinding remain intact. No legacy/unverified pairing is used.

The board owner then reported `Phase 1, BLE -1014`: public-key generation and
point validation passed, but the first DH call returned failure. The installed
SDK's `ble_sm_alg_ecc_init` installs `ble_sm_alg_rand`, which calls
`ble_hs_hci_util_rand` and requests randomness from the C6 controller. The OFE
self-test executes before the NimBLE host task starts, so that HCI source is not
yet usable. This dependency was not represented by the earlier native RNG fixture.

Version **1.0.4alpha-P4Prep** adopts the board owner's working test fix: after
`nimble_port_init()`, install a TinyCrypt callback backed by **P4
`esp_fill_random()`**. Retain it for both the known-answer checks and subsequent
Secure Connections keys. Do this on each pairing start because NimBLE init can
reinstall its HCI callback. Fixed test scalars are never supplied as pairing keys.
The supplied RNG test sketch successfully paired on the physical board according
to the owner; the generated OFE revision still needs its normal device retest.

Entropy prerequisite: [the IDF 5.5.5 P4 RNG documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/system/random.html)
states that stock P4 startup keeps the SAR entropy source enabled. The current
P4 sketch does not use the ADC or disable this source. Future ADC/entropy changes
must preserve a cryptographically suitable source; C6 WiFi being active is not a
substitute for entropy on the separate P4. Do not blindly toggle the bootloader
entropy APIs while other peripherals own the ADC.

Before advertising, each attempt tests two nontrivial full-width P-256 key pairs,
peer validation and both directions of ECDH against independent OpenSSL results.
Failure still stops pairing before credentials can be written. The displayed
error now identifies the exact check:

| BLE code (phase 1) | Failed known-answer check |
| --- | --- |
| -1011 / -1021 | Public-key computation returned failure (pair A / B) |
| -1012 / -1022 | Computed public key differs from expected |
| -1013 / -1023 | Reference peer point rejected |
| -1014 / -1024 | Shared-secret computation returned failure |
| -1015 / -1025 | Shared secret differs from expected |

The serial startup marker is
`P4 BLE ECC: software-only + P4 hardware RNG + self-test v3`.

For the reported P4 `1035` plus Master `531` failure, rebuild/reflash **only the
P4** with the complete generated 1.0.4 folder and keep Master 2.0.75RC. Master
531 (`0x0213`) is the peer-termination consequence, not another timeout reason.
Reopen pairing on both sides and compare/confirm the code again. Do not flash
the C6 as a first response to this SDK crypto error.

Native tests compile the real production software path without a modeled
accelerator, using 64 independently generated OpenSSL ECDH cases, blinding,
invalid peers, all ten injected self-test failures and signatures. A separate
modeled-hardware regression checks the upstream port conversions/scalar guard.
The RNG regression reproduces a failed pre-host callback, then verifies the real
self-test installs the P4 RNG callback and leaves it installed for pairing.
Neither test substitutes for pairing on the physical P4/C6 board. A complete Arduino link
must also pass `tools/tests/check_p4_ecc_link.py <build-directory>`: each live
ECC symbol must come from the P4 sketch, not the old SDK archive members, and
the BLE ECC objects must have no references to accelerator functions.
Distributions of P4 firmware must include the upstream notices and licenses
from `src/tinycrypt/`; no release binaries are published by this change.

Verification results are recorded in the isolated build directory by
`check_p4_ecc_link.py`. The supplied RNG patch has a successful user-reported
hardware pairing test; compile/link/native checks are not independent radio tests.

The stock P4 SDK allocates NimBLE's host memory internally. It cannot be moved
to PSRAM by redefining sketch macros. Pairing memory is temporary and freed on
normal teardown; memory snapshots and the existing low-memory UI are retained.
SDK task/controller/SDIO memory still needs internal RAM. Do not promise a
zero-internal-RAM Bluetooth path or change the installed SDK for this port.

## Board Pins

| Function | GPIO / value |
| --- | --- |
| RGB HSYNC / VSYNC / DE / PCLK | 40 / 41 / 2 / 3 |
| RGB D0-D15 | 8, 7, 6, 5, 4, 14, 13, 12, 11, 10, 9, 19, 18, 17, 16, 15 |
| Touch + STC8 I2C SDA / SCL | 45 / 46, 400 kHz |
| GT911 reset / interrupt | 36 / 42 |
| GT911 addresses | 0x5D, alternate 0x14 |
| STC8 address | 0x2F |
| STC8 LCD power / PWM register | 0x1B / 0x20 |
| OFE UART TX / RX | 47 / 48 |
| C6 SDIO CLK / CMD | 53 / 54 |
| C6 SDIO D0 / D1 / D2 / D3 | 52 / 51 / 50 / 49 |
| C6 reset | 20 |

**GPIO2 is LCD DE, not a backlight pin.** Do not use the Guition/S3 backlight
LEDC configuration. STC8 writes are checked; failed values are not cached as
successful. I2C is initialized once and shared with touch. Backlight polarity,
brightness curve and touch reset must still be visually verified on the board.

The exposed UART is **logic-level UART**, not an RS485 A/B interface. Use a
suitable 3.3 V-compatible RS485 transceiver and common ground. Confirm connector
pin ordering and supply voltage against the board silkscreen/schematic before
wiring; do not assume it matches the existing S3 connector mechanically.

## Initial Driver Settings

- 800x480 RGB565, rotation 0, non-inverted colors (manufacturer orientation).
- Requested PCLK 16 MHz, matching the board owner's artifact-free RNG test sketch
  (previous preparation: 18 MHz). H timing: pulse 4, back 8, front 8; V timing: pulse 4,
  back 16, front 16. Reported scan estimates use these timings, not the S3's.
- HSYNC/VSYNC idle-low flags 0, PCLK negative-edge flag 1, PCLK idle-high 1,
  matching the manufacturer's bundled `BusRGB` conversion defaults.
- One 768,000-byte PSRAM framebuffer and two 20-line internal bounce buffers:
  64,000 bytes of internal RAM in total (previous preparation: 32,000 bytes).
- Original partial LVGL renderer with up to 48 internal draw lines; existing
  SRAM allocation reserves retained. No PSRAM-widget trial or font cache enabled.
- No explicit PSRAM writeback is added to the bounce path. This initial topology
  is a conservative port, not a completed P4 performance optimization.

The P4 base pool, renderer and copy path need measurement with real bus/WLAN
traffic. Do not extrapolate Espressif reference FPS to OFE's UI.

The owner's supplied RNG test folder reports artifact-free output without a newly
built high-performance SDK. Its `HighPerfSDK/` directory contains builder helpers
and a configuration fragment, **not prebuilt SDK binaries**. The current installed
`esp32p4_es-libs/3.3.12` configuration uses 128 KiB L2 / 64-byte lines and 200 MHz
PSRAM. Startup now logs the actual compiled L2 configuration. No installed SDK is
replaced by preparing the sketch; do not apply the S3 high-performance SDK to P4.

An additional, separate **PRE-V3 cache-performance build recipe** is prepared in
[`tools/boards/p4_high_perf_sdk`](../tools/boards/p4_high_perf_sdk/README.md).
It targets 256 KiB L2 / 128-byte lines while retaining the current source and
component versions, CPU/PSRAM clocks and compiler policy. Preparation and candidate
validation are tested; the performance SDK itself has **not** been built, installed
or measured on hardware. This is not part of the default firmware build.

## Arduino Preparation

Use the installed **Arduino-ESP32 3.3.12** with P4/ESP-Hosted support,
the existing LVGL 9.6.x and Arduino_GFX libraries. No S3 High Performance SDK is
used by this sketch. Keep the bundled `build_opt.h` and `ofe_lv_conf.h` together.
Review/remove the version-pinned ECC backport before upgrading to a newer core.
Do not replace the working libraries or clear their caches for this preparation.

Suggested starting board settings:

- Board: **ESP32P4 Dev Module**, not `ofe:esp32:ofe800`.
- Flash: **16 MB**, PSRAM: **Enabled**.
- Partition scheme: **16M Flash (3MB APP/9.9MB FATFS)** for the compile check.
  Both OTA application slots must be retained; send application `.bin` images
  through the Master, never merged/bootloader images.
- Chip Variant: select the actual chip revision reported by the device/tools.
  `Before v3.00` uses the ES SDK / 360 MHz; `v3.00 or newer` uses the newer SDK /
  400 MHz. **Do not guess based on the advertised maximum clock.**
- Serial monitor: **115200 baud**. Initially use the board's UART-debug USB-C
  connection; USB-CDC settings must match the connector selected.
- Leave erase-all disabled. Do not upload until the board arrives and its
  revision and factory operation are recorded.

The existing local SDK enables P4 HEX PSRAM at 200 MHz and hosted SDIO 4-bit,
40 MHz, active-high C6 reset. The actual runtime pins are overridden to ELECROW's
values with `WiFi.setPins`; default Dev Module EV-board pins conflict with RGB.
If pin setup fails, the P4-specific retry path cannot silently initialize the
radio with EV-board pins. Delivered C6 firmware compatibility remains a hardware
test item; this port never requests updating that firmware itself.

## Arrival Checklist

1. Run and photograph the factory display/touch demo first. Record the PCB and
   P4 chip revision. Preserve the factory firmware before experimenting.
2. Compile the matching 5-inch manufacturer display and WLAN examples as a
   reference if the factory behavior or hosted connection is uncertain.
3. Build the OFE P4 preparation for the confirmed chip variant and upload over
   the correct USB port first. Never upload an S3 image.
4. Check `1.0.5alpha-P4Prep`, the P4 startup banner, PSRAM size/speed, GT911 touch
   coordinates and brightness (off/10/30/100%) before connecting controlled loads.
5. Verify RS485 discovery, addresses, Home updates, module details and controls,
   then manual/RS485 WLAN provisioning and reconnect behavior.
6. Check screensaver/light-off wake behavior, original appearance and all touch
   corners. Run the display benchmark and log `heap`, `tasks` and `perf`.
7. Start Bluetooth on the display and search from the updated Master. Test matching
   and rejected codes, cancellation, expiry, repeated pairing, persistence and WiFi
   recovery. Check internal heap after pairing stops.
8. With controlled outputs disconnected, verify P4 identification and rejection
   of S3 firmware. Then test signed P4 OTA over RS485 and WiFi, abort/retry and
   post-update reboot. Actual throughput and P4 acceleration require measurement.

## Generation and Verification

The independent folder is generated from the stable OFE S3 application plus
small P4-specific templates. Existing generated files are checked against their
previous hashes: manual P4 edits are preserved unless `--force` is explicitly
requested. Review such edits before regenerating.

```powershell
python tools/boards/prepare_elecrow_p4.py
python tools/boards/prepare_elecrow_p4.py --check
python -m unittest tools.tests.test_p4_preparation_source
& tools/tests/display_hybrid_native/run_p4_board.ps1
python tools/tests/check_p4_ble_native.py
python tools/tests/check_p4_preparation_build.py --chip-variant prev3
python tools/tests/check_p4_preparation_build.py --chip-variant postv3
```

Source tests check snapshot provenance, unchanged assets, synchronized protocol,
P4-only updates, authenticated BLE and timing integration. Native tests exercise the actual board
helper against fake I2C/GPIO/WiFi interfaces, including failure/retry behavior.
Target checks compile the complete sketch, implementation sources, original
fonts and real LVGL allocator against both installed P4 SDK variants. They use
cached Arduino declarations only, not cached S3 binaries or compiler flags.
They are **not a final full firmware link, upload or physical-board test**.

A separate earlier full Arduino compile/link also succeeded with the installed
3.3.12 `prev3` SDK and the OTA-capable 16 MB partition layout. That
`1.0.4alpha-P4Prep` build with the P4 RNG fix and 16 MHz / 20-line panel profile
uses 1,977,982 bytes of application space and 80,060 bytes of static RAM. Its
ELF/map verification confirms the sketch's software ECC implementations, excludes
the old SDK ECC objects and checks the P4 `esp_fill_random` handoff. This does not
validate the delivered chip revision, panel, C6 firmware or runtime behavior.
The reported static RAM figure excludes dynamic framebuffer, WiFi and BLE
allocations; it is not the free heap that will be available after boot.

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,ChipVariant=prev3 --build-path tools/tests/build-checks/p4-full-prev3 --jobs 4 Module/DisplayModule_P4_800x480
python tools/tests/check_p4_ecc_link.py tools/tests/build-checks/p4-full-prev3
```

For the owner's revision 1.3 board, the separate cache SDK and installed Arduino
profile preserve its existing OTA partition layout. Version 1.0.5 adds a
**LVGL Renderer > PPA fills (experimental)** option, with aligned buffers and
real hardware-call/error counters in `perf`. It does not enable accelerated
images or change the UI. See the [P4 SDK and PPA instructions](../tools/boards/p4_high_perf_sdk/README.md)
and [verification history](../tools/boards/p4_high_perf_sdk/BUILD_VERIFICATION.md).
Keep the Software mode as a fallback; no additional device flash or claimed FPS
gain follows from preparing this option.
