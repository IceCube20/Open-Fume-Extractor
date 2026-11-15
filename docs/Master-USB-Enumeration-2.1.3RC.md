# USB Enumeration Filter, Master 2.1.3RC-P4-Preview

## Confirmed Software Defect

The owner's 2.1.2 logs repeatedly showed disconnect/reconnect transitions,
an enabled HS root port and one allocated device, but no configured device.
No MSC acceptance or filesystem mount was reached.

The installed Arduino 3.3.12 PRE-V3 SDK has
`CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=1`. Our zero-initialized
`usb_host_config_t` left `enum_filter_cb` null. In IDF 5.5.5,
`select_active_configuration()` starts with `enum_proceed=false`; only the
callback can grant permission. A missing callback requests cancellation.
That path logs a warning, which is omitted by this SDK's error-only logging.

Source: [Espressif IDF 5.5.5 enumeration implementation](https://github.com/espressif/esp-idf/blob/v5.5.5/components/usb/enum.c).

2.1.3 explicitly registers an allow callback when the SDK filter is enabled.
It preserves the SDK's selected configuration and allows both hubs and other
devices to finish enumeration. The MSC driver still selects supported
SCSI/Bulk-Only storage interfaces; accepting enumeration is not mounting or
granting filesystem access to every USB device.

A single `[USB] Enumerate VID=... PID=... class=... config=...: allow` line
records this stage. No PHY, GPIO, voltage, clock or task-priority change is
included. The 2.1.0/2.1.1/2.1.2 16 MiB partition table is unchanged.

## Developer-Only Web Diagnostics

The normal USB status, file selection, backups, trace exports, firmware
selection and eject commands remain visible without developer mode.
Technical host/observer/MSC flags, raw counts and VID/PID rows are shown only
when the master reports developer mode enabled. `/usb/status` omits the
`diagnostics` object outside that mode; it is not merely CSS-hidden.
The UI starts with diagnostic elements hidden and clears them on the next
status refresh after developer mode is disabled. Serial troubleshooting logs
remain available independently of the web developer mode.

## Verification Scope

- Native regression tests execute the production callback and host
  initialization with the SDK filter enabled and disabled. They reproduce
  the previous default rejection and check hub/storage acceptance, repeated
  attachment, unchanged configuration choice and unchanged host settings.
- 338 Python regression tests passed.
- Browser tests passed in German and English at 320, 390 and 1440 pixels,
  including developer-mode transitions and unchanged USB file workflows.
- The initial callback correction passed syntax compilation against the
  installed Arduino 3.3.12 / IDF 5.5.5 SDK headers.

Native SDK adapters and simulated browser responses do not establish real
USB enumeration, hub compatibility or FAT mounting. A hardware test of
2.1.3 is still required. Nothing was flashed or published automatically.

## Final Build

The full PRE-V3 Arduino 3.3.12 build passed: program size 3,463,772 bytes
within the 5,242,880-byte slot, app BIN size 3,464,032 bytes, static RAM
68,628 bytes. The firmware marker is `MASTER / 2.1.3RC-P4-Preview`.
Final ELF/BIN ECC link verification and generator consistency checks passed.
The partition BIN is byte-identical to 2.1.2.

Local artifact set:
`MasterP4/OpenFumeExtractorMaster_P4/build/2.1.3-prev3-usb-enumeration/`

App SHA-256:
`b74c7568345a304a1ab99ff5dfe707c769df4e8508c3eb24ef52c19630fabb38`
