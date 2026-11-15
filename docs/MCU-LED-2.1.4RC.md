# MCU Names and Master Status LEDs, 2.1.4RC

## Observations and Scope

The owner reported occasional white EVT flashes and uneven animation during
master OTA on both S3 and P4. `OfeStatusLed::pulseBusActivity()` was already a
no-op. Normal master EVT events do not use the white channel; the established
firmware-update blue/white breathing effect intentionally does. The healthy
OFE/bus LED also intentionally transitions to white.

Two software hazards were identified, without claiming to have measured the
electrical cause of the owner's flashes:

- Pixel rendering was driven by the main loop and the scheduler service
  callback. A blocked loop delays animation. Scheduler calls can also originate
  from HTTP update handling, so these are not safe independent pixel owners.
- The installed NeoPixel ESP-IDF-5 adapter requests one RMT block. S3 and P4
  provide 48 symbols per block, while two RGBW LEDs require 64 symbols.
  A short buffer makes transmission dependent on interrupt-driven refills.

Espressif describes delayed interrupts and unpredictable ping-pong results
during flash cache-disabled intervals in the
[RMT cache-safety documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/peripherals/rmt.html#cache-safe).

## LED Changes

`MasterStatusLedService` publishes brightness, eco cap, OFE/EVT events and OTA
override in one atomic 32-bit state. A single core-1 task at priority 2 renders
every 20 ms, with a 4096-byte stack. It reads no registry or scheduler objects.
The scheduler service only publishes state. If task allocation fails, only
the normal master loop renders. Disabled LEDs do not allocate the LED task.

`MasterStatusPixels` preserves NeoPixel GRBW byte packing and the existing
10 MHz, 8/4 versus 4/8 tick encoding. It reserves two RMT blocks (96 symbols)
for the complete 64-bit frame and termination. A fixed internal symbol array
remains owned until blocking transmission completes, and NeoPixel's 300 us
latch interval is preserved. The master transport explicitly supports up to
two status LEDs; module/display LED transports were not changed.

Master OTA and the reboot-pending interval now select the update indication,
as does module OTA. Leaving the OTA override restores the published normal
event. Flash/cache-critical sections can still pause scheduling; this is not
a guarantee of uninterrupted animation during every flash erase. No bundled
SDK, global NeoPixel library, GPIO, LED colors or effect curves were modified.

## Names and Compatibility

- Hardware model: `MCU-S3` or `MCU-P4`, meaning `Master Control Unit`.
- Web state adds `master_model` and `master_model_description`; `master_name`
  includes the product and MCU model. The status firmware caption names the MCU.
- MQTT discovery uses the MCU model and a matching device name.
- The display protocol receives the short MCU model, fitting its existing
  24-byte name field.
- Native HA device metadata and card attributes use the model when supplied,
  with the historical name/model retained for older firmware.

Serial numbers, device/entity unique IDs, user-assigned names, MQTT topics,
backup product identity and the signed firmware target `MASTER` remain intact.
The HA source changes are local, not a new published HACS release.

## Verification

Native tests execute the actual master LED headers with hardware adapters:
GRBW bit order, all 64 symbols, latch timing, task-only pixel ownership,
autonomous rendering without loop ticks, concurrent atomic setters, OTA
override restoration, allocation-failure fallback, the default LED count and
no white-channel output for the master's normal EVT states.

Firmware Python regression suite: 341 tests passed. HA suite: 185 tests passed,
including live model metadata with unchanged device/entity identities and
legacy-firmware fallback. The final S3 sketch, scheduler and display-WiFi
translation units passed the real ESP32 compiler's syntax checks.
These tests and the native
adapters do not replace a real S3/P4 LED observation during normal load and OTA.
No device was flashed and no GitHub checkout was updated automatically.

## P4 Artifact

The complete Arduino 3.3.12 PRE-V3 build passed: program 3,471,098 bytes
of the 5,242,880-byte slot, static RAM 68,884 bytes, app BIN 3,471,360 bytes.
This is 256 additional bytes of static RAM versus 2.1.3; the LED task's 4096-byte
stack and RTOS bookkeeping are additional runtime allocations.
Final ELF/BIN ECC-link verification and generator consistency checks passed.
Disassembly of the actual LED task confirms `rmtInit` with two memory blocks
and blocking `rmtWrite`, not the default single-block NeoPixel show adapter.
The app contains the MCU-P4 web/MQTT labels and the 2.1.4 MASTER marker.
The partition BIN is identical to 2.1.3.

Local artifact directory:
`MasterP4/OpenFumeExtractorMaster_P4/build/2.1.4-prev3-mcu-led/`

App SHA-256:
`a165c592265aca8d48379937cfdca23e5cd0e09cb8e87eb5855b7dd144474c9a`

The S3 source passed compiler syntax checks but no new full S3 BIN was built
in this task. HA changes remain source-only, not a new HACS release.
During the first OTA installation of 2.1.4 the previous firmware still owns
the LEDs; the new OTA animation path is active only after reboot into 2.1.4.
