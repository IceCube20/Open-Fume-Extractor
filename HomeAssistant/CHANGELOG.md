# Changelog

## 0.1.31RC (0.1.31rc4) - 2026-11-15

- Measure overview card width with ResizeObserver for explicit one-to-four-column
  limits, including generated dashboards, sidebar changes and initially hidden
  views. Do not depend on CSS container queries for the overview grid.
- Apply column-only configuration changes without rebuilding native controls.
- Preserve phone layouts and narrow Sections/Masonry containers without overflow;
  overview cards cannot expand their containing HA section automatically.

## 0.1.31rc3 (Development Candidate)

- Add the MCU Network diagnostic sensor with matching MQTT captions and icons:
  Ethernet, WiFi/WLAN, Access Point or Disconnected/Getrennt.
- Use the active MCU network interface in entity attributes and dashboard badges
  instead of always showing WiFi, including Ethernet-to-WiFi failover.
- The native sensor uses the existing API; MQTT requires MCU firmware 2.1.8.

## 0.1.31rc2 (Development Candidate)

- Use MCU in visible integration and dashboard labels. MCU-S3/MCU-P4 identities
  supplied by the firmware take precedence over the generic MCU fallback.
- Keep entity IDs, device identifiers, API keys and dashboard configuration types
  unchanged, preserving existing automations and cards.

## 0.1.31RC (0.1.31rc1)

- Schedule configured subsecond polling using precise start-to-start deadlines
  instead of the coordinator's whole-second alignment. The selected 0.5-second
  interval no longer produces uneven catch-up bursts or drifts by response time.
- Keep HA's serialized refresh/error/unload handling. Skip missed slots for slow
  requests instead of overlapping HTTP reads or sending a backlog to the master.
- Preserve existing intervals, entity identities, cleanup, state projections and
  card appearance. No master firmware update or extra endpoint is required.
- Add virtual-clock tests for output transitions, 0.5/0.75-second timing, slow
  responses, in-flight requests, polling disablement, shutdown and auth failures.

## 0.1.30RC (0.1.30rc1)

- Default the automatic dashboard to a maximum of four columns, reducing to
  three, two or one according to the available card width, including HA's sidebar.
- Add a one-to-four-column limit to the dashboard and overview card editors.
  Existing overview cards without a column preference keep their automatic grid.
- Adapt compact headers and summaries to each individual card's width so names, icons and
  OFE/EVT indicators stay aligned in narrow desktop columns. Controls and live
  updates are unchanged; resizing does not recreate cards or interrupt focus.

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
