# USB Recognition Diagnostics, Master 2.1.1RC-P4-Preview

**Diagnostic limitation discovered after the owner's hardware test:** IDF hides
hub NEW/GONE events from application clients, and the library's device count
also includes devices still being enumerated. The event-only list below can
therefore be empty while its count is 1. See the corrected
[2.1.2 census](Master-USB-Census-2.1.2RC.md). The 2.1.1 software tests used
simulated hub events and did not establish real hub visibility.

The owner reports that a FAT32 stick is still not recognized on the
Waveshare ESP32-P4-Module-DEV-KIT after checking the port/jumper selection.
The hardware cause is **not yet established**. This build adds observation
and makes startup ordering deterministic; it is not a confirmed USB fix.

## Changes

- Register the MSC class client and a passive descriptor observer before
  starting the host event pump. This removes the registration/enumeration
  timing window for a stick present at boot.
- Select the P4 high-speed controller explicitly (`peripheral_map = 1`),
  equivalent to the SDK's previous default. No USB clocks, PHY voltages,
  GPIOs, port-power switching or filesystem settings are changed.
- The new observer opens only cached descriptors and never claims an
  interface or submits a transfer. It records at most eight devices and
  releases its handles on disconnect.
- Network/System's USB area now shows host/observer/MSC-driver readiness,
  root link, logical port enable, enumerated-device count and VID/PID/class.
  Hubs and SCSI/BOT storage interfaces are shown separately. A hub alone
  does not cause a misleading USB-stick header icon.
- `/usb/status` includes the same bounded diagnostics object. Serial logs
  prefixed `[USB]` show startup, descriptor classification, MSC acceptance,
  device initialization and FAT mount result codes.

## Reading The Result

1. **Host not ready or nonzero host/event error:** capture the error code
   and `[USB]` startup lines. A task/driver initialization failure is not a
   FAT32-format diagnosis.
2. **Host ready, root link absent, zero USB devices:** no device connection
   has been detected at the selected controller. Port routing, VBUS supply,
   connector/stick contact and PHY behavior still require investigation.
   The software cannot identify which of these is responsible.
3. **Root link present, zero enumerated devices:** a connection is observed,
   but enumeration has not completed. Capture the SDK/`[USB]` serial output.
4. **Only a hub listed:** the hub enumerates; the stick behind it has not
   reached the observer. Check the selected downstream socket and capture
   hub/host errors rather than reformatting the stick blindly.
5. **Storage with another protocol:** the stick is enumerated but does not
   match the bundled MSC driver's SCSI/BOT interface filter.
6. **SCSI/BOT device listed but not mounted:** capture `[USB] MSC install`
   and `[USB] FAT mount`; these distinguish storage initialization from the
   FAT-mount step. `msc_ready` describes the class driver, not mounted media.

`root_powered` is the controller's logical PRTPWR bit, **not a measurement of
5 V at the USB connector**. `root_connected` is the root-port state; with a
hub it describes the hub's upstream connection, not every downstream port.
The numbers in the enumerated-device list are USB bus addresses, **not**
the physical socket numbers printed in Waveshare's jumper diagram.

After installing the diagnostic firmware, reconnect the stick, wait a few
seconds, then send a screenshot of the whole USB section. A serial log is
additionally useful, but is not required to see the main recognition stages.
No stick formatting or filesystem writes are performed by this diagnosis.

## Firmware Layout

The 2.1.0 P4 16 MiB partition layout is unchanged: two 5 MiB app slots,
historical NVS address, relocated LittleFS. An app update is appropriate
only if that table is already installed. For older tables, follow
[the backup and migration guide](Master-USB-2.1.0RC.md).

The separate GitHub publication checkout is not modified by this preparation.

## Verified Build

- Full Arduino 3.3.12 / IDF 5.5.5 PRE-V3 build passed: app BIN 3,462,864 bytes,
  below the unchanged 5,242,880-byte slot. Static RAM report is 68,644 bytes,
  not runtime free heap.
- The compiled firmware marker is `2.1.1RC-P4-Preview`, target `MASTER`.
- The generated partition BIN is byte-identical to the 2.1.0 artifact.
- 335 Python regression tests passed. Native USB transfer/observer-table
  tests, actual P4 SDK checks and S3 compiler checks passed.
- USB browser tests passed at 320/390/1440 widths in DE/EN, distinguishing
  no device, hub only and SCSI/BOT storage. Their USB devices are simulated.
- Final ELF/BIN verification passed for the pinned software BLE ECC backport.

Local artifacts:
`MasterP4/OpenFumeExtractorMaster_P4/build/2.1.1-prev3-usb-diagnostics/`

App SHA-256:
`af7144501692303b554637891c880d84f395d73c3bc618345c66f5a612c01440`

No device was flashed. USB recognition on the owner's actual board is still
awaiting this diagnostic build and its hardware results.
