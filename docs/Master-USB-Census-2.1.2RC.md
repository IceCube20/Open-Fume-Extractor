# USB Census Correction, Master 2.1.2RC-P4-Preview

Follow-up: [2.1.3 fixes the missing SDK enumeration-filter callback](Master-USB-Enumeration-2.1.3RC.md).
The observations and verification below describe the historical 2.1.2 build.

Observed on the owner's P4 with a stick inserted: host/observer/MSC driver
ready, root link present, logical port powered, device count 1, no device rows
and no recognized storage. The cause of the storage failure is not yet proven.

Two SDK semantics were missing from the 2.1.1 diagnosis:

- `usb_host_lib_info().num_devices` comes from the allocated USB device-object
  pool. The object is created before enumeration completes. A value of 1
  does **not** establish completed enumeration or successful FAT access.
- IDF's `usbh_event_callback()` passes hub events to its internal hub driver
  without broadcasting NEW/GONE events to application clients. An application
  observer driven only by those callbacks can omit an enumerated hub.

Primary sources, matching the installed IDF 5.5.5:
[host event dispatch](https://github.com/espressif/esp-idf/blob/v5.5.5/components/usb/usb_host.c),
[device allocation and configured-address filtering](https://github.com/espressif/esp-idf/blob/v5.5.5/components/usb/usbh.c).

## Correction

The observer now reads `usb_host_device_addr_list_fill()` once per second.
This SDK API lists devices with enumeration unlocked, configured state and
nonzero addresses. Up to eight entries are described using cached SDK
descriptors, including hubs. A temporary handle is always closed before
finishing each descriptor read; hub removal must not rely on a GONE callback.
No interfaces are claimed, no transfers submitted, no GPIOs/clocks changed
and no port-power or filesystem writes are performed.

Network/System now distinguishes:

- **USB objects:** the SDK allocation count (previously labeled USB devices).
- **Configured:** the count from the configured-address list.
- **Device rows:** bounded VID/PID/class details, with explicit read errors.
  The numbers are USB bus addresses, not the physical connector numbers.

The JSON adds `allocated`, `configured`, `census_ms` and per-device `error`.
The old `enumerated` key remains as a legacy alias for the raw object count;
it must not be interpreted as enumeration completion. A failed census keeps
the previous rows and census timestamp, with an observer error code.

## Next Hardware Check

After updating, reload Network/System and reconnect the stick. Send the full
USB section, including both counts and any device rows:

1. Objects 1, configured 0: an object exists, but the SDK has not exposed a
   fully configured device. A serial SDK/`[USB]` log is the next useful input.
2. Configured 1, row is a hub: the hub is visible; no configured storage behind
   it has been identified yet. This is not a FAT32 diagnosis.
3. Storage row, another protocol: the stick does not match the SCSI/BOT filter.
4. SCSI/BOT row but no storage acceptance: inspect MSC event/initialization
   logs. Do not infer a formatting failure merely from driver readiness.

The 2.1.0/2.1.1 16 MiB P4 partition layout is unchanged. No device is flashed
automatically; the separate GitHub publication checkout remains untouched.

## Software Verification

- Full PRE-V3 Arduino 3.3.12 / IDF 5.5.5 build passed. App BIN is 3,463,696
  bytes, within the 5,242,880-byte OTA slot; its marker is
  `2.1.2RC-P4-Preview`, target `MASTER`.
- The partition BIN is identical to 2.1.1. Final ELF/BIN ECC verification passed.
- 336 Python regression tests passed, as did P4 SDK compiler checks and USB
  browser tests in DE/EN at 320/390/1440 widths.
- Native tests execute the actual census functions against bounded SDK
  adapters: allocated but unconfigured device, hub without any NEW/GONE
  callback, SCSI/BOT metadata, list/open/descriptor failures, immediate handle
  closure, stale snapshot retention and bounded details. These are not real
  USB electrical/driver execution tests.

Local artifact set:
`MasterP4/OpenFumeExtractorMaster_P4/build/2.1.2-prev3-usb-census/`

App SHA-256:
`74bdb3a63274dee68a2bbac6cf1ef3bd6b7c32cc1a1234bd4af1b132565d6170`

Not flashed or published. Hardware feedback from this census is still required.
