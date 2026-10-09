# Master Settings Store Native Tests

Run `./run.ps1` from PowerShell. The harness compiles the actual master
`MasterSettingsStore.cpp` using MSVC, with a typed in-memory Preferences backend.
Build artifacts are written to the system temporary directory.

Coverage includes existing v1 control blobs and legacy-key migration, checksum
and structural rejection, main input normalization including IN8 and universal
entity IDs, routing key compatibility, output selection, rule round trips,
empty strings, certificate preservation, and LED/eco bounds. Failure injection
tests failed opens, rejected writes, writes reported successful but not stored,
readback failures, and LED/eco pair rollback. Repeated identical saves and changes
only to Continuous must not issue any writes.

The tests do not emulate ESP32 power loss, flash wear, NVS page compaction or
FreeRTOS scheduling. LED/eco rollback is best effort, not a multi-key atomic NVS
transaction. The existing backup restore undo journal remains responsible for
whole-restore recovery. Hardware restart and backup/restore tests are still
required after flashing.
