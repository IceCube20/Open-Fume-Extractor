# MCU 2.1.27: WiFi Scan Lifecycle And C6 Control Diagnostics

## Owner Report

The owner supplied a 2.1.26 PoE V3.2 boot log with repeated
`Response not received for [0x15e](Req_GetCoprocessorFwVersion)` and clarified
that WLAN scanning does NOT work: the page reports a failed search. This
supersedes the earlier interpretation that scan results were available.
Date/time recovery on 2.1.26 was confirmed separately.

The attached schematic has SHA-256
`885b8f038881c6656486fea143bcf749f17c6a1ec9006cc693dcdb298018380b`, matching
the existing vendor reference. CLK18/CMD19/D0..D3 14..17 and reset54 agree with
the source configuration. No pin, SDIO clock, SDK, C6 image or reset wiring
was changed on speculation. A scan failure alone does not prove that the
entire control path is dead, nor does a version timeout identify the factory
firmware version.

## Changes

- Scan POST requests allow 20 seconds for a cold ESP-Hosted startup, versus
  the previous five seconds. GET polls retain five-second request timeouts.
  The overall scan/retry deadline remains 30 seconds.
- Temporary HTTP 409 responses from the C6 exclusive-operation guard are
  retried within that deadline, not presented immediately as a failed scan.
  Other authentication/HTTP errors are not silently retried.
- A pending, not-yet-connected STA association is paused before a manual
  scan. Credentials are retained (`disconnect(false,false)`); a connected
  WLAN session is not disconnected. Normal association retries resume after
  30 seconds with the existing reconnect logic.
- C6 background version probes also wait while a STA connection is pending.
- `[WiFi scan]` serial output distinguishes radio initialization failure,
  a paused association and the Arduino scan return code. Failed scan JSON
  includes the code and the browser preserves it in its error message.
- A failed C6 version probe now performs one version RPC, rather than first
  attempting an SDK cache refresh and then the same failing RPC again. A
  successful read still refreshes Arduino's cache used by BLE initialization.
- If the version read fails while Hosted WiFi is active, a separate read-only
  `esp_wifi_get_mode()` RPC is recorded. `[C6] Version RPC failed: ...;
  WiFi-mode RPC: ..., mode=...` and `/update/c6/status` fields `control_error`
  and `control_mode` make the two results distinguishable.
- `control_error=0` with a valid mode proves that this independent RPC replied.
  Nonzero errors require further diagnosis; mode255 means no valid mode.
  Before a control read, or in BLE-only operation, the default control error
  is ESP_ERR_INVALID_STATE and mode255; these are not a measured reply.
- Update preflight separates `C6_UNAVAILABLE` (initialization/acquire) from
  `C6_VERSION_UNAVAILABLE` (version read). Failed version preflight still
  cannot call OTA begin, write, end or activate. The UI says no firmware data
  was written instead of claiming that the chip is absent.
- PoE recovery links now point to the PoE FAQ. PRE-V3 retains its own FAQ.

This is a scan-lifecycle correction plus a diagnostic build, not a confirmed
hardware/C6 compatibility fix. Unknown C6 versions remain unknown. The pinned
2.12.13 image/hash, minimum supported version, downgrade protection, recovery
confirmation and MCU OTA chip/revision guard are unchanged. No C6 was flashed.

## Verification

- 378 Python source tests passed.
- Actual S3/P4 WiFi lifecycle native fixtures passed, including pending
  association pause, connected/setup preservation and scheduled resumption.
- P4 native fixtures passed, including version-only failure with a successful
  independent control reply, both-RPC failure, cached SDK version, backoff,
  BLE-only handling and version-preflight no-write guards.
- JavaScript tests cover longer POST timeout, POST/GET busy retries,
  non-retried HTTP401, failed scan return code and preserved form values.
- Edge/Playwright DE/EN C6 views at 320/390/768/1440 pixels passed; scan views
  at desktop/mobile passed with the embedded logo. An old scan test fixture
  was corrected to expand the actual transport-header macro and shared styles
  instead of displaying C++ macro tokens as page text. No production CSS was
  changed for that fixture correction.
- Full PoE MCU build with the installed `ofemcu:esp32:ofemcu_p4_poe` IDE
  profile passed, not only the minimal profile smoke test.
- App: `2.1.27RC-Developer`, 3,568,080 bytes. SHA-256:
  `38af5b5757adea9bff59da7f6f6253083b53c704811fd72933fbeaba9bfa5417`.
- App and bootloader both specify chip18, revisions301..399 and 32 MiB flash.
  The merged image and exported copies match. The actual ELF/BIN ECC/RNG link
  inspection passed and both generated-source manifests match the source.
- No device was flashed, no global SDK or board core was modified, and no
  GitHub publication was performed. Hardware validation is pending.

OTA file:
`tools/tests/build-checks/master-p4-poe-ide-2127/OpenFumeExtractorMCU_P4_POE.ino.bin`.
After installing, retry WLAN scan and collect `[WiFi scan]` and `[C6]` lines,
or the new C6 status JSON. Do not infer an installed C6 version from a timeout.

## References

- ESP32-C6 scan while connecting limitation:
  <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/api-guides/wifi.html#scan-when-wi-fi-is-connecting>
- PoE schematic:
  <https://files.waveshare.com/wiki/ESP32-P4-WIFI6-POE-ETH/ESP32-P4-WIFI6-POE-ETH-Schematic.pdf>
- Correct PoE recovery documentation:
  <https://docs.waveshare.com/ESP32-P4-WIFI6-POE-ETH/FAQ>
