# MCU 2.1.19: Ethernet Socket Color and LED Placement

The status socket uses `var(--muted)`, the same color as its Network label,
including when linked. Header transport-state colors are unchanged.

The 8 px LED windows move inward by 3 px each. Their centers are now at
11/37 px, midway between the 48 px socket's outer walls (4/44 px) and latch
edges (18/30 px). The centers sit at y=36 px, halfway between the black opening
at y=30 px and the outside bottom at y=42 px. The JavaScript positions the
windows by their centers and actual pixel-rounded size. Equal sizing and actual-traffic
activity replay remain unchanged.

The standalone USB page receives the shared panel border, background and padding.
The modal file picker retains its existing outer dialog, without an extra inner
panel frame.

The network browser regression checks label/socket color equality and centered
LED positions across desktop/mobile viewports and fractional pixel densities.

Verified locally: 364 Python regressions, USB picker/frame checks, network browser
checks and traffic-replay tests passed. The final PRE-V3 P4 app image is
3,552,640 bytes and marked `2.1.19RC-Developer`; exported/build hashes match,
and compiled UI headers match the current source after Arduino line directives.
No device was flashed.
