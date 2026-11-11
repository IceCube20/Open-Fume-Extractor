# Open Fume Extractor 2.0.91 RC

Community Master **2.0.91RC** and native Home Assistant integration **0.1.29RC**
(HA/HACS version **0.1.29rc1**). The public master excludes developer mode and
private developer credentials. Signing private keys and build caches are not
published.

## Changes Since 2.0.87 RC

- Routing and the Boolean designer share the same complete signal catalogue,
  module aliases, profile entity names and supported Fan/IO channels. Existing
  saved signal IDs remain compatible. Descriptor caches are checked against the
  current profile CRC before reuse.
- Additional system inputs expose power saving, alarms, warnings, errors,
  filter warnings/change/validity, module connectivity, local-device connectivity
  and JBC Stand. Eight runtime markers can link routing and logic definitions.
- Additional system outputs inhibit power saving, wake selected/all supported
  displays or keep their backlights on. Display holds expire after a lost link;
  UI changes remain owned by the display GUI task.
- Main output selected and main output active are distinct: selection remains
  true while idle; activity requires extraction to be enabled through that
  assigned module, including after-run. This is control state, not RPM feedback.
- JBC Stand evaluation distinguishes FAE-bus and USB modules. Filter validity
  is separate from explicit filter warning/fault bits; missing or stale data
  must not be interpreted as a healthy filter.
- Native HA includes an optional automatic community dashboard. Master/module
  cards follow the live integration inventory without extra polling. Multiple
  masters have separate tabs, offline modules remain visible and removed modules
  disappear after inventory reconciliation. Existing dashboards are untouched.

## Display Versions

| Target | Version |
| --- | --- |
| Display 320x480 (QSPI) | 1.4.35RC |
| Display 800x480 (S3 RGB) | 1.4.51RC |
| Display ST7796 320x480 | 1.0.45RC |
| Display P4 800x480 (PRE-V3) | 1.0.14RC |

Update displays to these versions to use the new wake/hold automation outputs.
Older displays retain their normal operation; unsupported automation controls
are not offered. The main-output state fix requires only the master update.

## Updating

Use signed normal `.bin` files for OTA. Use `*-merged.bin` files only for an
initial USB installation at address 0x0. Select the exact hardware in the
[firmware index](FIRMWARE.md) or
[online flasher](https://icecube20.github.io/Open-Fume-Extractor/).

Update HA from the dedicated integration repository or the ZIP, restart HA and
fully reload the browser/app. UI-managed dashboard resources update
automatically; YAML-managed resources remain user-managed. Create the optional
dashboard once via Settings > Dashboards > Add dashboard > Community dashboards
> Open Fume Extractor. Enable pre-releases in HACS when selecting this RC.

See [System automation](System-Automation.md) for signal semantics and examples.
This remains a release candidate. Keep a known-working firmware and verify bus,
WiFi, touch, pairing, screensaver and OTA behaviour on your hardware.
