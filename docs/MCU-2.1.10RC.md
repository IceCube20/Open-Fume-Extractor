# MCU 2.1.10RC

## P4 USB file manager

The USB sections on Network/System, Updates and Bus Diagnostics now use a
responsive file list instead of a dropdown. Files in the USB root and OFE/
are identified separately. Search, type filters and name/size sorting are
browser-side and do not add USB scans or change the three-second status poll.

Selection follows the file path and size, not its list index. Removed/replaced
files and disconnected sticks clear the selection. Unchanged status polls
preserve focused rows and search inputs. Firmware and backup actions are only
shown for matching file extensions; existing content validation still applies.

Reads and writes show the current file and progress. Success and failure
messages survive ordinary status polls. Safe-eject confirmation waits for the
worker to finish unmounting. Host diagnostics remain developer-only.

The existing authenticated routes, CSRF wrapper, generation/token checks,
PSRAM staging, 6 MiB file limit and 32-entry listing limit are unchanged.
There is no recursive folder browsing, deletion, renaming or formatting.
Firmware selection does not start an update. Backup selection does not import
settings, and saving a backup still requires the credentials warning.

## Build labels

Both MCU-S3 and MCU-P4 use `RC-Developer` when compiled with
`OFE_DEVELOPER_MODE_ENABLE=1`, and `RC` otherwise. Community sources omit private
configuration files and keep the public RC label. The runtime developer-mode
toggle does not rename the running firmware.

## Checks

The USB browser tests use the actual embedded JavaScript and shared web CSS in
German and English at 320, 390 and 1440 pixels. They cover filters, sorting,
stable focus, contextual actions, transfer progress/failures, buffer release,
ejection and disconnected/empty listings. MCU identity tests preprocess the
real version macros for both chips with developer support enabled and disabled.
