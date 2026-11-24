#pragma once

// Master SK6812 event mapping from current extractor, alarm and bus state.
static bool status_led_has_online_module() {
  for (uint8_t i = 0; i < registry.count(); ++i) {
    if (registry.at(i).online) return true;
  }
  return false;
}
struct StatusLedAlarmState {
  bool warning = false, critical = false;
};
static StatusLedAlarmState status_led_alarm_state() {
  StatusLedAlarmState state;
  state.warning = !scheduler.mainInputSourceAvailable() || scheduler.outputAddr() == 0;
  bool has_jbc_module = false, active_output_fault_alarm = false;
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    const auto alarm = scheduler.moduleAlarmState(m);
    state.warning |= alarm.warning();
    state.critical |= alarm.error();
    has_jbc_module |= m.online && (m.caps & CAP_JBC_BUS);
    active_output_fault_alarm |= m.online && m.addr == scheduler.outputAddr() && alarm.faults;
  }
  // Match the alarm centre: optional/muted links cannot create hidden LED alarms.
  if (scheduler.outputAddr() && has_jbc_module && !active_output_fault_alarm) {
    const uint16_t error = scheduler.systemJbcError();
    state.warning |= (error & 0x0002U) != 0;
    state.critical |= (error & (uint16_t)~0x0002U) != 0;
  }
  return state;
}

static void update_status_leds() {
  ofe_status_leds.setEcoMode(scheduler.powerSaveActive(), 51);
  ofe_status_leds.setBusOnline(status_led_has_online_module());
  ofe_status_leds.setFirmwareUpdate(scheduler.firmwareUpdateActive()
#if WEB_ENABLE
    || master_update_active || master_update_restart_due_ms.load()
#endif
  );

  const auto alarm = status_led_alarm_state();
  if (alarm.critical) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_CRITICAL);
  } else if (extractor.afterrunLeftMs() > 0) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_AFTER_RUN);
  } else if (extractor.continuous()) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_CONTINUOUS);
  } else if (extractor.outputEnabled()) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_EXTRACTOR_ON);
  } else if (alarm.warning) {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_WARNING);
  } else {
    ofe_status_leds.setModuleEvent(OFE_LED_EVENT_OFF);
  }
}
