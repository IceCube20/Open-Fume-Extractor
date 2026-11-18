# MCU 2.1.22: Shared S3/P4 Form Appearance

The recent file-field and action-row styles were embedded in the P4 USB UI
script, so S3 pages did not receive them. They now live in `WebFormStyles.h`,
included by the existing typography macro in both the streamed status page and
the shared page shell. The USB script retains only USB-specific styles.

S3 receives full-width 44 px file fields, consistent Browse controls and hover/
disabled states without loading any USB scripts or adding unsupported controls.
The previously shared refresh-free header and grouped CSV/JSON/diagnostic exports
are retained. USB storage, hosted C6 and Ethernet functions remain P4-only.

The dedicated S3 browser regression uses the production C++ style macro and
export renderer, without the USB script, across mobile/desktop widths and DE/EN.
`tools/build_master_s3.ps1` reuses the exported S3 board profile and enforces the
actual 0x330000-byte OTA slot. It builds/exports only; it never flashes a device.

Verification: 366 Python regressions, the native render/policy checks, eleven
browser suites and traffic replay passed. Both private MCU builds/exported apps
are marked `2.1.22RC-Developer`: S3 2,899,088 bytes (chip 9), P4 3,557,232 bytes
(chip 18). Build/export hashes match; compiled UI headers match current sources.
Both apps fit their existing OTA slots. Neither device was flashed.
