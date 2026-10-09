# Shared Entity Presentation Tests

`EntityPresentation.cpp` is compiled directly, without Arduino stubs or a copied
implementation. It has no Arduino, transport, NVS or heap-allocation dependency.

Run `run.ps1`, then `node ../test_entity_presentation.js` from this directory.
The JavaScript test consumes fixtures produced by the native production core and
executes the actual web renderer and live control-update functions.

Coverage includes bounded descriptor parsing, DE/EN label fallback, explicit
access modes, unknown metadata keys, long option lists, raw binary/UTF-8 values,
UTF-8-safe wire truncation, boolean readback, select indices and command values,
already transformed profile text, duration overflow, live value updates and
preservation of dirty UI fields. A host benchmark covers a 64-entity descriptor;
it is not an ESP32 timing measurement.
