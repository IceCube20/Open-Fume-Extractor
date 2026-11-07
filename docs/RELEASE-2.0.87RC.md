# Open Fume Extractor 2.0.87 RC

Community Master **2.0.87RC**, JBC FAE Bus **1.1.70RC** and native Home Assistant
integration **0.1.0rc27**. The public master excludes developer mode and private
developer credentials. Signing private keys and build caches are not published.

## Changes Since 2.0.75 RC

- Faster display home-card I/O streaming: prioritize current input/output masks
  while rotating larger module metrics and descriptor-based values fairly.
- Display firmware-update cancellation offers a Back action and returns to Home
  after ten seconds. Normal firmware-update error screens remain distinguishable.
- WiFi reconnect recovery on the master preserves the setup portal during
  background connection attempts and reconnects after an access-point restart.
- WiFi OTA diagnostics distinguish ACK/session/request/offset problems, and
  confirmed bulk-capable displays can recover from a temporary legacy fallback.
- Improved S3 RGB OTA write handling and timing diagnostics. The optional serial
  RGB-clock probe is nonpersistent; normal startup remains at 16 MHz.
- Consistent backlight wake handling, compact numeric controls and large-display
  Home settings. P4 hostnames use the board's factory identity.
- JBC FAE identity uses the reconstructed F1 model branch:
  `02:F2:8886791:0023324:F1_DIS_02`, 31 ASCII payload bytes.
  Hardware ID and writable device ID are separate. Existing stored IDs survive;
  only new modules without an ID generate and store a new text ID.
  ID writes acknowledge success under command 0x1F only after persistence.
- Local trace CSV follows the chosen transaction/raw layout. Transaction rows
  include decoded results with stable headings; malformed/unpaired frames are
  retained. JSON includes decoded events and transaction metadata.
- Trace filenames identify the project, bus, module type, address and timestamp.
- Master module cards use inset header accents and uniform pixel-aligned system
  separators. The module table uses the same icons/colors as the cards.
- Native Home Assistant polling is configurable from **0.5 to 60 seconds**.
  The existing default and stored preferences remain unchanged.
- HACS hides the development branch from normal downloads. Existing branch
  installations must select a numbered release once; RC releases remain opt-in
  pre-releases. This does not alter saved HACS channel preferences.

## Updating

Use signed normal `.bin` files for OTA. Use `*-merged.bin` files only for an
initial USB installation at address 0x0. Select the exact module/display hardware
in the [firmware index](FIRMWARE.md) or the
[online flasher](https://icecube20.github.io/Open-Fume-Extractor/).

The developer firmware used during testing is not the public master download.
OFE module firmware versions and JBC-facing emulated firmware strings are
different identities. The F1 string does not limit how many stations the master
can combine for one extractor and does not establish complete DME compatibility.

Home Assistant: update the custom integration through its dedicated HACS
repository or use the ZIP, then restart HA and refresh the browser/app.
UI-managed Lovelace resources update automatically; YAML-managed resources keep
their documented manual configuration.

This remains a release candidate. Keep a known-working firmware and verify bus,
WiFi, touch, pairing, screensaver and OTA behavior on your hardware.
