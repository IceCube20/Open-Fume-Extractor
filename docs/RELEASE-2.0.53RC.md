# Open Fume Extractor 2.0.53 RC

Community release with Master **2.0.53RC** and the native Home Assistant
integration **0.1.0rc21**. Developer mode and private developer credentials are
not included in the published master. Firmware-signing private keys remain local.

## Changes since 2.0.51 RC

- Native Home Assistant removes obsolete entities and devices already removed
  from the master's module list, including registry entries left from earlier
  integration versions. Temporarily offline modules remain registered.
- Native JBC USB entities no longer reappear from obsolete raw counter caches.
  Counters are checked against the current station family and port count.
- When an online JBC USB module explicitly reports no connected station twice,
  its station-specific sensors, counters and controls are removed in MQTT and
  the native integration. Module diagnostics, connection, Work and Stand remain.
  Supported station entities return on reconnect with their existing identities.
- MQTT repairs historical retained JBC discovery entries even if they are
  missing from the master's saved inventory. Long counter topic names are no
  longer truncated. Cleanup is bounded and failed deletes are retried.
- Failed polls, offline modules and incomplete discovery do not authorize
  destructive inventory reconciliation.

## Install or update

Only the **master** and **native Home Assistant integration** need updating for
these fixes. All module firmware versions are unchanged from 2.0.51 RC.

Install the attached `open-fume-extractor-ha-0.1.0rc21.zip` by replacing
`custom_components/open_fume_extractor` and restarting Home Assistant. Do not
delete and recreate the integration. Native cleanup follows two confirmed polls;
the polling interval remains configurable from 1 to 60 seconds.

For MQTT, enable **Home Assistant Discovery** under **Network/System > MQTT**
and save. The usual discovery prefix is `homeassistant`. MQTT live values alone
do not enable device discovery or entity removal. Allow two discovery checks
(normally about 15-30 seconds) plus the incremental retained-topic cleanup.

All eleven targets have individual signed `.bin` OTA packages and complete
`*-merged.bin` USB images attached to this release. Never upload a merged image
through OTA. Use it only for an initial USB flash at address `0x0`. The
[online USB flasher](https://icecube20.github.io/Open-Fume-Extractor/) uses the
same merged images. Select the exact display hardware target.

See [Firmware Files](FIRMWARE.md) for versions and checksums and
[the integration guide](../HomeAssistant/README.md) for installation details.

## Verification

The native integration passed 172 tests, including MQTT/native catalogue
comparisons. The master passed 108 source regression tests and native tests of
the production MQTT cleanup functions, including station disconnect/reconnect,
historical orphan entries, retries and full-length topics. Community firmware,
OTA signatures and merged-image contents are checked before publication.

Native cleanup has been confirmed on the test installation. This remains a
community release candidate, not a guarantee for every station or installation.
