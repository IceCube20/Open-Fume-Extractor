# MCU 2.1.14RC: Bounded USB File Streaming

The former 6 MiB USB transfer cap is removed. Transfers no longer allocate a
file-sized buffer. The USB worker owns file handles and reads/writes chunks
of at most 8 KiB; its two reusable buffers occupy 16 KiB of PSRAM. An HTTP
download uses one additional 8 KiB buffer and writes directly to the browser
download, without assembling a whole-file JavaScript Blob for ordinary files.
Firmware/backup selection still hands a browser File to the existing validators;
this does not bypass image, size, hash, chip or configuration checks.

The current signed 32-bit ESP VFS file-offset API sets the technical maximum
to 2,147,483,647 bytes per file. This is not a PSRAM-sized allocation. Available
stick capacity, filesystem and network/browser constraints still apply.

Uploads use exclusive temporary files and are published only after a complete
transfer, successful flush and close. Existing destinations are not replaced.
Tokens, directory generations, chunk bounds, sequence/duplicate checks and
idle leases are retained. A failed/abandoned upload is cleaned up by the USB
worker. If a stick is physically unplugged, cleanup cannot write to that absent
medium; an interrupted `.tmp` file may remain there for removal on a PC.
Do not unplug a stick while writing; use Eject after completion.

Filesystem operations stay on the USB worker. HTTP downloads run on the existing
HTTP-service task, not the main control loop; the single HTTP server handles
other page requests after the download response completes.

Ethernet LEDs are positioned inside the RJ45 symbol, to the left and right of
the latch recess. Activity still uses real, sampled TX/RX history; no artificial
periodic blink was added. Private builds retain `RC-Developer`.
