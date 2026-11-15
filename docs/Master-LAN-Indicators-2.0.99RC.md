# Master LAN indicators

The Master System network cell no longer repeats the IP address. The existing
IP cell remains authoritative. Ethernet now shows a Lucide ethernet-port icon
with a green link LED, an amber activity LED, negotiated speed and WiFi-off
state. WiFi mode retains the RSSI meter and SSID.

Link comes from `ETH.linkUp()`, independently of DHCP readiness. Activity comes
from matching ESP-NETIF TX/RX packet events, not an unconditional animation.
The browser pulses when the sampled packet sequence changes; it is a sampled
activity indication, not a wire-timing replica of the physical PHY LEDs.

Only authenticated `/state?lan_activity=1` requests renew the 8-second monitoring
lease. Ordinary HA/API polling does not enable or prolong packet reporting.
The HTTP service disables reporting when the lease expires or LAN disconnects.
The event callback only increments an atomic counter, without packet copies,
logging or UI work. Only one event-handler registration is retained. The
installed P4 SDK already enables `CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC`; no SDK
or global Arduino library edits are necessary.

Unsupported SDKs expose link/speed without invented activity. S3 builds use an
empty monitor. Browser request failure clears the visual LEDs; counter rollover,
unavailable monitoring and interface switching do not create a false pulse.

Tests cover filtering other interfaces and invalid events, TX and RX, missing
links/netifs, SDK failure/backoff, lease expiry, millis rollover, S3/no-events
builds and DE/EN responsive browser states at 320/390/768/1440 pixels.

C6 updating is a separate feature and is not enabled by this firmware change.
See [C6 update design](Hosted-C6-Update-Plan.md).

## Verified local build

`2.0.99RC-P4-Preview`, Arduino-ESP32 3.3.12 PRE-V3 stock SDK: full build,
ECC link verification, 312 Python regressions, native activity/lifecycle/runtime
tests, S3 compiler checks with both MQTT worker modes and browser checks passed.
The final ELF contains the real SDK TX/RX event enable/disable APIs and the
monitor callback. App BIN: 3,259,552 bytes within the 3,342,336-byte OTA slot.
No device was flashed; physical LED behavior and traffic load need testing.

App BIN SHA-256:
`0a4569a2ff38eaab0e855098d22fe6751a32397dcc020f703dbdfb82c928912e`
