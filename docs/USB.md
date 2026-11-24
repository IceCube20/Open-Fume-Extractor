# USB Storage on MCU-P4

USB storage is provided on the supported MCU-P4 PRE-V3 and PoE V3 profiles.
MCU-S3 does not expose this USB file manager. Use the board's native host
connection; a USB flashing/serial connector is not automatically a host port.
On carriers with a HOST/DEVICE jumper, select HOST as described by that board.

## Connect and Select a Drive

1. Connect a supported USB mass-storage stick with a supported FAT filesystem;
   FAT32 is the tested starting point. Back up important data first.
2. Open **USB** in navigation or select the connected-stick icon in the header.
3. Wait for **Ready**, not just a detected USB device.
4. Select the correct drive by label, capacity and device information before
   making changes. Logical drive numbers are not physical hub-port numbers.

A compatible hub can keep up to four mass-storage devices connected. Only one
filesystem is selected/mounted for file operations at a time. Removing the
selected stick does not silently redirect an operation to another one. The
fifth device is not registered. Power the hub appropriately; software support
does not increase the board's available USB current.

## Browse and Manage Files

Expand or collapse the directory tree, select a folder and use search/filter
controls to find the file. Refresh after making changes from another computer.
The manager supports creating folders, uploading/downloading files, renaming,
moving and deleting. Rename/Move works on both files and directories.

For a move or rename, select the item, open **Move / Rename**, choose the
destination directory and edit its name. A directory cannot move into itself.
Existing destination files/directories are not overwritten. Folder deletion
requires an empty folder; there is no recursive-delete or recycle-bin feature.
Cross-drive copying/moving is not implemented. Use a computer for bulk operations
or to transfer between drives.

The bounded listing holds up to 128 files and 64 directories, through six
directory levels. A truncated list does not mean the remaining files have been
deleted. Operations that cannot safely validate an incomplete directory tree
are rejected. Keep filenames below 96 bytes and full paths below 384 bytes;
names beginning with a dot/space or containing traversal components are refused.

## Save OFE Data to a Folder

On an export-capable page, select **Save to USB**, choose the drive/folder and
edit the proposed filename. Confirm the destination and wait for completion.
The destination is not restricted to a fixed OFE folder.

Supported contexts include configuration backups, Universal/Modbus profile
exports, bus trace CSV/JSON, diagnostic snapshots and RS485 quality reports.
A backup contains credentials and certificates: protect the stick accordingly.
Keep separate backups before overwriting an installation's configuration.

## Install or Import from USB

- **MCU/Module Updates:** choose the target, select **Install from USB** and
  inspect the detected firmware target/version. Start the update separately.
- **C6:** the same file picker feeds the C6 verifier. Exact image, size/hash,
  Ethernet, recovery and confirmation requirements remain in force.
- **Configuration backup:** select **Load from USB**, inspect the selected file
  and explicitly start restore/restart.
- **Universal/Modbus profile:** load the file into the editor, validate it and
  explicitly transfer/commit it to the module.

The picker filters relevant file extensions, but an extension alone never
establishes validity. Selecting a file does not bypass a signature, chip,
board/revision, profile or backup check and never starts flashing by itself.

## Size Limits and Safe Removal

The old 6 MiB ordinary-file transfer cap is removed. Transfers stream in bounded
8 KiB chunks rather than allocating a whole-file PSRAM buffer. The current
signed 32-bit VFS offset limit is 2,147,483,647 bytes per file; available space,
filesystem and browser/network limits still apply. Firmware and backup imports
retain their own much smaller validation/staging limits.

Wait until the operation reports completion, then use **Eject** before unplugging.
Do not change drives, remove power or unplug during writes. There is no format
function. Interrupted uploads can leave temporary files that must be checked
on a computer after reconnecting. Do not treat a USB stick as the only backup.

## Detection Problems

In developer mode, Network/System exposes USB diagnostics. Distinguish:

- no root link: connection, host-port selection or power problem;
- USB object but not configured: enumeration/hub/driver problem;
- configured mass storage but not Ready: filesystem/mount problem.

Collect the USB serial log before changing anything. Try a direct connection,
another port and a known-good backed-up FAT32 stick before reformatting. A
device counter alone is not proof that a filesystem is mounted. If Windows
cannot read a stick, test another PC port and inspect the drive before writing
more data; do not immediately format it.
