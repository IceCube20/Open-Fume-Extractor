# MCU 2.1.18: Web Controls and Ethernet Port

- USB file inputs and their USB buttons share a 44 px row, with 8 px gaps and
  no extra left/top button margins. At mobile widths they stack at equal width
  with 12 px vertical spacing. Consecutive action groups also have 12 px gaps.
- Download/export pairs use consistent 40 px buttons and explicit group gaps.
  Native file-picker controls have matching typography, height and padding.
- The manual Refresh button is removed from the status header. The existing
  automatic refresh and internal forced reloads after commands are unchanged.
- The status and header Ethernet ports use the Material Design Ethernet socket
  glyph instead of the previous Lucide outline, oriented latch-side down. Equal-size
  green link and yellow activity windows sit at the outer latch-side edges.
  Their positions remain device-pixel aligned; traffic replay is unchanged.

Browser regressions cover file/USB alignment, equal export-button heights,
mobile stacking, refresh-button removal and port/LED alignment across viewport
sizes and device pixel ratios. Hardware update/USB functionality is unchanged.
