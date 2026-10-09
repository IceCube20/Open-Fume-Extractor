# Request Timeout Diagnostics

Master 2.0.26RC-Developer adds bounded, allocation-free observations of
synchronous RS485 request timeouts. No wire formats, module firmware, timeout
limits, retry policies or UART settings are changed.

The Diagnostics page exposes the following extra fields only when developer
mode is enabled:

- Separate GET_IO and SET_IO timeout totals. Other commands in the existing I/O
  timeout class remain visible as a separate remainder.
- The last synchronous RS485 timeout's command, time limit and elapsed wait.
- The largest interval between master receive-service attempts, including a
  final scheduling stall that expired the deadline before another attempt.
- Valid physical frames and parser CRC, length, version, escape, overflow and
  short-frame counter deltas during the request.
- Target-specific SEQ, CMD and recognized late-response deltas during the same
  wait. These distinguish a missing response from a valid but rejected reply.

A large receive-service gap is evidence that the master was not servicing
reception for that interval. It does not identify the delaying task. A small
gap does not prove the module is at fault: line faults, missing responses or
hardware UART overruns are still possible. The overflow delta above belongs
to the software frame parser, not the UART driver's FIFO/ring-buffer counters.
Parser deltas cover the whole physical bus, not exclusively the target module.
These measurements are not UART arrival timestamps or module execution times.

Asynchronous WLAN timeouts clear the synchronous probe's validity flag, so an
older RS485 snapshot cannot be mistaken for network-delay evidence. The last
timeout observation remains available after later successful requests and is
replaced on the next timeout. All observations are RAM-only and reset on boot.

## Length-Rejected Frame Snapshot (2.0.27)

If a synchronous timeout observed at least one LEN error, developer mode also
shows the last length-rejected frame from that wait: its decoded byte count,
the size declared by the header (six header bytes + payload + two CRC bytes),
and the six raw header bytes (version, destination, source, sequence, command,
payload length). Escaping and delimiter bytes are excluded from these sizes.
The timed-out request's sequence is displayed separately for comparison.

These header bytes are **unverified**: length validation failed before CRC
validation. In particular, the displayed source address is not proof that a
specific module sent this data. Missing bytes, extra bytes and a corrupt header
are all possible. All LEN failures reaching this check have a complete header;
short, wrong-version, escape and overflow failures retain their separate
counters and do not replace the LEN snapshot. Only the last LEN failure in a
wait is captured, not every faulty frame.

The snapshot is copied into the timeout record, so subsequent unrelated parser
errors do not alter it. A later timeout without LEN errors shows no old header.
The shared parser sources are synchronized across module projects, but only
the master needs updating to use this diagnostic. No wire protocol changes or
module update are required.

## VERSION and Valid-Frame Snapshots (2.0.28)

VERSION failures now retain the last rejected frame's decoded byte count and
six raw header bytes independently of the LEN snapshot. The expected protocol
byte is `0x01`; this is not a firmware-version check. VERSION validation runs
before both length and CRC validation, so none of this rejected header is
verified. Short frames do not overwrite it.

When the physical parser accepted valid frames during the failed wait, the
master also retains the last physical frame's destination, source, sequence,
command and payload length. Its `filtered` flag says whether the serial
transport filter dropped it before the synchronous request received it. This
observation is recorded even if a WLAN response is returned by the same poll;
WLAN polling does not overwrite the physical snapshot. A valid CRC detects
transmission errors but is not cryptographic sender authentication.

Separately, valid frames returned to the request but not used as its response
are counted. Only the last such frame's header and disposition are retained:
different source/destination, wrong sequence, wrong command, recognized late
response, or a separate display response handled by the asynchronous path.
The last frame's transport is explicit: WLAN traffic can be handled while an
RS485 request waits. These counts are not an additional RS485 error total and
can include normal unrelated traffic. They do not include frames dropped by
the serial filter; those are visible in the physical parser count and last
physical snapshot instead.

All these snapshots are copied only into the failed request's RAM record and
are gated by its current-wait counters. Later successful traffic cannot replace
them, and a new timeout cannot display an old frame from an earlier wait. The
developer-mode API gate remains server-side. No payloads are retained. Source
copies of the shared bus implementation remain synchronized; only the master
needs flashing for these diagnostics. Timeouts, retry policy, scheduling and
wire protocol are unchanged.

## Buffered Transmission and Segment Checks (2.0.29)

Physical transmission now escapes a complete frame into one bounded 402-byte
stack buffer and calls the UART buffer-write API once. Backward in-place
escaping avoids a second buffer and does not allocate heap memory. The wire
format, final UART flush, routing and timeouts are unchanged. This removes
per-byte UART locking/calls; it does not prove or guarantee uninterrupted
electrical transmission under every condition.

On a LEN rejection, the parser can check the CRC of the header-declared prefix
if that many decoded bytes exist. It records the remaining byte count and,
when present, the next six-byte header. The remaining segment's CRC is checked
only when its declared size exactly matches the remaining data and its payload
fits the protocol limit. Checks are bounded by the existing 201-byte parser
buffer; oversized joined contents still follow normal overflow rejection.

Developer mode displays both CRC check results and the second header. Two
CRC-valid segments with protocol version 1 support the joined-frame-contents
hypothesis, but do not prove where framing was lost or authenticate the sender.
The complete malformed frame remains rejected, never split into usable replies.
Diagnostic CRC checks do not increment normal CRC counters. No payloads are
retained and a new LEN snapshot clears all earlier segment results.

The shared bus sources are synchronized across module sketches. For this test,
update the master to 2.0.29RC-Developer and the affected Fan/IO Pro to 1.1.70RC
(Fan/IO: 1.1.75RC). Unlike the earlier diagnostic-only changes, the module
must also be rebuilt/flashed to benefit from buffered response transmission.

## Firmware Maintenance Accounting (2.0.30)

OFE firmware requests and their retries now have separate update request/timeout
counters. Normal module polling is suppressed only for the update target;
unrelated modules retain normal timeout and availability handling. An outstanding
WLAN sample for the target is canceled before the transfer, without a miss.

After a successful FW_END, the target is invalidated as before so stale outputs
and descriptors are not treated as live. Reconnect probes have separate restart
wait counters for up to 15 seconds, ending earlier on a validated INFO response.
The interval uses unsigned elapsed time and handles millis rollover. After expiry,
normal accounting resumes even if the target never returns. Failed/canceled
updates do not start this successful-update recovery window.

The normal timeout percentage excludes update/recovery requests from its
denominator. The diagnostic module card shows their separate timeout counts.
Raw trace events remain available, with maintenance labels and separate trace
timeout counters. CRC, LEN, ESC, wrong-command and sequence errors are not
globally suppressed. WLAN bulk transfers keep their existing transfer-layer
retry handling; these OFE request counters do not count bulk UDP packets.

Only the master needs rebuilding/flashing for this change. Existing diagnostic
counts are not reset by starting an update.

## Hardware Check

Update the master and affected Fan/IO module. Enable developer mode, open Bus Diagnostics and wait
for a fresh Fan/IO timeout. Record its command, time limit, elapsed wait,
receive-service gap and parser deltas. Compare several events before changing
timing or assigning a root cause. The earlier I/O class alone does not prove
that every failure was GET_IO or SET_IO.

## Native Regression Test

Run `tools/tests/request_wait_native/run.ps1` on the configured Windows build
host. It compiles the production header and covers clean waits, final stalls,
parser-error deltas, independent snapshots and 32-bit clock/counter rollover.
The same runner compiles the actual RS485 parser and Link implementation to
test LEN/VERSION rejection, escaping, filtered and forwarded physical frames,
WLAN/RS485 observation separation, recovery, reset and snapshot retention.
It also compares 10,000 buffered encodings against a reference encoder,
checks maximum payloads and escaped bytes, and tests valid/corrupt/truncated
joined contents without relaxing rejection or changing error counters.
