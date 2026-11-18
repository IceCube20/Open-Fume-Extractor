# MCU 2.1.20: File Field Width and Ethernet Contrast

- File fields use their full container width on every viewport. The USB source
  button is on the next line, separated by 12 px. On desktop it is left aligned;
  on mobile it spans the same width as the field. Field/button heights stay 44 px.
- The status socket is a slightly darker gray (`#7f8995`) to emphasize the LEDs.
  Their approved corner-centered position, sizing and activity logic are unchanged.
- The header transport colors and the dedicated USB panel/modal framing remain
  unchanged.

Browser checks assert full-width fields and vertically separated buttons on all
tested viewports, plus the darker socket color and unchanged LED geometry.

Verification: 364 Python regressions and the picker/network/replay browser checks
passed. The P4 app is 3,552,656 bytes, marked `2.1.20RC-Developer`; exported/build
hashes match and the compiled UI headers match current sources. Not flashed.
