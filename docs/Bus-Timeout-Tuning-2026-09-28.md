# Bus Timeout Tuning

Date: 2026-09-28. Scope: Master firmware only, version 2.0.16RC-Developer.

## Evidence

The supplied diagnostic snapshot reports approximately 6% OFE bus load and
mostly 2-5 ms successful response latency. The largest observed successful
GET_STATUS replies for Fan/IO, Weller, and Modbus were about 58-60 ms, while
the configured GET_STATUS deadline was 50 ms. The WiFi display had successful
responses at roughly 490-508 ms with a 500-ms asynchronous deadline. Its
DISPLAY_STATUS and display-cache requests account for most display timeouts.

These measurements justify narrow deadline changes, not a global bus priority
increase or a blanket timeout increase.

## Changes

- GET_STATUS response deadline: 50 -> 80 ms. Successful normal requests still
  complete as soon as their reply arrives. A missing module now occupies the
  synchronous request path for up to 30 ms longer per status attempt.
- Asynchronous WiFi display deadline: 500 -> 750 ms, maximum accepted async
  deadline 600 -> 900 ms. No extra loopTask waiting is introduced. Recovery
  from an actually missing WiFi display may be correspondingly slower.
- Other module, JBC, IO, telemetry, RS485 display, OTA, and network settings
  remain unchanged. The bus baud rate and scheduling priorities are unchanged.

## What This Does Not Fix

CRC, LEN, and ESC are serial parser errors; response deadlines cannot repair
damaged or truncated RS485 frames. SEQ and Late are separate reply-correlation
counters, not equivalent to a CRC failure. The counters are cumulative since
boot, so compare their **increase per hour after flashing**, not the old totals.

If CRC or LEN continues increasing, inspect the two physical end terminations,
cable impedance and length, common reference/ground, biasing, connector quality,
and transceiver power with a scope if available. The previously mentioned
180-ohm end resistors are worth checking against the actual cable's characteristic
impedance; 120 ohms is common for RS485 cable, but should not be assumed to
eliminate all faults. Avoid adding terminators at every intermediate module.

## Verification

- 68 Python regression tests passed, including a source integration check that
  the measured deadline values are used by the status and async display paths.
- Master firmware compilation passed with the intended ESP32-S3 configuration.
- No physical master was flashed in this pass. A multi-day hardware comparison
  remains necessary, especially for CRC/LEN and actual module response jitter.

## Follow-up: 2.0.18RC-Developer

A later snapshot from 2.0.16RC-Developer with three wireless displays showed
successful display replies up to about 800 ms, but the async deadline was
750 ms. Display timeout rates were 0.47-1.2%, compared with 0-0.06% for most
RS485 modules. The JBC FAE modules also had successful GET_STATE replies up
to 104 ms with a 50-ms deadline. Serial transport errors remained low (CRC 0,
LEN 14, ESC 0 in that snapshot), so these are distinct problems.

- Increase only the nonblocking WiFi-display async deadline from 750 to
  1000 ms and its cap from 900 to 1200 ms. This does not make the master loop
  wait. A truly unresponsive display may take roughly 1.25 s longer to pass
  the five-miss offline threshold.
- Increase only JBC FAE GET_STATE from 50 to 120 ms. FAST_POLL and JBC USB
  state deadlines remain unchanged. A missing JBC FAE reply can now block the
  synchronous path for 70 ms longer, so verify loop maxima and control latency
  on hardware.
- Do not change the baud rate, global bus priority, or other module deadlines.

This is deadline tuning, not a proven fix for WiFi packet loss. Compare fresh
per-hour timeout, Late, and SEQ counts after flashing; also check WiFi RSSI,
router airtime/retries, and whether 0x41 resumes returning telemetry. If a
display remains silent while authenticated keepalives continue, inspect its
request handler and UDP receive path rather than raising the deadline again.
