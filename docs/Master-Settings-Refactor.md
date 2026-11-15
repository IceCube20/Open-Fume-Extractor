# Master Settings Refactor: Persistence Layer

Master 2.0.25RC-Developer moves control, main input/output and rule persistence
from inline header implementations into `src/MasterSettingsStore.cpp`.
The header retains key constants, value types, key builders and declarations.
It is not yet the sole owner of every persistent setting in the project.

## Behavior and Compatibility

- Existing namespaces, keys, scalar types, legacy migration and v1 blob layouts
  remain unchanged. No NVS erase or new partition layout is required.
- Continuous remains runtime-only. It is absent from the control blob and backup
  export, and loads as false. The existing 3-second debounce / 30-second maximum
  control persistence delay is unchanged.
- Blob writes are read back and checked byte-for-byte. Identical blobs are not
  rewritten. The same verified/no-op writers are used for network, MQTT,
  certificate, initial setup, authentication reset and legacy input-route saves.
- Empty strings are valid persisted values. Arduino Preferences returns their
  length (zero), so their success is confirmed by presence and exact readback,
  rather than treating zero as a generic write failure.
- Main input validation is shared by the scheduler and storage. IN3 through IN8
  remain supported, and universal entity IDs are not treated as GPIO bit numbers.
- LED and eco saves are owned by the store. Live settings are only applied after
  both writes succeed; a failed pair attempts to restore its previous values.
  Brightness remains 10–100%, idle timeout 1–1440 minutes.
- The existing backup validation, staged logic saves and restore undo journal
  remain in place. Normal boot does not import a backup.

## Ownership and Limits

The store does not call MQTT, WiFi, the OFE bus, the command queue or the
scheduler. Existing handle ownership and task locks remain the caller's
responsibility. Readback is used on configuration saves, not live telemetry
polling. Module aliases continue to be sent to and stored by the modules.

Single control/input/rule records remain individual NVS blobs. LED/eco pairs and
the ordinary network form are not power-loss-atomic multi-key transactions.
Pair rollback is best effort if the storage device keeps failing. Whole-backup
restore uses the pre-existing undo journal; this refactor does not claim to
make arbitrary NVS writes atomic.

Logic storage, module snapshots, pairing keys and some language/metadata saves
still have their existing specialized persistence owners. A future phase can
encapsulate them separately without changing their formats or runtime timing.

## Verification

`tools/tests/settings_store_native/run.ps1` exercises the production store with
fault-injected Preferences. Python integration tests check restore recovery,
delegation and fail-closed web behavior. Build the ESP32 master before flashing.
On hardware, change controls and routes, reboot, toggle MQTT off/on, save blank
optional MQTT credentials, change LED/eco settings and export/import a backup.
Continuous must return to off after restart.
