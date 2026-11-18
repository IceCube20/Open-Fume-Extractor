# MCU 2.1.13RC: Ethernet Activity Without Synthetic Flicker

The web Ethernet activity LED now follows only the captured TX/RX history.
The extra 80 ms CSS blink cycle from 2.1.12 is removed. Link remains steady,
both LEDs remain below the port, and no extra status polling was introduced.
The web history is delayed and sampled, not an exact PHY LED reproduction.

The USB transfer cap remains 6 MiB (6,291,456 bytes). Uploads and downloads
currently allocate a complete file buffer in PSRAM. Listing, renaming,
moving and deleting files do not have this transfer-size restriction.
A larger-file transfer design should use bounded streaming buffers instead
of simply increasing the full-file allocation limit.

Private firmware retains `RC-Developer`; community firmware uses `RC`.
