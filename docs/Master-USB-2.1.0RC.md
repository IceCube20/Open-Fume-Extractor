# Master 2.1.0: USB Storage and Partition Migration

Prepared for the Waveshare ESP32-P4-Module-DEV-KIT PRE-V3 with 16 MB flash
and 32 MB PSRAM. Firmware numbering rolls over to **2.1.0**, not 2.0.100.
S3 master partition offsets and sizes are unchanged.

## USB Stick

The carrier has four Type-A sockets, not four simultaneously equivalent ports.
According to the [Waveshare USB FAQ](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT/FAQ),
the **HOST** jumper position selects direct **USB port 1**; the **DEVICE**
position selects the CH334 path for **ports 2-4**. Use the manufacturer's
[numbered port diagram](https://docs.waveshare.com/assets/images/ESP32-P4-USB-6848eed08e2e1a2477a910ccc822e6c4.webp)
to identify port 1. Power the board off before moving jumpers. Start diagnosis
with HOST and port 1; do not assume a stick in another socket is connected.
No formatting is needed for this routing check.

The installed Arduino 3.3.12 P4 SDK already enables
`CONFIG_USB_HOST_HUBS_SUPPORTED` and `CONFIG_USB_HOST_HUB_MULTI_LEVEL`.
No sketch-only define is needed to enable those precompiled SDK options.
The CH334 path is not yet physically validated with OFE, and the four sockets
alone do not prove a firmware or FAT32 problem.

The P4 high-speed OTG host uses
Espressif's pinned `usb_host_msc` 1.3.0 driver. No global SDK modification,
USB device-mode mass storage, or custom SCSI implementation is needed.
References: [board](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT),
[Espressif host API](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/usb_host.html),
[driver](https://github.com/espressif/esp-usb/tree/7d9431146f9078ebe4f0ac94e9a69e88cee64deb/host/class/msc/usb_host_msc).

- One FAT/FAT32 volume; no exFAT/NTFS or USB hubs promised. The master never
  formats a stick. Unsupported media reports a mount error.
- The header shows the Material Design `usb-flash-drive` icon when detected:
  green when mounted, yellow if detected but not mounted, absent when removed.
  It shares the existing 5-second header status poll; no high-rate USB poll.
- Network/System lists files and supports config backup to `/OFE`, downloading
  files, loading an existing backup into the normal restore form, and eject.
  Backups contain credentials: confirm deliberately and protect the stick.
- Updates can load a `.bin` from the stick into the existing master/module
  update form. Select the module separately, review the recognized firmware,
  then press the normal update button. Existing chip/type/signature checks
  remain active. Nothing installs automatically on insertion or selection.
- The browser transfers the selected local file through the existing update
  endpoint. Internet access is not needed; this is not unattended boot-time
  flashing directly from the stick.
- Bus Diagnostics can save the existing decoded CSV/JSON trace export and
  diagnostic snapshot JSON to `/OFE`. This is not continuous serial-console
  capture or an unlimited rotating log service.
- Root and `/OFE` files are listed, at most 32, max 6 MiB per file, safe ASCII
  names up to 95 characters. Supported extensions: BIN, JSON, CSV, LOG, TXT,
  NDJSON, OFEPROFILE. Oversize/unsupported names are not listed.
- A dedicated worker handles filesystem I/O. Bounded file buffers live only
  in PSRAM; they expire after inactivity. Selection uses a generation counter;
  stale selections and invalid offsets are rejected. Duplicate chunks must
  match previously buffered bytes. Only complete files can be committed.
- Writes use a new exclusive `.tmp` file, flush/close and rename. Existing
  files are not overwritten. Use Eject before removing a stick. Sudden power
  loss or unplugging while writing can still damage a FAT volume; temporary
  files may remain and are never mistaken for a valid firmware file.

USB mounting, removal during read/write, drive compatibility, VBUS current,
serial/USB coexistence and sustained control load need physical board tests.
Software tests do not establish electrical or power-loss safety.

## P4 Layouts

| Region | 16 MiB board | 32 MiB candidate |
| --- | --- | --- |
| OTA data | 0xE000, 8 KiB | same |
| App 0 | 0x10000, 5 MiB | same |
| NVS | 0x670000, 256 KiB | same |
| App 1 | 0x6B0000, 5 MiB | same |
| LittleFS | 0xBB0000, 4.25 MiB | 0xBB0000, 20.25 MiB |
| Coredump | 0xFF0000, 64 KiB | 0x1FF0000, 64 KiB |

The gap after App 0 deliberately preserves the historical NVS address.
PSRAM is runtime memory, not additional flash space or an OTA partition.
The 32 MiB table is not the default and must never be flashed to a 16 MiB
board. Confirm the new board's silicon revision before choosing its SDK.

## Migration From The Previous P4 Layout

**Do not install this layout by app-only OTA.** The previous app slots were
0x330000 bytes; partition tables are not replaced by ordinary OTA.
LittleFS moves from 0x6B0000 to 0xBB0000 and becomes smaller on the 16 MiB
board. Its old contents are not automatically migrated. On first boot the
normal filesystem initialization may format the new LittleFS region.

Before a USB upload:

1. Download the master config backup using Network/System. Export profiles,
   certificates and other needed files separately. Check that copies exist
   outside the device and protect files containing secrets.
2. For rollback, use esptool to read and preserve the complete 16 MiB flash,
   the existing NVS region and the old LittleFS region. These read-only ranges
   are 0x0/0x1000000, 0x670000/0x40000 and 0x6B0000/0x940000 respectively.
   Keep backups outside the firmware output directory.
3. Build with `tools/build_master_p4.ps1`; use the matching generated
   bootloader, partition table, boot-app/OTA-data and app set for USB upload.
   The upload must reset OTA selection to App 0. Do not erase all flash or use
   a merged image blindly if you intend to retain NVS settings.
4. Restore exported filesystem files/profiles into the new filesystem; import
   config only if required. Verify networking, pairing, aliases, rules, MQTT,
   HA and module controls before reconnecting extraction equipment.
5. Verify OTA app updates and abort recovery against the new 5 MiB slots.

No backup, erase or hardware flash is executed by the preparation tools.
The build helper checks the real 5 MiB limit; the stock IDE's permissive
Custom-menu maximum is not a substitute for that check.

## Verified Local Build

`2.1.0RC-P4-Preview`, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5, PRE-V3,
16 MB flash. Full linked build passed. App BIN: 3,457,056 bytes, below the
5,242,880-byte OTA slot. Static RAM report: 68,436 bytes; this is not a
measurement of runtime free heap after USB/network/task allocation.

App SHA-256:
`dc08557ef011ad968850b0788b473823b496c86bdfca0273a1972d5ece291fc9`

The complete local artifact set is in
`MasterP4/OpenFumeExtractorMaster_P4/build/2.1.0-prev3-usb/`, deliberately
separate from older builds. Its `flash_args` names the matching bootloader,
partition table, OTA-selection data and app. The merged image is NOT a
settings-preserving migration method.

Software verification passed:

- 333 Python regression tests, including both partition layouts and pinned
  MSC-source hashes. Espressif's partition generator accepted both layouts.
- Actual P4 SDK compiler checks for the USB backend and all four MSC C files.
- Native tests of the actual USB transfer functions, including allocation
  failure, generation/token/offset validation, duplicate content, incomplete
  commits, buffer ownership and disconnect cleanup.
- Browser tests for the USB icon, backup/file staging/trace save without
  automatic restore/install, DE/EN at 320/390/1440 widths.
- Shared S3 sketch/scheduler/display-tunnel compiler checks; native P4 BLE,
  C6, LAN/WLAN and LAN-activity tests; RS485/hardware/LAN browser suites.
- Final ELF/BIN proof that the existing software BLE ECC backport is linked
  and the old SDK ECC implementation is excluded.

Not flashed, not published to GitHub, and not physically USB-stick tested.
The separate publication checkout was left unchanged because its existing
uncommitted work needs an approved, reviewed reconciliation.

## Other 2.1.0 Changes

- Both master variants show chip model, revision, cores/clocks, flash,
  SDK-visible PSRAM and Arduino/IDF versions under Network/System.
  P4 C6 firmware and host compatibility versions are reported separately;
  a C6 silicon revision is not invented from its firmware version.
- LAN link/activity LEDs use identical pixel-snapped dimensions. Activity
  replays a rolling history sampled by the small LED endpoint, independent of
  slow bulk status/module updates. Continuous traffic may keep ACT lit;
  software sampling cannot reproduce every physical PHY LED pulse.
- Bus Diagnostics offers a passive 30/60/300-second RS485 quality window,
  using existing polls, sampled latency and error counters. No baud changes
  or extra traffic are injected. Missing samples, OTA/recovery and resets make
  a result inconclusive rather than a false pass.
- The owner's [auto-direction transceivers](https://www.roboter-bausatz.de/p/ttl-zu-rs485-modul-uart-3.3v-5v)
  are specified up to 256 kbaud, close to the normal 250 kbaud bus. A speed
  sweep above that limit is not enabled. UART counters alone cannot distinguish
  an electrical short from disconnection/power loss; no short-circuit test or
  claim of safe intentional shorting is included.
