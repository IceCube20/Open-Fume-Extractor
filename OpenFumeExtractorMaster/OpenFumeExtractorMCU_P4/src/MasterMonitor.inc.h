#pragma once

// Serial monitor summary helpers for CLI monitor mode and non-web builds.
static uint32_t last_summary_ms = 0;

static void print_summary() {
  const JbcModuleState& js = extractor.jbcState();
  const uint16_t power_pct = (extractor.outputPower() + 5U) / 10U;
  const uint32_t afterrun_s = (extractor.afterrunLeftMs() + 999UL) / 1000UL;
  Serial.print(F("[MON] modules="));
  Serial.print(registry.count());
  Serial.print(F(" output="));
  Serial.print(extractor.outputEnabled() ? F("on") : F("off"));
  Serial.print(F(" power="));
  Serial.print(power_pct);
  Serial.print(F("% work=0x"));
  Serial.print(extractor.workMask(), HEX);
  Serial.print(F(" continuous="));
  Serial.print(extractor.continuous() ? F("on") : F("off"));
  Serial.print(F(" afterrun="));
  Serial.print(afterrun_s);
  Serial.print(F("s"));
  Serial.print(F(" cpu="));
  Serial.print(cpu_load_pct);
  Serial.print(F("% loop="));
  Serial.print(loop_max_ms);
  Serial.print(F("ms heap="));
  Serial.print(ESP.getFreeHeap() / 1024);
  Serial.print(F("KB"));
  if (js.valid) {
    Serial.print(F(" suction="));
    Serial.print(js.suction_level);
    Serial.print(F(" custom="));
    Serial.print(js.select_flow / 10);
    Serial.print(F("% jbc_err=0x"));
    Serial.print(js.stat_error, HEX);
  }
  Serial.println();
}

static void monitor_summary_tick(uint32_t now) {
  if ((uint32_t)(now - last_summary_ms) < 10000UL) return;
  last_summary_ms = now;
#if WEB_ENABLE
  if (serial_cli_monitor_enabled) print_summary();
#else
  print_summary();
#endif
}
