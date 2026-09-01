#pragma once

// Main runtime tick. Keeps Arduino loop() tiny while preserving the original
// execution order for CLI, LEDs, module OTA, scheduler, logic and telemetry.
static void master_loop_tick() {
#if WEB_ENABLE
  serial_cli_tick();
#endif
  update_status_leds();
  ofe_status_leds.tick();
  const uint32_t loop_start_us = micros();

  // External controls are serialized through the loop before OTA/background
  // traffic. Critical OFF commands are drained first inside this function.
  master_command_queue_process();

  module_update_pump();
  scheduler.tick();
#if WEB_ENABLE
  logic_runtime_tick();
#endif
#if WEB_ENABLE
  if (scheduler.consumeScanJobFinished()) {
    registry.sortByAddress();
    apply_module_labels();
    save_module_snapshot();
    load_routing_config();

    // HA discovery is reconciled from the persisted publish manifest. Do not
    // brute-force every possible module/entity topic after each scan.
    mqtt_discovery_published = false;
    mqtt_next_discovery_check_ms = 0;
    mqtt_last_publish_ms = 0;
  }
#endif
  const uint32_t busy_us = (uint32_t)(micros() - loop_start_us);
  if (busy_us > loop_max_us) loop_max_us = busy_us;

  const uint32_t now = millis();
  if ((uint32_t)(now - loop_window_ms) >= 1000UL) {
    uint32_t max_ms = (loop_max_us + 999UL) / 1000UL;
    if (max_ms > 65535UL) max_ms = 65535UL;
    loop_max_ms = (uint16_t)max_ms;
    sample_cpu_load();
    scheduler.setMasterTelemetry(cpu_load_pct, loop_max_ms);
    loop_window_ms = now;
    loop_max_us = 0;
  }
  monitor_summary_tick(now);

  // Prevent the Arduino loop task from busy-spinning at 100% on one core.
  // The delay is intentionally placed after telemetry/runtime measurement,
  // so loop_max_ms continues to report actual work rather than idle time.
  delay(1);
}
