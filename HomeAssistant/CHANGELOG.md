# Changelog

## 0.1.0rc25

- Automatically register the bundled dashboard module in Lovelace storage mode.
- Update its versioned resource URL during integration setup after upgrades;
  preserve an existing resource ID and remove only duplicates of the same OFE URL.
- Leave YAML-managed resources, other cards and dashboard layouts untouched.
- Resource-registration errors do not prevent entity setup. Restart HA after
  installation/update and fully reload the browser/app to use the new JavaScript.

## 0.1.0rc24

- Localize automatic display backlight modes in German and English.
- Replace the numeric delay with a 1-60 minute dropdown, matching MQTT and the master.
- Keep unsupported backlight entities hidden on older display firmware.
- Document two-touch wake: first light, then leave the screensaver after release.


## 0.1.0rc23

- Add per-display automatic backlight shutdown mode, a 1-60 minute delay and
  a read-only backlight status, matching MQTT names and icons.
- Require Master 2.0.54 and updated display firmware for the new controls;
  older display firmware keeps its existing entities without unsupported controls.
- Preserve existing entity IDs, polling, screensaver and normal brightness behavior.


## 0.1.0rc22

- Publish the native integration and bundled dashboard cards in their own
  HACS-compatible repository.
- Add HACS/manual installation and migration instructions, CI validation and
  versioned release ZIPs.
- Point integration documentation and issue reporting to this repository.
- Keep the `open_fume_extractor` domain, device identities, entity unique IDs
  and polling behavior unchanged from 0.1.0rc21. No additional master/module
  flash is needed when already running Master 2.0.53.

## 0.1.0rc21

- Remove JBC USB station-specific entities after two confirmed reports of no
  connected station from an online module. Preserve module diagnostics and
  connection/Work/Stand indicators; restore supported entities on reconnect.
- Remove obsolete native devices already deleted from the master inventory.
- Prevent old station counters being resurrected from raw cached data.
