# MCU 2.1.12RC: USB Folder Actions and Exports

- USB folders can be renamed or moved with their contents. Root, self/subtree
  moves, collisions, stale inventories and excessive path/depth are rejected.
  Incomplete inventories cannot be used for directory moves. No recursive
  delete or cross-volume copy was added; deletion still requires an empty folder.
- Backup, trace and diagnostic exports now open a filename/destination dialog.
  The current folder is preselected. Cancel does not write anything. Uploads
  retain the same destination selection and 6 MiB transfer limit.
- The update-page USB selector includes ESP32-C6 when its update panel exists.
  It hands the file to the existing C6 staging/size/hash/SDK checks; LAN,
  recovery confirmation and an explicit installation command remain required.
- Web Ethernet LEDs are equal-sized and positioned below the port. Real
  TX/RX history gates an 80 ms visual activity cycle during traffic. Reporting
  and polling rates are unchanged; this is a delayed visualization, not exact
  PHY LED timing. Reduced-motion settings disable the visual cycle.

Private MCU builds retain the `RC-Developer` suffix. Community builds use `RC`.
No partition migration is needed for this app update. Software tests use a
simulated filesystem and network; physical USB/media removal and C6 update
behavior must also be checked on the board. No device is flashed automatically.
