# MCU 2.1.17: USB Page and Contextual File Picker

The P4 has a dedicated `/usb` page, reachable from the USB navigation tab or
the connected-stick header icon. It contains the full file manager, including
drive selection, folder tree, uploads, downloads, rename/move, delete and eject.
The large inline browsers on Network/System, Updates and Bus Diagnostics are
removed. The S3 does not receive USB controls.

## File Actions

- MCU, module and C6 firmware inputs have an **Install from USB** button.
  The modal picker stages the selected file in the existing input and triggers
  its usual validation. It never starts installation automatically. Signature,
  chip/type checks, C6 transport/recovery requirements and explicit confirmation
  remain unchanged.
- Configuration backup import supports **Load from USB**; backup export offers
  **Save to USB**, including the existing credentials warning.
- Universal/Modbus profile import and all three profile exporters (expert,
  Protocol Builder and Register Map) support USB. Import stages the profile in
  the form, not directly in the module.
- Trace CSV/JSON, diagnostic snapshots and RS485 quality reports can be saved
  through the same destination picker.

The save dialog selects a folder and editable filename. Cancel/Escape is inert
before a transfer; closing is blocked during the bounded active transfer. The
dialog restores focus to its caller. Files are extension-filtered for imports.
Late reads from a removed/replaced stick cannot populate the target input.
Existing drive-instance/generation guards, fixed-size streaming buffers,
no-overwrite policy and authenticated/CSRF-protected endpoints remain intact.

## Verification

`tools/tests/test_usb_picker_browser.cjs` exercises the new flow at 320, 390 and
1440 pixels in German/English, including cancellation, save destinations,
dynamic profile controls, replacement during reading and no automatic flash.
Existing USB manager, streaming, drive-isolation, backup/C6 validation and
network browser suites remain applicable. Browser device responses are mocked;
physical stick and firmware installation still require a device test.

Verified locally: 364 Python regressions, the native USB suite and ten browser
suites passed. The PRE-V3 P4 build/export succeeded (3,551,392-byte app image,
chip ID 18, marker `2.1.17RC-Developer`). Build and exported app hashes match.

No device is flashed and no GitHub publication is performed by this change.
