# Separate LAN and WLAN IPv4 settings

The P4 master configuration page now has independent WLAN and LAN / Ethernet
IPv4 sections. Each interface supports DHCP or a static IP with its own gateway,
subnet mask, primary DNS and secondary DNS.

## Compatibility

- Existing WLAN preferences and form names remain unchanged.
- LAN defaults to DHCP. WLAN addresses are never copied to LAN automatically.
- LAN is saved as a separate versioned, checksummed 24-byte NVS record with
  readback verification. Unchanged settings do not write flash again.
- Older backups without LAN settings preserve the current LAN configuration.
- New P4 backups include optional `eth_*` fields. S3 masters ignore these fields
  and continue to show WLAN settings only.
- The P4 can configure LAN from its setup access point without a saved WLAN SSID.
- Equal static LAN/WLAN addresses and invalid host/mask/gateway/DNS combinations
  are rejected before configuration writes. This does not detect IP conflicts
  with other physical devices.

## Network behavior

Ethernet priority, WLAN standby and WLAN recovery from 2.0.97 are unchanged.
Ethernet readiness now requires both a live cable link and an assigned address;
a configured static IP alone must not disable the fallback WLAN.
The connection status shows separate current LAN and WLAN addresses, and the
post-save redirect uses the newly configured static LAN address when applicable.
When moving to DHCP, the hostname redirect is attempted first; a router may
assign a different address, so the old address is only a best-effort fallback.

Use different unused addresses for the P4's LAN and WLAN. An S3 master running
in parallel also needs its own address. This change does not alter router DHCP
reservations or detect external address collisions.

## Verification

Native tests cover static settings, checksum corruption, invalid records, failed
writes, write deduplication, link detection and all five SDK Ethernet fields.
Browser tests render the actual C++ form helper with the production JavaScript
in German and English at 320, 390, 768 and 1440 pixels, including independent
DHCP/static switching and form payloads. Firmware needs a hardware test after
flashing; host tests cannot prove PHY, DHCP or radio operation.

Verified local snapshot: `2.0.98RC-P4-Preview`, stock Arduino-ESP32 3.3.12,
PRE-V3. Full build and ECC link verification passed. App BIN: 3,255,456 bytes
within the 3,342,336-byte OTA slot. 310 Python regressions, native settings and
network/lifecycle tests, runtime tests, S3 compiler checks with both MQTT worker
modes and browser checks passed. Nothing was flashed or pushed.

App BIN SHA-256:
`d62ba4fa78bfa6784fc5aab9b90df11d96a735c417ee6125d04ebec8d9e98b20`
