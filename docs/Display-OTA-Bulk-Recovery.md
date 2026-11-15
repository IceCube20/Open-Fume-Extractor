# Display OTA Bulk Recovery

Local Master firmware: **2.0.76RC-Developer**. Not published or flashed by this change.

## Observed Transfer

The reported S3 RGB update ran at 3.9 kB/s with a nearly full producer queue,
zero starvation, ACK times of 94/3204 ms and pump gaps of 96/3216 ms.
These counters do not identify the source of the long ACK delay. They do show
that the producer queue was not empty at that sample and that the longest pump
gap closely followed the longest ACK wait.

The former `WLAN-Chunk: 188 B` label is a configured OFE-frame limit, not proof
that the whole update used that path. The new developer-only suffix distinguishes
`OTA Bulk 1024 B` from `OTA OFE 188 B` and counts bulk fallback events.
The limit describes the selected packet format; a final partial packet can be smaller.

## Recovery Policy

Previously, any failed bulk block permanently disabled bulk for that update.
Now a display that has already returned a valid, authenticated bulk ACK in the
current update session can be retried after a two-second cooldown. Consecutive
failed recovery attempts back off to at most 30 seconds; a successful block
resets the cooldown. A display that has never confirmed bulk still gets only
the existing initial probe and then remains on the compatible OFE path.

Transport, device identity, session checks, offset validation, ACK timeout and
the three-attempt per-block bound are unchanged. On failure, the complete popped
block is sent through the existing idempotent OFE fallback at the same offsets.
No bytes are skipped merely because a request might have reached the display.
Session begin/end clear recovery state, including across address reuse.

## Scope And Verification

Only Master code changes are needed for this recovery. Display firmware, panel
timings, the SDK and flash erase/write behavior are unchanged. The cause of slow
RS485 updates is **not established or fixed** by this WLAN change.

Native tests execute the production routing and recovery methods, covering old
firmware, successful recovery, repeated failure backoff, session reset, device
identity and millisecond rollover. Existing display OTA tests retain duplicate
chunk, completeness, abort, failure and reboot coverage. Hardware throughput
must still be measured after updating the Master.

## Master ACK Diagnostics: 2.0.77RC-Developer

The S3 1.4.44RC log measured a maximum transport poll gap of about 64 ms,
bulk validation of 594 us, processing of 60.6 ms and local ACK enqueue of
1.417 ms. It reported no local ACK-send failures or bulk rejects, but repeated
requests were received. These measurements do not prove where the reply was
lost, delayed or rejected. A UniFi screenshot also showed strong recent RSSI
and 0% reported TX retries; these are not an end-to-end UDP delivery guarantee.

Master 2.0.77 adds developer-only `ACK-Diag` counters to the existing update
progress line and the `bulk_ack` object in the protected update statistics:

- `tx`, `tx_fail`: bulk send attempts and failed local sends.
- `rx`: ACK-shaped packets read by the active bulk wait, including rejects.
- `invalid`: malformed, authentication-failed or non-ACK bulk packets.
- `endpoint`: source IP/port did not match the pinned peer.
- `session`: authenticated UID, session nonces or transport mode did not match.
- `request`: authenticated reply belonged to another bulk request.
- `offset`: matching successful reply reported an unexpected accepted offset.
- `status`: matching reply reported a non-success device status.
- `timeout`: a send attempt exhausted its ACK wait without a matching reply.
- `unclaimed`: an ACK-shaped datagram was consumed outside the bulk waiter.
  This counter is not authenticated and does not establish why the packet arrived
  there; it can include delayed, unrelated or malformed traffic.
- `read_fail`: the socket had a peekable bulk packet but the consuming read failed.
- `poll_gap_us`: longest observed gap in the bulk wait loop, including time in
  packet processing. The web view formats it in milliseconds.

Counters reset at the beginning of an update and remain available after it ends.
They use fixed atomic storage, not extra queues or per-packet heap allocations.
The complete ACK acceptance and retry function is exercised in native tests with
good replies, corrupt/stale/wrong-session/wrong-offset replies, control traffic,
socket failures, absent replies and timer rollover. The wire format, timeout and
retry policy remain unchanged. No router settings are changed.

Install only this Master test build; leave the S3 on 1.4.44RC. During the next
WLAN OTA attempt capture the full Master progress line and the S3 `OTA WIFI`
lines for the same transfer. This change is diagnostic, not a claimed speed fix.

## Master Reconnection And Size Experiment: 2.0.78RC-Developer

The reported AP restart exposed a separate reconnect bug: after a 12-second STA
timeout the Master entered AP-only mode, and the active captive portal prevented
all later STA retries. A reboot was then needed to reconnect.

With saved credentials, fallback now retains AP+STA mode and retries association
on the existing 30-second schedule. The recovery portal remains available during
these attempts and closes only after STA connects. Initial setup without saved
credentials remains AP-only. Credentials and static IP settings are preserved.
Native tests exercise the actual connection functions, including repeated
failures, AP recovery, spontaneous STA recovery and millisecond rollover.

Developer mode also exposes a WLAN-only packet-size test selector on the update
page: 1024 B (unchanged default), 512 B or 256 B. The selection applies to one
update session, not stored settings. It changes only the bulk data limit; the
188-byte compatibility probe/fallback, RS485 path, authentication and image
validation are unchanged. The server rejects overrides outside developer mode
and rejects values other than those three exact choices.

A short PC ping sample to the S3 returned 10/10 small packets (13-49 ms), but
9/10 packets with a 1072-byte payload (34-138 ms). The same larger sample to the
Master returned 10/10 (5-48 ms). These were not synchronized controlled load
tests and do not establish a packet-size fault or a stable loss rate. The size
selector exists to test that possibility without another display firmware build.

Keep the display firmware, task priority and buffer configuration constant when
comparing bulk sizes. Conversely, keep the Master packet size constant when
testing display task or WiFi buffer changes. Neither experiment is a confirmed
fix for the observed S3 WLAN throughput. This local Master version has not been
flashed or published by these changes.
