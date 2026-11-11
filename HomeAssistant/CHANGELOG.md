# Changelog

## 0.1.29RC (0.1.29rc1)

- Add an optional Open Fume Extractor community dashboard strategy. Create the
  dashboard once; master and module cards then follow the live integration inventory.
- Separate multiple masters into individual tabs and allow selecting one master.
- Keep registered offline modules visible and remove cards for deleted inventory.
- Sort module cards by numeric bus address, with the master first. Existing card
  configurations, controls and dashboards are preserved; no extra polling is added.

## 0.1.28RC (0.1.28rc1)

- Refresh the master, module and overview cards with the master's module accent
  colors, compact summaries, clearer connection badges and consistent spacing.
- Keep native Home Assistant controls, entity IDs, card configuration and focus
  during live updates. OFE/EVT effects and reduced-motion support remain intact.
- Show a green check for an empty alarm list and distinguish unavailable alarm
  data from a healthy state. Alarm entries keep their original text.
- Use the display name 0.1.28RC and the comparable HA/HACS version 0.1.28rc1.

## 0.1.0rc27

- Hide the `main` development branch from the HACS download selector so normal
  installations use numbered releases.
- Document the one-time switch from branch/commit tracking to an RC release.
  Existing HACS channel selections are not changed automatically; RC releases
  still require opting into pre-releases where applicable.
- Keep device identities, integration settings and dashboard layouts unchanged.

## 0.1.0rc26

- Allow native status polling from 0.5 to 60 seconds, preserving fractional intervals.
- Keep the existing 5-second default and saved polling preferences.

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
