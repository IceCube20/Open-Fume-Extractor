# MCU naming, portable module aliases and safe input defaults

Local candidate: MCU 2.1.5RC (P4 suffix RC-P4-Preview), HA 0.1.31rc2.
No GitHub publication or device flashing is performed by these changes.

## Visible Naming

Use MCU (Main Control Unit) in web, CLI, pairing, display and Home Assistant
labels. Model identities remain MCU-S3 and MCU-P4. Existing protocol target
MASTER, MQTT topics, API keys, NVS namespaces, HA unique IDs, firmware filenames
and custom card types are unchanged for compatibility.

## Module Aliases

An INFO name received from the physical module is authoritative. Derive its
alias from that name and mirror it into the local UID-keyed cache. A standard
module name means there is no user alias and removes an obsolete cached alias.
Only use the MCU cache before a valid INFO name has been read, including cached
offline modules. Repeated synchronization does not rewrite unchanged NVS values.

Current modules already persist their labels and include them in INFO; this does
not require a new command or automatically write controller labels into modules.
Only names actually stored on the module can follow it between MCUs. Registry
history merges must not resurrect a cleared label over a fresh INFO response.

## Main Input

A missing main-input setting defaults to None, both in the scheduler and in
legacy-key migration. Explicitly saved main inputs, including the old explicit
All JBC setting, remain unchanged. Do not erase NVS to install this update.
All JBC is offered when JBC sources exist; a previously selected unavailable
source remains a disabled selection until the user changes it. New display
startup state is also None; decoding of older status packets is unchanged.

## Displays

All four display alarm headers now show local MCU connection loss in red,
independent of old or empty remote critical-alarm bits. Ordinary warnings remain
yellow; reconnection restores the appropriate warning, critical or healthy state.
The screensaver and alarm list already classify connection loss as critical.

Display source versions: QSPI 1.4.36RC, ST7796 1.0.46RC, S3 RGB 1.4.52RC,
P4 1.0.15RC. Rendering, clocks, buffers and task priorities are unchanged.

## Local Verification

- 344 firmware regression tests and 185 HA unit tests passed.
- Native tests compile the actual alias/cache reconciliation, registry history
  merge and settings-store implementation, including failed NVS access.
- Native tests compile all four actual alarm-header functions: warning, connection
  loss, recovery, remote critical alarm and stale critical-mask cases passed.
- Current S3 MCU and all four display sketches passed installed-SDK compiler
  syntax checks. Display binaries and a full S3 MCU binary were not rebuilt.
- HA cards, routing selectors and the MCU status/network header passed local
  desktop/mobile browser checks. Internal CSS selectors are unchanged.
- Full PRE-V3 MCU-P4 build passed: 3,472,984 program bytes and 68,884 static RAM
  bytes. Final BIN contents and software BLE ECC link origins were verified.
- P4 partition binary is identical to 2.1.4; no partition migration is needed.

P4 app SHA256:
`93bde5dda2dc63b6130c28db17b9e05faebdf10ae889f3513e19762583f84bb4`

Local artifacts: `MasterP4/OpenFumeExtractorMaster_P4/build/2.1.5-prev3-mcu-labels/`
and `HomeAssistant/dist/open-fume-extractor-ha-0.1.31rc2.zip`.
Use the application `.ino.bin` for normal MCU-P4 OTA, not the merged flash image.
Hardware behavior after installing these candidates is not yet confirmed.
