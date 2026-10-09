# Master runtime regression tests

Run `./run.ps1` from this directory on Windows with the bundled Python runtime,
MSVC 2022 and Windows SDK installed. Generated fixtures and binaries are placed
in the temporary directory, not in the firmware tree.

The fixtures extract the actual production functions. The tests cover:

- 50,000 differential ticks against the previous evaluator, including all
  blocks, feedback, duplicate links, four input ports and full graph limits.
- Input sampling once per tick without changing the eight-pass evaluation order.
- 100,000 clock phase cases, short/asymmetric timings and arithmetic overflow.
- Mutex allocation failure, lock contention and `millis()` wraparound.
- Round-robin absent-station probes, scheduler jitter, offline/OTA exclusions,
  connected stations and wraparound.
- Separate OFE transport and local-device connectivity for JBC, Weller,
  Universal/Modbus and display modules.

The host benchmark compares only the evaluator. It is not an ESP32 CPU-load
measurement, and does not include actual bus I/O or FreeRTOS scheduling.

Also run `node ../test_diagnostics_devices.js` for diagnostics JavaScript syntax,
German/English rendering and live device-state transitions.
