# Technical Review and Targeted Refactor

Date: 2026-09-27. Scope: the local firmware source project, not the public release tree.

## Assessment

The project already separates transport, scheduling, settings, discovery and UI
responsibilities. Shared bus/LED sources are consistent across all ten module
sketches. Native tests exercise real production handlers and rendering paths,
which is substantially stronger than relying on source-pattern checks alone.

The principal risks found in this pass were state transitions and ownership,
not insufficient nominal bus bandwidth. The supplied long-running diagnostics
show low typical transport latency and low average bus utilization, but averages
do not reveal command-specific bursts, network stalls or maximum handler latency.
Timeout counts alone cannot prove a deadline is too short. No blanket timeout
increase, display timing change or additional polling load was introduced.

Maintainability still needs attention: the scheduler, web status renderer and JBC
USB sketch remain large. Included `.inc.h` implementation files share global
state and declaration order with their parent sketch; this is not the same
isolation as independently compiled classes with explicit interfaces. A wholesale
rewrite during a stability pass would add substantial regression risk.

## Confirmed Issues Corrected

1. **Captive configuration authentication.** A configured password must still be
   authenticated while the AP/captive portal is active. Only initial setup or an
   explicit authentication reset permits the intended bootstrap exception.
   Credential checking is now shared without invoking the captive redirect guard.
2. **Competing browser OTA sessions.** Failure before acquiring an update session
   must not abort an upload started by another browser. Cancellation waits for
   its own pending BEGIN response and aborts only after that BEGIN succeeds.
3. **MQTT availability topic ownership.** Offline publication now uses the topic
   root of the connected MQTT session, not newly edited configuration. This
   prevents leaving the previous topic's retained availability online during a
   base-topic change. Publication remains dependent on broker connectivity.
4. **Display packet bounds.** Both detail parsing paths in all three displays now
   validate the complete fixed tail after the variable-length firmware suffix,
   before reading those fields. The previous initial-length check did not cover
   a long suffix followed by an incomplete fixed tail.
5. **Display OTA task ownership.** Transport processing owns update timeout checks
   as well as BEGIN/CHUNK/END. The Arduino loop checks timeouts only when the
   transport task is unavailable. The UI-facing active flag, byte counters and
   start timestamp are atomic. Percentage calculations snapshot their denominator;
   the timeout timestamp is initialized before publishing the active state.
6. **RS232 repeat commands during failsafe.** Readable output state may remain ON
   after an OFF command until the device replies. Repeat scheduling now chooses
   OFF while the output failsafe is active instead of replaying the stale ON state.
7. **Rejected profile imports.** Universal RS232 and Modbus share a scoped state
   transaction. Invalid input, allocation failure or failure to persist the
   authoritative profile text restores the existing runtime profile. The stored
   text and UART restart are committed only after validation. The snapshot uses
   heap memory, contains only trivially copyable state and is released on return.
8. **Modbus transaction ownership and matching.** Only one RTU request may be in
   flight. Responses must match its slave and function; writes must also echo
   the register and encoded value. Read byte counts and frame lengths are exact.
   Unsolicited replies can no longer update the first vaguely matching entity.
   A bounded write queue preserves multiple output commands, coalesces normal
   target values, preserves button events and gives failsafe OFF commands priority.
   Timed-out exchanges enter a 250-ms recovery period and require an idle receive
   path before another request starts. Successful profile changes discard old
   exchange context and queued writes.
   The response deadline includes both frames' wire time at low UART baud rates;
   it remains 250 ms at the normal 9600 baud and above.
9. **Fan/IO deferred persistence after reset.** Factory reset cancels the pending
   manual-power save instead of allowing an old value to reappear later. Pro also
   clears pending filter-runtime persistence. Failed power writes remain dirty
   and retry after 60 seconds instead of silently being treated as saved.
10. **Expired WiFi peer state.** Receive processing explicitly invalidates peers
    after five seconds without traffic. Their published timestamp is cleared,
    so an ancient connected timestamp cannot become apparently fresh after a
    complete `millis()` cycle.

## Verification

- Python regression suite: 67 tests passed.
- Embedded update JavaScript: syntax, DE/EN transport labels, developer-only
  chunk display, signature rejection and update-session cancellation passed.
- Native suite: transport authentication/replay checks, OTA retry/error paths,
  actual handlers from all three display variants, JBC labels, module availability,
  touch/inertia, LVGL allocator and pixel-identical RGB copying passed.
- New native coverage: captive authentication, 148,224 combinations of detail
  packet lengths and suffix lengths across three displays, transaction rollback,
  allocation failure, power-write retries, MQTT availability topic ownership,
  RTU response/echo validation, low-baud response deadlines, queued OFF, button events and
  unsigned time arithmetic across rollover.
- Shared bus and LED source hash check: all ten module sketches passed.
- ESP32 firmware compilation results are recorded below.

| Target | App Bytes | Static RAM Bytes | Build |
| --- | ---: | ---: | --- |
| Master | 2,493,903 | 83,316 | Passed, ESP32-S3 / OPI PSRAM |
| Fan/IO | 336,412 | 31,372 | Passed, ESP32 |
| Fan/IO Pro | 368,176 | 33,412 | Passed, ESP32 |
| Universal RS232 | 400,365 | 90,596 | Passed, ESP32 |
| Modbus RTU | 375,029 | 76,180 | Passed, ESP32 |
| Display 320x480 | 1,743,602 | 97,816 | Passed, ESP32-S3 / OPI PSRAM |
| Display 800x480 | 1,755,546 | 89,836 | Passed, installed `ofe:esp32:ofe800` high-performance SDK |
| Display ST7796 320x480 | 1,730,066 | 97,604 | Passed, ESP32-S3 / OPI PSRAM |

Static RAM totals do not include runtime heap allocations or task stacks. Master
app usage is 74% of its 3,342,336-byte OTA slot, not 74% of the 16-MB flash chip.
Display builds emit linker warnings about missing `.note.GNU-stack` sections in
SDK/toolchain objects (`crtn.o` or `_floatdidf.o`); they complete successfully.
Those objects were not modified in this pass.

Native packet tests execute the production parser prefix; they are not a complete
fuzzer for every descriptor extension. The rollback helper is tested directly;
all profile combinations and real NVS power-loss behavior still need hardware
tests. No connected controller was flashed and no GitHub release was changed.

## Remaining Constraints

- Modbus RTU read replies do not carry a transaction ID or the requested register.
  A very late response with identical slave/function/length is fundamentally
  ambiguous once a later request begins. Recovery reduces this risk; it cannot
  provide absolute correlation. Queue acceptance also does not imply physical
  actuator feedback or confirmed command delivery.
- Profile rollback protects runtime state and checks the authoritative NVS text
  write. It does not make all ancillary configuration-key writes a single
  power-loss-atomic NVS transaction.
- OTA cancellation ownership prevents the automatic browser error path from
  cancelling another session. It is not a server-side per-client ownership token.
- HTTP Basic authentication and password gating do not encrypt an HTTP session.
  Use a trusted network; signed firmware protects firmware authenticity, not
  confidentiality of normal web traffic.
- Hardware-only faults, power integrity, termination and long-duration timing
  cannot be excluded by native tests and compilation. Keep the existing stable
  display SDK, cache and panel settings during validation.

## Firmware Versions

| Target | Version |
| --- | --- |
| Master | 2.0.15RC-Developer |
| Fan/IO | 1.1.74RC |
| Fan/IO Pro | 1.1.69RC |
| Universal RS232 | 1.0.72beta |
| Modbus RTU | 1.0.58beta |
| Display 320x480 | 1.3.99RC |
| Display 800x480 | 1.4.6RC |
| Display ST7796 320x480 | 1.0.9RC |

Original affected implementation files were copied before editing to
`tools/refactor-backups/2026-09-27/`. These are local reference copies, not release
artifacts. Existing exported/signed binaries have not been overwritten.

## Hardware Validation Checklist

1. With a custom password already set, start captive/AP mode and verify that
   configuration changes still require the password. Check first setup separately.
2. Upload in browser A; attempt another upload in browser B. B must fail without
   stopping A. Cancel A during BEGIN and during chunk transmission.
3. Change the MQTT base topic while connected. Verify old availability becomes
   offline and the new session publishes its normal availability and discovery.
4. Import invalid and oversized profiles with a working profile loaded. Verify
   the UART parameters and every existing entity remain unchanged.
5. Send several Modbus output writes together. Inject wrong-slave replies, wrong
   write echoes and delayed replies. Check that readback is not replaced by
   command shadows and all intended OFF commands reach the slave.
6. Let RS232 master traffic time out while an RW output still reports ON. Verify
   repeat commands do not reactivate the output.
7. Change Fan/IO power, then factory-reset before the deferred-save deadline.
   Wait beyond that deadline and reboot; old power must not return.
8. Abort and retry OTA over RS485 and WiFi on all three displays. Continue a
   multi-day stability run with normal UI, routing and MQTT activity.
