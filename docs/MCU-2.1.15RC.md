# MCU 2.1.15RC: Multiple USB Sticks

Up to four USB mass-storage devices can remain connected through a compatible
hub. The USB browser selects one drive at a time. Only that filesystem is
mounted at `/usb`; other registered devices remain connected. This avoids
changing the SDK's two-volume FatFS configuration and keeps the existing
single-worker, bounded-buffer transfer model.

The first detected drive is selected once at startup. Removing or ejecting it
does not silently switch file actions to another stick. Ejected drives can be
selected again to remount. Removing an inactive drive does not abort an active
transfer. A fifth drive is not registered and the UI reports the limit.

Drive slots are logical identifiers, not physical hub-port numbers. Product,
capacity and serial information help distinguish devices. Connection-instance
IDs change on reinsertion; request generation and drive-instance checks prevent
stale paths or identical filenames from targeting a different stick. Drive
switching is blocked while a transfer or file operation owns the active volume.

Uploads, downloads, exports and firmware/backup selection use the selected
drive. Cross-drive copying and moving are not implemented. Existing no-overwrite,
temporary-file, token, path and streaming protections remain in place.

Hub compatibility, power capacity and physical removal behavior still need board
testing. Use a suitably powered hub for several devices; this change does not
control or increase the board's USB power supply. No media is formatted.
