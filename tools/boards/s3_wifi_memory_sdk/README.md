# S3 RGB WiFi Memory SDK Experiment

Status: **Candidate SDK built and exported in an isolated workspace; A/B
preflight passed. It has not been installed, linked into the full OFE firmware
or tested on hardware.**

The separate local workspace is `C:/esp/ofe-s3-3.2.0-wifi-sram`. It contains the
pinned original builder source, baseline snapshot/hashes and two full defaults
files. Large SDK sources and build output must stay outside the OFE project.
The existing S3/P4 SDKs, Arduino board menus and firmware remain unchanged.

## Scope

The only intended SDK setting difference is:

```text
# CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP is not set
```

Keep Arduino 3.2.0 / IDF `2f7dcd862a`, the original managed-component versions,
Octal PSRAM at 80 MHz, XIP, 64-byte data cache lines and the existing WiFi buffer
counts. Source revisions are recorded in `profile.json` and cross-checked against
the installed SDK's `versions.txt`. The RGB pixel clock stays at **16 MHz** in
the sketch; do not enable `rgbprobe` during the memory A/B test.

The switch disables PSRAM-first allocation for eligible WiFi/lwIP allocations;
it does **not** force every network allocation into internal SRAM. The normal
allocator's size threshold/fallback policy still applies. Internal heap usage
and available LVGL draw-buffer space can change. This is a hypothesis test, not
a confirmed fix for slow OTA.

The installed SDK's root `esp32s3/sdkconfig` describes Quad PSRAM. The actual
display uses `qio_opi/include/sdkconfig.h`, with Octal PSRAM and XIP. Preparation
resolves that active header (including legacy aliases) before constructing the
two defaults. Simply editing the root sdkconfig would test the wrong variant.

## Preparation And Audit

From the project root, using Python 3.11 or newer:

```powershell
$ws = 'C:/esp/ofe-s3-3.2.0-wifi-sram'
python tools/boards/s3_wifi_memory_sdk/prepare.py prepare --workspace $ws --sdk <baseline-sdk-root>/esp32s3
python tools/boards/s3_wifi_memory_sdk/prepare.py fetch-builder --workspace $ws
python tools/boards/s3_wifi_memory_sdk/build.py fetch --workspace $ws
python tools/boards/s3_wifi_memory_sdk/build.py pin-components --workspace $ws
python tools/boards/s3_wifi_memory_sdk/build.py setup --workspace $ws
python tools/boards/s3_wifi_memory_sdk/build.py configure --workspace $ws --profile candidate
python tools/boards/s3_wifi_memory_sdk/build.py build --workspace $ws --profile candidate --jobs 8
```

The `prepare.py` preparation/audit commands never install a toolchain or upload
firmware. `build.py setup` installs build tools only inside the isolated
workspace, and `build.py build` compiles the candidate libraries. The helpers
refuse unrelated nonempty workspaces and conflicting files. Fetch uses exact
commits, not floating branch names.

## Candidate Build Result

The candidate used the pinned IDF 5.4 / Arduino 3.2.0 sources and exact managed
component versions. The full SDK builder target completed successfully, and the
original `copy-libs.sh` exporter generated the S3 SDK package. To export a local
candidate build, create a temporary junction for the exporter's expected build
path, run its CMake target, then remove only that junction:

```powershell
New-Item -ItemType Junction -Path "$ws/builder/build" -Target "$ws/build-candidate-r5"
& "$ws/tools/tools/cmake/3.30.2/bin/cmake.exe" --build "$ws/build-candidate-r5" --target idf-libs --parallel 8
Copy-Item "$ws/baseline-versions.txt" "$ws/builder/out/tools/esp32-arduino-libs/versions.txt"
Remove-Item -LiteralPath "$ws/builder/build"
```

The rebuilt archives differ from the captured baseline (`esp_wifi`: 329,848 to
329,388 bytes; `lwip`: 4,506,032 to 4,505,744 bytes). The active `qio_opi`
header differs only by the intended `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`
setting, component/source pins match, and the audit plus all 11 automated tests
pass. These small archive-size changes do not imply a throughput improvement.

The separate board-profile installer is
`tools/boards/s3_wifi_memory_sdk/install_candidate_board.ps1`. Run it from the
project root with `-CandidateWorkspace $ws`, restart Arduino IDE, and select
**OFE RGB WiFi SRAM Candidate > OFE Display 800x480 (WiFi SRAM Test)**. Its FQBN
is `ofe_sram:esp32:ofe800sram`; the existing High Performance profile remains
separate.

The full OFE S3 sketch still needs a compile/link against this candidate and a
memory review before it is a verified firmware build. Arduino CLI could not be
used safely in this turn: its default startup tried to install helper tools in
the protected global Arduino15 directory. No global SDK or board settings were
changed, and no firmware was flashed. The candidate profile is for a deliberate
test build; keep the known-good firmware available for recovery.

Do not graft replacement archives into the installed SDK by hand. The candidate
must remain isolated until the full sketch build and hardware checks pass.

```powershell
python tools/boards/s3_wifi_memory_sdk/prepare.py audit --workspace C:/esp/ofe-s3-3.2.0-wifi-sram --sdk <candidate-sdk-root>/esp32s3
```

Audit rejects header-only changes, unchanged WiFi/lwIP archives, changed source
or managed-component versions, loss of XIP/cache settings and any other active
header difference. It is an early consistency check, **not** proof of build
provenance or a complete Arduino SDK. It never installs a candidate.

## Hardware Acceptance

Use the same Master, AP, signal conditions and OTA payload, ideally repeated
baseline/candidate runs. Keep the normal image visible throughout. Compare:

- Sustained OTA throughput, retries, duplicate requests and ACK timeouts.
- Free/largest/minimum internal heap before and during OTA and BLE pairing.
- WiFi reconnect after an AP restart, BLE pairing and normal module data updates.
- LVGL draw-buffer size, benchmark/scrolling and absence of RGB artifacts.

Restore the old board/SDK if SRAM pressure, watchdog resets or artifacts appear.
Do not advertise a throughput improvement until measured with the new SDK.

References: [Espressif RGB/WiFi guidance](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html),
[pinned S3 WiFi allocator](https://github.com/espressif/esp-idf/blob/2f7dcd862a62ec7380abe3e64c952d8038661463/components/esp_wifi/esp32s3/esp_adapter.c),
[pinned lwIP allocator policy](https://github.com/espressif/esp-idf/blob/2f7dcd862a62ec7380abe3e64c952d8038661463/components/lwip/port/include/lwipopts.h).
