#pragma once

// Master SK6812 event mapping from current extractor, alarm and bus state.
static bool status_led_has_online_module() {
  for (uint8_t i = 0; i < registry.count(); ++i) {
    if (registry.at(i).online) return true;
  }
  return false;
}
static bool status_led_has_offline_module() {
  for (uint8_t i = 0; i < registry.count(); ++i) {
    if (!registry.at(i).online) return true;
  }
  return false;
}
static bool status_led_fault_is_critical(uint16_t faults) {
  return faults && faults != 0x0002U; // filter warning is warning-level
}
static bool status_led_jbc_error_is_critical(uint16_t error) {
  return error && (error & (uint16_t)~0x0002U) != 0; // JBC filter warning is warning-level
}
static bool status_led_has_warning_alarm() {
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    if (!m.online) continue;
    if ((m.caps & CAP_JBC_BUS) && (!m.station_addr || !(m.jbc_link_flags & FAST_FLAG_CONNECTED))) return true;
    if ((m.caps & CAP_JBC_USB) && !(m.jbc_link_flags & FAST_FLAG_CONNECTED)) return true;
    if ((m.caps & CAP_WELLER_INTERFACE) && (m.weller_uart_age_sec == 0xFFFF || m.weller_uart_age_sec > 10)) return true;
  }
  return false;
}

static void update_status_leds() {
  ofe_status_leds.setBusOnline(status_led_has_online_module());
  ofe_status_leds.setFirmwareUpdate(scheduler.firmwareUpdateActive());

  const uint16_t output_fault = extractor.outputState().fault_mask;
  const uint16_t jbc_error = scheduler.systemJbcError();
  const bool critical_alarm = status_led_has_offline_module() || status_led_fault_is_critical(output_fault) || status_led_jbc_error_is_critical(jbc_error);
  const bool warning_alarm = scheduler.mainInputSourceType() == 0 || status_led_has_warning_alarm() || output_fault == 0x0002U || jbc_error == 0x0002U;
  if (critical_alarm) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_CRITICAL);
  } else if (extractor.afterrunLeftMs() > 0) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_AFTER_RUN);
  } else if (extractor.continuous()) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_CONTINUOUS);
  } else if (extractor.outputEnabled()) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_EXTRACTOR_ON);
  } else if (warning_alarm) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_WARNING);
  } else {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_OFF);
  }
}
