# P4 PRE-V3 Cache Performance SDK Preparation

The bring-up notes below document the initial SDK experiment. The owner later
tested the dedicated GUI-task firmware at 24 MHz and reported about 41 FPS in
the OFE benchmark. See [current release notes](../../../docs/RELEASE-2.0.75RC.md).
The P4 image remains experimental; these results are not a guarantee for other
boards, scenes or chip revisions.

Status: **native Windows candidate built, validated and boot-tested on the owner's
P4; performance and full functional tests pending**. A separate Arduino IDE
profile is installed locally; the stock ESP32 SDK is unchanged.
The generated SDK is kept outside the project and is not
included in this source folder. The complete OFE P4 sketch also compiles and links
against it; see [local build verification](BUILD_VERIFICATION.md).
The owner's working RNG test already pairs and displays without artifacts using
the stock SDK. This is an additional performance experiment, not a required fix.
It does not modify the OFE UI, the S3 SDK, Master or C6 slave firmware.

## Arduino IDE Installation

After the candidate passes `prepare.py check-sdk`, run from the project root:

```powershell
./tools/install_display_p4_arduino.ps1
```

The installer copies the validated SDK to
`C:/Users/User/Documents/Arduino/ofe-p4-high-performance/sdk/esp32p4_es`
and adds a separate sketchbook platform, `ofep4:esp32:ofep4`. Restart Arduino IDE,
then choose **OFE P4 High Performance > OFE Display P4 800x480 (High Performance,
PRE-V3)**. Open `Module/DisplayModule_P4_800x480/DisplayModule_P4_800x480.ino`.
UART0 and native hardware USB CDC are supported; select the actual device port.
Serial monitor speed is 115200 baud. Both consoles receive PERF/benchmark reports
with the updated sketch.

The board fixes PRE-V3, PSRAM enabled, CPU 360 MHz, 16 MB flash and hardware CDC.
It uses the existing `default_8MB` partition layout (two 3264 KiB OTA slots),
verified against the owner's original 16 MB flash. Its name describes the layout,
not a reduction of the physical flash size. Do not choose a different partition
layout casually. The board's cache guard rejects an accidentally selected stock
128 KiB SDK.

The standard ESP32 and OFE S3 platforms are not replaced. Core resources reference
the installed ESP32 **3.3.12** through junctions; keep that version installed.
Do not edit the linked `cores`, `variants`, `libraries` or `tools` directories.
Back up the SDK ZIP and installer/profile sources, not private flash/NVS data to
public repositories. Installation does not flash a device.

## Profile

| Setting | Installed baseline | Experimental candidate |
| --- | --- | --- |
| Arduino-ESP32 | 3.3.12 | unchanged |
| IDF | 5.5.5 (`b774170ff46`) | unchanged |
| Chip variant | `esp32p4_es`, PRE-V3 | unchanged |
| L2 cache | 128 KiB | 256 KiB |
| L2 cache line | 64 bytes | 128 bytes |
| CPU / PSRAM | 360 / 200 MHz | unchanged |

Only the two cache choices in `sdkconfig.defaults` are changed. No overclock,
PSRAM XIP, BLE policy or SDK-wide optimization change is included. A larger cache
is not free heap; measure internal RAM and BLE headroom again. More FPS are not
guaranteed. The existing software ECC/RNG fix remains in the P4 sketch.

## Experimental PPA Renderer

Firmware **1.0.6alpha-P4Prep** extends the **LVGL Renderer** board menu:

- **Software (stable baseline)**: default; no PPA draw unit.
- **PPA fills (experimental)**: original opaque rectangle fill test.
- **PPA fills + images (experimental)**: all drawing paths currently supported by
  the installed LVGL PPA backend, including eligible RGB565/RGB888 images.

After installing the updated profile, use **Tools > Reload Board Data** (or restart
Arduino IDE). Select the third option to run the extended test. CLI equivalent:

```powershell
arduino-cli compile --fqbn ofep4:esp32:ofep4:Renderer=ppaimage --jobs 4 Module/DisplayModule_P4_800x480
```

This uses LVGL's installed PPA backend for opaque, square, non-gradient fills and,
in the third mode, fully opaque unscaled/unrotated variable RGB565/RGB888 images
without masks, recoloring, tiling or custom blending. Text, rounded shapes,
gradients, transparent images and other unsupported draw tasks stay in the
original software renderer. **This is not a 100% hardware renderer.** Do not remove
the software renderer or change UI styles to make an operation eligible. No UI
style, pixel clock, radio setting, rotation,
framebuffer count or refresh cadence changes. A single software draw unit and
`LV_USE_OS=LV_OS_NONE` remain in use; PPA operations are blocking.

All main draw-buffer allocations use 128-byte alignment and whole aligned sizes.
LVGL's layer buffers use its 128-byte aligned allocation path. Stride stays
unpadded, as required by the installed PPA fill driver's physical-width handling.
The 128-byte cache SDK is required. No globally installed LVGL source is edited.
This trial uses the installed LVGL 9.6.0 backend. Its address-based cache query
is bridged on IDF 5.5 to `esp_cache_get_alignment`, using the actual memory
capability (internal SRAM or PSRAM) rather than assuming all buffers share the
external-cache line size. Native tests cover both memory types, uncached memory,
unknown addresses, query errors, API forwarding and the microsecond-timer wrap.

The ordinary `perf` command and completed benchmark print renderer counters; the
image mode additionally reports actual hardware blend calls:

```text
P4 RENDERER: PPA-fill (blocking) | fills=... errors=0 max=... us | draw alignment=128 B
P4 RENDERER: PPA-fill+image (blocking) | fills=... errors=0 max=... us | draw alignment=128 B
P4 PPA IMAGE: blends=... errors=0 max=... us; unsupported tasks use software
```

`fills` and `blends` count calls to the IDF hardware APIs since boot, not requested
UI changes. Errors count failed calls separately. The wrappers forward the
original arguments and results unchanged. `blends=0` is a valid result if the
tested UI uses only unsupported images, masks, fonts or transformed icons; it is
not proof of a broken build. Record the full benchmark
and this line; compare to a build with **Software** selected. Check colors,
artifacts, Home updates, touch, internal heap, BLE pairing and WiFi/RS485 OTA.
If `errors` increases, the display stalls or artifacts appear, revert to the
Software option and retain the diagnostic log. Hardware/FPS results are not
inferred from a successful build. The owner's repeated fill-only comparison was
approximately 14.0 FPS vs. 14.8 FPS for software, with 1,934 fills and no reported
PPA errors. It did not establish a performance gain. The extended image mode
needs a new on-device comparison; its FPS and pixel output are not yet verified.

PPA hardware can accelerate fills, blending and image transformations; it does not
guarantee a particular FPS increase for a text-heavy UI. See
[Espressif's PPA documentation](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32p4/api-reference/peripherals/ppa.html).
The LVGL backend remains experimental; see the
[LVGL PPA integration documentation](https://lvgl.io/docs/open/9.4/details/integration/chip_vendors/espressif/hardware_accelerator_ppa).
The current 16 MHz RGB timing scans at approximately 37.8 Hz; increasing PCLK
alone cannot fix slow rendering. Do not change panel timing as part of SDK
installation or trade the artifact-free baseline for an unverified FPS target.

The supported choices are documented in Espressif's
[IDF 5.5.5 P4 cache configuration](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/kconfig-reference.html#config-cache-l2-cache-size).
Sketch `-D` options cannot reconfigure a precompiled SDK.

## Separate Build Workspace

Use Linux/WSL2 or the isolated native Windows workflow below, with the prerequisites of the official
[Arduino library builder](https://github.com/espressif/esp32-arduino-lib-builder/tree/6671d0b).
Keep its multi-GB sources/output **outside the OFE project and Arduino installation**,
for example `$HOME/ofe-sdk-build/p4-3.3.12`. Do not use the old 3.2.0 high-performance
package, an S3 package or the post-V3 P4 package.

In a new dedicated directory, obtain these exact source revisions (the prefixes
are recorded by the installed SDK's `versions.txt`; the tool checks each HEAD):

| Source repository | Revision | Path relative to builder |
| --- | --- | --- |
| `espressif/esp32-arduino-lib-builder` | `6671d0b` | `.` |
| `espressif/esp-idf` | `b774170ff46` | `esp-idf` |
| `espressif/arduino-esp32` | `e7c1e8cd4` | `components/arduino` |
| `hathach/tinyusb` | `88c7f0b81` | `components/arduino_tinyusb/tinyusb` |

Clone/check out these revisions in the separate workspace, initialize their exact
submodules, install the matching IDF tools and source `esp-idf/export.sh` there.
Apply only the pinned builder's TinyUSB patches (if not already present).
Never run an unpinned update against a working Arduino installation.

Managed components must also be resolved at the versions recorded in this
folder's `versions.txt`, including ESP-Hosted 2.12.13 and WiFi Remote 1.6.4.
Freeze the resulting `dependencies.lock` and preserve it with the SDK backup.
**Source commits alone do not lock managed dependencies.** The upstream builder
does not ship that lock at this revision. An arbitrary fresh dependency resolution
is not assumed to match: `check-sources` parses the lock with the IDF Python
environment's `ruamel.yaml` and rejects differing or missing required versions
before the build. Output validation checks the recorded versions again.

## Prepare, Build, Validate

`<OFE>` below means the OFE project path, `<BUILDER>` the separate checkout and
`<STOCK>` a read-only path to the complete installed `esp32p4_es-libs/3.3.12` SDK
(from WSL, normally under `/mnt/c/Users/User/AppData/Local/Arduino15/...`).

```sh
python3 <OFE>/tools/boards/p4_high_perf_sdk/prepare.py prepare --builder <BUILDER> --baseline-sdk <STOCK>
python3 <OFE>/tools/boards/p4_high_perf_sdk/prepare.py check-sources --builder <BUILDER>
```

Preparation refuses wrong revisions, unrelated tracked builder edits, changes to
its owned fragment, project-local builds and Arduino installation paths. It adds
the feature to **only** `esp32p4_es`; other target definitions are preserved.
Repeated preparation is idempotent. Nothing is downloaded, compiled or installed.

With the pinned source/component setup ready and the IDF environment active:

```sh
cd <BUILDER>
bash build.sh -s -t esp32p4_es
```

The official builder rebuilds the libraries, headers, linker scripts, bootloaders
and memory variants into its own `out/` directory. `-s` skips the upstream source
update/install stage, so it does not move the pinned sources to newer branches.
Do not use `-c` or `-d` (copy/deploy) and do not copy individual archives into the
stock SDK.

```sh
python3 <OFE>/tools/boards/p4_high_perf_sdk/prepare.py check-sdk --baseline-sdk <STOCK> --sdk <BUILDER>/out/tools/esp32-arduino-libs/esp32p4_es
```

The builder puts `versions.txt` in the parent `esp32-arduino-libs` directory.
Copy that **build-produced** file beside the candidate's `sdkconfig` before this
check; do not substitute this folder's reference file for build provenance.
Checks cover component versions, PRE-V3, unchanged CPU/PSRAM clocks, only allowed
config differences, SDK file inventory, regenerated QIO/DIO cache headers and
changed library archives. Extra config differences require review; do not widen
the allowlist simply to obtain a pass. These are structural checks, **not** proof
of ABI correctness or physical-board operation.

## Isolated Arduino Test, Not Global Installation

Once the complete candidate passes validation, put it in its own SDK folder on
Windows (not Arduino15). For Arduino-ESP32 3.3.12, `compiler.sdk.path` selects its
headers, archives, linker scripts and bootloader together. A separate build path
prevents reuse of standard-SDK objects:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,ChipVariant=prev3,USBMode=hwcdc,CDCOnBoot=cdc --build-property "compiler.sdk.path=C:\OFE-SDKs\p4-es-3.3.12-l2-256-128" --build-path C:\OFE-Builds\p4-highperf-prev3 --jobs 2 Module/DisplayModule_P4_800x480
python tools/tests/check_p4_ecc_link.py C:\OFE-Builds\p4-highperf-prev3
```

This command compiles only; it does not upload. Keep both the standard and
candidate SDK and their full firmware/bootloader outputs. Do not assume an
application-only OTA switches every bootloader-level cache setting. Review the
matching bootloader and first test through USB with a recoverable standard build.
On Windows, use backslashes in `compiler.sdk.path`: Arduino's `cmd /c COPY`
prebuild step rejects forward-slash paths even though GCC and esptool accept them.
The example enables hardware USB CDC/JTAG and CDC on boot for native USB logs.

Compare startup L2 diagnostics, the identical display benchmark, scrolling,
Home updates, internal heap/largest block, BLE pair/cancel/re-pair, WiFi OTA and
RS485 with the same UI and radio workload. Do not replace the stock SDK or publish
the candidate until these tests pass. Omitting `compiler.sdk.path` and using a
fresh build path returns compilation to the installed standard SDK.

Back up sources/revisions, `dependencies.lock`, the final sdkconfig, `versions.txt`,
the **entire** generated SDK and its checksums, not just this two-line fragment.

## Native Windows Build

An existing IDF 6.0 installation does not need to be removed or changed. This
candidate reproduces the Arduino 3.3.12 **IDF 5.5.5/GCC 14.2** SDK, not an IDF 6.0
upgrade. Use a separate short workspace without spaces, such as
`C:/esp/ofe-p4-3.3.12`. Do not activate the IDF 6.0 PowerShell profile for this build.

`bootstrap.py fetch --workspace <WORKSPACE>` obtains the four exact source
revisions and their recursive submodules. It preserves nonempty/unrelated
workspaces and records resolved commits in `source-pins.json`. Create a dedicated
Python environment at `<WORKSPACE>/python`, then use that environment with the
pinned IDF's `tools/idf_tools.py install-python-env --features core`. Set
`IDF_PATH=<WORKSPACE>/builder/esp-idf`, `IDF_TOOLS_PATH=<WORKSPACE>/tools` and
`IDF_PYTHON_ENV_PATH=<WORKSPACE>/python` for these provisioning commands only.
Install `cmake` using the same `idf_tools.py`; its native Windows version is
3.30.2. Existing Ninja 1.12.1 under `C:/Espressif/tools` and Arduino's GCC 14.2
under `esp-rv32/2601` are reused read-only. `--host-tools` can select another
Ninja tools root. No globally installed compiler, Python environment or SDK is
replaced.

With that dedicated Python environment:

```powershell
python <OFE>/tools/boards/p4_high_perf_sdk/bootstrap.py pin-components --workspace <WORKSPACE>
python <OFE>/tools/boards/p4_high_perf_sdk/windows_build.py host-tools --workspace <WORKSPACE>
python <OFE>/tools/boards/p4_high_perf_sdk/prepare.py prepare --builder <WORKSPACE>/builder --baseline-sdk <STOCK>
python <OFE>/tools/boards/p4_high_perf_sdk/windows_build.py configure --workspace <WORKSPACE>
python <OFE>/tools/boards/p4_high_perf_sdk/windows_build.py build --workspace <WORKSPACE>
python <OFE>/tools/boards/p4_high_perf_sdk/windows_build.py finish-sdk --workspace <WORKSPACE>
python <OFE>/tools/boards/p4_high_perf_sdk/prepare.py check-sdk --baseline-sdk <STOCK> --sdk <WORKSPACE>/builder/out/tools/esp32-arduino-libs/esp32p4_es
```

`pin-components` adds exact managed-component download constraints with
`require: no`; it does not add dependencies to the app's linker requirements.
`host-tools` verifies the official jq executable against its SHA-256 and creates
a local `python3` shim. Native builds use two compiler jobs. `finish-sdk` uses a
derived copy of the pinned official Bash exporter: command-metadata reading is
adapted for Windows JSON/drive paths, and header copying is batched with the same
suffix, no-overwrite and transitive-relative-include rules. The original tracked exporter
and IDF/Arduino source files remain unchanged; TinyUSB uses the official baseline
patch applied by `pin-components`. The derived exporter also refreshes and exports
existing archive aliases (such as `libmbedtls_2.a`) on repeated runs; the upstream
retry branch otherwise links them without copying them into the new output.
The native helper parses memory-variant JSON itself to place the QIO flash archive;
native jq CRLF output can prevent the upstream Bash target comparison from matching.
Bootloaders and the DIO memory variant
are built in separate directories, and version records come from the actual
source checkouts/component manifests. The complete candidate is exported under
the workspace's `builder/out`, never into Arduino15.

If a later variant step is interrupted after the common export succeeded,
`finish-sdk --skip-export --workspace <WORKSPACE>` resumes without rerunning the
common exporter. It checks that the common output exists and its configuration
still matches the builder. After SDK configuration/source changes, run the full
build/export workflow instead.

Neither a successful SDK build nor the Windows helper is a physical-board test.
The isolated full Arduino build/link and board tests above remain mandatory.

For this machine, the separate candidate path is
`C:/esp/ofe-p4-3.3.12/builder/out/tools/esp32-arduino-libs/esp32p4_es`.
Select it only through the isolated compile command's `compiler.sdk.path`
override. Do not copy it over the SDK in Arduino15. The full native build workspace
is not required for everyday compilation once the complete candidate is backed up;
keep its source pins, dependency lock and configuration with that backup.
