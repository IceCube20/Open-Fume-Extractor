# MCU 2.1.9: Isolated MQTT Topics Per Serial Number

Previously both MCUs used the factory `open-fume-extractor` Base Topic for state,
module state, commands and availability. Separate HA discovery IDs could not
prevent cross-fed values or both MCUs accepting the same command.

The empty/factory Base Topic now becomes `open-fume-extractor/<16-digit-serial>`:

- S3 example: `open-fume-extractor/0000ECF7384AEC24/state`.
- P4 example: `open-fume-extractor/0000ACFFE1A0ED30/state`.
- Commands, availability and module topics all use the same isolated root.
- MQTT Client IDs also use the full factory serial, not a configurable hostname.
- Existing discovery topics, HA device identifiers and entity unique IDs stay
  unchanged. Rediscovery updates the referenced state/command topics in place.
- Loading old/default settings and restoring/saving a factory-root configuration
  resolve to the receiving MCU's own serial. The reserved factory pattern
  `open-fume-extractor/<16-hex-serial>` also rebinds when restoring another MCU's
  configuration. Explicit custom Base Topics stay unchanged; custom roots must
  still be unique per MCU.
- Cleanup does not delete state/status retained under the old shared factory
  root, since another old-firmware MCU may still be publishing there. Retained
  values under a different MCU's reserved serial root are protected too.

Recompile and update both MCUs, without erasing settings. On reconnect their
enabled HA discovery announces the new topic references. No HA integration or
display firmware update is required. Direct subscriptions and external consumers
of the old default Base Topic must use the new serial-specific path; there is no
shared command alias, because it would reintroduce cross-control.

No firmware flash or GitHub publication is performed by this source change.
