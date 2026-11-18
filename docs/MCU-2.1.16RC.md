# MCU 2.1.16RC: USB Volume Names

The USB browser prefers the FAT filesystem volume name, such as `BACKUP`, over
the manufacturer's USB product string. Unnamed, unreadable or unsupported volumes
keep the existing USB-device fallback. Slots and connection-instance IDs still
identify drives, including two sticks with identical volume names.

The USB worker probes each newly registered stick with a temporary `/usb-label`
mount, using the SDK's second FatFS volume slot, then immediately unmounts it.
The active `/usb` filesystem and its file list are not replaced. No probe runs
while a transfer owns the worker. Names are cached per connection, cleared on
removal, and refreshed when the selected drive is mounted.

The locally documented `msc_host_vfs.c` extension calls FatFS `f_getlabel` with
the mount's actual drive identifier. It does not guess an index or parse raw
filesystem sectors. UTF-8 names are copied with bounds checks; no `f_setlabel`,
formatting, filesystem repair or write command is added.

Software tests use simulated USB/FatFS devices; actual label display still needs
verification on the board. The integration does not rename Windows drive letters.
