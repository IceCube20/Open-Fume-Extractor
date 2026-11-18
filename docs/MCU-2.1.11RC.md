# MCU 2.1.11RC

## P4 USB folders and file actions

The file manager shows the actual USB directory structure. Folders can be
expanded/collapsed individually or together; search opens matching branches.
File selection follows full path and size rather than a volatile array index.

Supported explicit actions:

- Upload a local file into a selected directory, including empty files.
- Create a folder, move/rename a file, delete a file, delete an empty folder.
- Download files and stage compatible firmware/backups in the existing forms.
- Retain the existing backup and trace exports to `/OFE`.

Deletion requires confirmation. There is no recursive folder deletion,
formatting, implicit overwrite, automatic firmware installation or automatic
backup import. Ordinary data files can be stored without being executed or
interpreted by the MCU. Hidden/system entries and incomplete `.tmp` files are
not listed. Windows-invalid/reserved names, malformed UTF-8 and unsafe path
components are rejected.

## Bounds and ownership

Inventory: 128 files, 64 directories including root, six directory levels,
383-byte relative paths and 95-byte UTF-8 leaf names. Scanning is breadth-first,
uses one open directory at a time, and stops after 2048 entries or ten seconds.
Incomplete inventories are marked. The directory/file tables remain in PSRAM.
The MCU never recursively scans from the HTTP handler.

Buffered uploads/downloads remain limited to 6 MiB. Larger files may be listed,
moved or deleted without loading their content into RAM. All file actions run
in the exclusive USB worker and require the current inventory generation;
clients submit indices, never arbitrary filesystem paths. HTTP authentication,
CSRF, update exclusion and transfer token checks remain active.

Management replies include an operation ID. Completion and result are published
together after the filesystem operation and inventory refresh. Mount/refresh
also keep the worker busy, preventing concurrent commands during enumeration.
The same-volume rename checks the destination first and cannot replace an
existing file. Folder deletion checks every entry, including hidden files,
before calling the filesystem's empty-directory removal.

## Verification

Native tests execute the production queue, scanner and file actions against a
simulated FAT filesystem. They cover nested/empty folders, collisions, stale
generations/metadata, I/O errors, unplugging, limits and directory-handle and
mutex ownership. Browser tests execute the embedded JavaScript with production
CSS in German/English at 320, 390 and 1440 pixels, including the confirmation
and target-folder dialogs. These tests do not replace physical USB validation.

Private MCU builds retain `RC-Developer`; community builds retain `RC`.
