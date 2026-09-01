#pragma once

// Synchronous command bridge from Web/MQTT tasks to the Arduino master loop.
// The request object lives on the caller's stack; the caller sleeps until the
// loop executes it, so pointer/string payloads stay valid without heap copies.
//
// Important:
// - Only the main loop executes scheduler/registry mutations routed here.
// - OFF/zero-output commands use a dedicated priority queue.
// - Queue storage is static; there is no dynamic allocation after setup.
// - Calls from the master loop itself execute directly to avoid deadlock.

enum MasterCommandType : uint8_t {
  MASTER_CMD_NONE = 0,
  MASTER_CMD_SCAN_REQUEST,
  MASTER_CMD_PROBE_MODULE,
  MASTER_CMD_SET_MODULE_LABEL,
  MASTER_CMD_SET_IO_ALIAS,
  MASTER_CMD_APPLY_CONTROL_SETTINGS,
  MASTER_CMD_SET_AFTERRUN_PROFILE,
  MASTER_CMD_PERSIST_CONTROL_SETTINGS,
  MASTER_CMD_SET_IO_OUTPUT,
  MASTER_CMD_SET_MODULE_POWER,
  MASTER_CMD_SET_MODULE_OUTPUT,
  MASTER_CMD_CALIBRATE_FANIO_FILTER,
  MASTER_CMD_SET_WELLER_SPEED,
  MASTER_CMD_RESET_WELLER_FILTER,
  MASTER_CMD_SET_WELLER_FILTER_RUNTIME,
  MASTER_CMD_SET_DISPLAY_SETTINGS,
  MASTER_CMD_SET_UNIVERSAL_PROFILE,
  MASTER_CMD_READ_UNIVERSAL_PROFILE,
  MASTER_CMD_SET_UNIVERSAL_ENTITY,
  MASTER_CMD_SET_JBC_USB_STATION_NAME,
  MASTER_CMD_SET_JBC_USB_CONFIG,
  MASTER_CMD_SET_MAIN_INPUT,
  MASTER_CMD_SET_PREFERRED_OUTPUT,
  MASTER_CMD_SET_JBC_INPUT_ENABLED,
  MASTER_CMD_SET_IO_INPUT_ROUTE,
  MASTER_CMD_SET_INPUT_RULE,
  MASTER_CMD_MODULE_REBOOT,
  MASTER_CMD_SET_LED_CONFIG,
  MASTER_CMD_TRACE_START,
  MASTER_CMD_TRACE_STOP,
  MASTER_CMD_TRACE_CLEAR,
};

struct MasterCommandRequest {
  MasterCommandType type = MASTER_CMD_NONE;
  SemaphoreHandle_t done = nullptr;
  bool result = false;

  uint8_t addr = 0;
  uint8_t a = 0;
  uint8_t b = 0;
  uint8_t c = 0;
  uint8_t d = 0;
  uint8_t e = 0;
  uint16_t w1 = 0;
  uint16_t w2 = 0;
  uint16_t w3 = 0;
  uint16_t w4 = 0;
  uint32_t dw1 = 0;
  uint64_t qw1 = 0;
  bool flag1 = false;
  bool flag2 = false;
  bool flag3 = false;

  const char* s1 = nullptr;
  const char* s2 = nullptr;
  const char* s3 = nullptr;
  const char* s4 = nullptr;
  const char* s5 = nullptr;
  const char* s6 = nullptr;
  const char* s7 = nullptr;
  const char* s8 = nullptr;

  const uint8_t* bytes = nullptr;
  uint8_t len = 0;

  MasterScheduler::InputActionRule rule;

  char* out_text = nullptr;
  size_t out_len = 0;
  uint32_t* out_crc = nullptr;
  bool* out_truncated = nullptr;
};

static QueueHandle_t master_command_queue_normal = nullptr;
static QueueHandle_t master_command_queue_critical = nullptr;
static StaticQueue_t master_command_queue_normal_ctrl;
static StaticQueue_t master_command_queue_critical_ctrl;
static uint8_t master_command_queue_normal_storage[
  MASTER_COMMAND_QUEUE_LENGTH * sizeof(MasterCommandRequest*)] = {};
static uint8_t master_command_queue_critical_storage[
  MASTER_COMMAND_QUEUE_LENGTH * sizeof(MasterCommandRequest*)] = {};
static TaskHandle_t master_command_loop_task = nullptr;
static volatile uint32_t master_command_queue_rejects = 0;
static volatile uint32_t master_command_queue_processed = 0;
static volatile uint8_t master_command_queue_max_depth = 0;

static bool master_command_is_critical(const MasterCommandRequest& r) {
  if (r.type == MASTER_CMD_SET_MODULE_OUTPUT && !r.flag1) return true;
  if (r.type == MASTER_CMD_SET_MODULE_POWER && r.w1 == 0) return true;
  if (r.type == MASTER_CMD_SET_IO_OUTPUT && (r.w2 & r.w1) == 0) return true;
  if (r.type == MASTER_CMD_TRACE_STOP) return true;
  return false;
}

static void master_command_execute(MasterCommandRequest& r) {
  r.result = false;
  switch (r.type) {
    case MASTER_CMD_SCAN_REQUEST:
      scheduler.requestScanKnownModules(r.flag1, r.flag2);
      r.result = true;
      break;

    case MASTER_CMD_PROBE_MODULE:
      scheduler.probeModule(r.addr);
      registry.sortByAddress();
#if WEB_ENABLE
      apply_module_labels();
      load_routing_config();
#endif
      r.result = true;
      break;

    case MASTER_CMD_SET_MODULE_LABEL: {
      r.result = scheduler.setModuleLabel(r.addr, r.s1 ? r.s1 : "");
#if WEB_ENABLE
      if (r.result && r.qw1) {
        Preferences prefs;
        if (prefs.begin(MasterSettingsStore::NS_CFG, false)) {
          if (r.s1 && r.s1[0]) prefs.putString(module_label_key(r.qw1).c_str(), r.s1);
          else prefs.remove(module_label_key(r.qw1).c_str());
          prefs.end();
        }
        apply_module_labels();
      }
#endif
      break;
    }

    case MASTER_CMD_SET_IO_ALIAS:
      r.result = scheduler.setIoAlias(r.addr, r.a, r.s1 ? r.s1 : "");
      break;

    case MASTER_CMD_APPLY_CONTROL_SETTINGS: {
      scheduler.setControlSettings(
        r.a, r.w1, r.w2, r.w3, r.flag1, r.flag2, r.flag3);
      const JbcModuleState applied = scheduler.controlSettings();
      for (uint8_t i = 0; i < registry.count(); ++i) {
        const ModuleRecord& m = registry.at(i);
        if (!m.online || !(m.caps & CAP_JBC_BUS)) continue;
        scheduler.setJbcSettings(
          m.addr,
          applied.suction_level,
          applied.select_flow,
          applied.delay_work_sec,
          applied.delay_stand_sec,
          applied.stand_intakes != 0,
          applied.continuous != 0);
      }
      r.result = true;
      break;
    }

    case MASTER_CMD_SET_AFTERRUN_PROFILE:
      scheduler.setAfterrunPowerProfile(r.flag1, r.w1, r.flag2);
      r.result = true;
      break;

    case MASTER_CMD_PERSIST_CONTROL_SETTINGS:
      scheduler.persistControlSettingsNow();
      r.result = true;
      break;

    case MASTER_CMD_SET_IO_OUTPUT:
      r.result = scheduler.setIoOutput(r.addr, r.w1, r.w2);
      break;

    case MASTER_CMD_SET_MODULE_POWER:
      r.result = scheduler.setModulePower(r.addr, r.w1);
      break;

    case MASTER_CMD_SET_MODULE_OUTPUT:
      r.result = scheduler.setModuleOutput(r.addr, r.flag1, r.w1);
      break;

    case MASTER_CMD_CALIBRATE_FANIO_FILTER:
      r.result = scheduler.calibrateFanIoProFilter(r.addr, r.a, r.w1, r.w2);
      break;

    case MASTER_CMD_SET_WELLER_SPEED:
      r.result = scheduler.setWellerSpeed(r.addr, r.a);
      break;

    case MASTER_CMD_RESET_WELLER_FILTER:
      r.result = scheduler.resetWellerFilter(r.addr);
      break;

    case MASTER_CMD_SET_WELLER_FILTER_RUNTIME:
      r.result = scheduler.setWellerFilterRuntime(r.addr, r.w1);
      break;

    case MASTER_CMD_SET_DISPLAY_SETTINGS:
      r.result = scheduler.setDisplaySettings(r.addr, r.a, r.b, r.c, r.d);
      break;

    case MASTER_CMD_SET_UNIVERSAL_PROFILE:
      r.result = scheduler.setUniversalProfile(
        r.addr,
        r.s1 ? r.s1 : "",
        r.s2 ? r.s2 : "",
        r.dw1,
        r.s3 ? r.s3 : "",
        r.s4 ? r.s4 : "",
        r.s5 ? r.s5 : "NONE",
        r.s6 ? r.s6 : "CR",
        r.s7);
      break;

    case MASTER_CMD_READ_UNIVERSAL_PROFILE:
      r.result = scheduler.readUniversalProfileText(
        r.addr, r.out_text, r.out_len, r.out_crc, r.out_truncated);
      break;

    case MASTER_CMD_SET_UNIVERSAL_ENTITY:
      r.result = scheduler.setUniversalEntity(r.addr, r.a, r.bytes, r.len);
      break;

    case MASTER_CMD_SET_JBC_USB_STATION_NAME:
      r.result = scheduler.setJbcUsbStationName(r.addr, r.s1 ? r.s1 : "");
      break;

    case MASTER_CMD_SET_JBC_USB_CONFIG:
      r.result = scheduler.setJbcUsbConfig(r.addr, r.bytes, r.len);
      break;

    case MASTER_CMD_SET_MAIN_INPUT:
      r.result = scheduler.setMainInputSource(r.a, r.b, r.c, r.flag1);
      break;

    case MASTER_CMD_SET_PREFERRED_OUTPUT:
      scheduler.setPreferredOutputAddr(r.addr);
      r.result = true;
      break;

    case MASTER_CMD_SET_JBC_INPUT_ENABLED:
      scheduler.setJbcInputEnabled(r.flag1);
      r.result = true;
      break;

    case MASTER_CMD_SET_IO_INPUT_ROUTE:
      r.result = scheduler.setIoInputRoute(r.addr, r.a, r.flag1);
      break;

    case MASTER_CMD_SET_INPUT_RULE:
      r.result = scheduler.setInputRule(r.a, r.rule);
      break;

    case MASTER_CMD_MODULE_REBOOT:
      r.result = scheduler.moduleReboot(r.addr);
      break;

    case MASTER_CMD_SET_LED_CONFIG:
      scheduler.setLedConfig(r.flag1, r.a);
      r.result = true;
      break;

    case MASTER_CMD_TRACE_START:
      scheduler.traceStart(r.addr, r.flag1);
      r.result = scheduler.traceStats().active;
      break;

    case MASTER_CMD_TRACE_STOP:
      scheduler.traceStop();
      r.result = !scheduler.traceStats().active;
      break;

    case MASTER_CMD_TRACE_CLEAR:
      scheduler.traceClear();
      r.result = true;
      break;

    default:
      break;
  }
}

static void master_command_queue_begin() {
  master_command_loop_task = xTaskGetCurrentTaskHandle();

  if (!master_command_queue_normal) {
    master_command_queue_normal = xQueueCreateStatic(
      MASTER_COMMAND_QUEUE_LENGTH,
      sizeof(MasterCommandRequest*),
      master_command_queue_normal_storage,
      &master_command_queue_normal_ctrl);
  }

  if (!master_command_queue_critical) {
    master_command_queue_critical = xQueueCreateStatic(
      MASTER_COMMAND_QUEUE_LENGTH,
      sizeof(MasterCommandRequest*),
      master_command_queue_critical_storage,
      &master_command_queue_critical_ctrl);
  }
}

static bool master_command_submit(MasterCommandRequest& r) {
  // Startup/internal loop calls remain direct and cannot deadlock.
  if (!master_command_queue_normal ||
      xTaskGetCurrentTaskHandle() == master_command_loop_task) {
    master_command_execute(r);
    return r.result;
  }

  StaticSemaphore_t done_storage;
  r.done = xSemaphoreCreateBinaryStatic(&done_storage);
  if (!r.done) return false;

  QueueHandle_t q = master_command_is_critical(r)
    ? master_command_queue_critical
    : master_command_queue_normal;

  MasterCommandRequest* ptr = &r;
  if (xQueueSend(q, &ptr, pdMS_TO_TICKS(MASTER_COMMAND_QUEUE_SEND_MS)) != pdTRUE) {
    master_command_queue_rejects++;
    return false;
  }

  const UBaseType_t depth = uxQueueMessagesWaiting(q);
  if (depth > master_command_queue_max_depth) {
    master_command_queue_max_depth =
      depth > 255 ? 255 : (uint8_t)depth;
  }

  // The request lives on this stack until the main loop completes it.
  // Waiting indefinitely here is intentional: returning early would invalidate
  // the queued pointer. A dead main loop already means the controller itself is
  // unhealthy; normal requests complete in a few milliseconds plus RS485 time.
  xSemaphoreTake(r.done, portMAX_DELAY);
  return r.result;
}

static bool master_command_process_one(QueueHandle_t q) {
  if (!q) return false;
  MasterCommandRequest* r = nullptr;
  if (xQueueReceive(q, &r, 0) != pdTRUE || !r) return false;

  master_command_execute(*r);
  master_command_queue_processed++;
  if (r->done) xSemaphoreGive(r->done);
  return true;
}

static void master_command_queue_process() {
  // Safety/off requests always win. In normal operation these queues are almost
  // always empty because WebServer and MQTT each serialize their own callbacks.
  for (uint8_t i = 0; i < 4; ++i) {
    if (!master_command_process_one(master_command_queue_critical)) break;
  }

  // Limit normal RS485-producing commands per loop so JBC polling and display
  // status do not get a large latency burst if several clients submit at once.
  for (uint8_t i = 0; i < 2; ++i) {
    if (!master_command_process_one(master_command_queue_normal)) break;
  }
}

// ---- Typed wrappers used by Web/MQTT tasks --------------------------------

static bool master_cmd_request_scan(bool with_auto_address, bool prune_missing) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SCAN_REQUEST;
  r.flag1 = with_auto_address;
  r.flag2 = prune_missing;
  return master_command_submit(r);
}

static bool master_cmd_trace_start(uint8_t addr, bool local_trace) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_TRACE_START;
  r.addr = addr;
  r.flag1 = local_trace;
  return master_command_submit(r);
}

static bool master_cmd_trace_stop() {
  MasterCommandRequest r;
  r.type = MASTER_CMD_TRACE_STOP;
  return master_command_submit(r);
}

static bool master_cmd_trace_clear() {
  MasterCommandRequest r;
  r.type = MASTER_CMD_TRACE_CLEAR;
  return master_command_submit(r);
}

static bool master_cmd_probe_module(uint8_t addr) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_PROBE_MODULE;
  r.addr = addr;
  return master_command_submit(r);
}

static bool master_cmd_set_module_label(uint8_t addr, uint64_t uid, const char* label) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_MODULE_LABEL;
  r.addr = addr;
  r.qw1 = uid;
  r.s1 = label;
  return master_command_submit(r);
}

static bool master_cmd_set_io_alias(uint8_t addr, uint8_t channel, const char* alias) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_IO_ALIAS;
  r.addr = addr;
  r.a = channel;
  r.s1 = alias;
  return master_command_submit(r);
}

static bool master_cmd_apply_control_settings(
    uint8_t suction,
    uint16_t select_flow,
    uint16_t delay_work,
    uint16_t delay_stand,
    bool stand_intakes,
    bool continuous,
    bool persist) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_APPLY_CONTROL_SETTINGS;
  r.a = suction;
  r.w1 = select_flow;
  r.w2 = delay_work;
  r.w3 = delay_stand;
  r.flag1 = stand_intakes;
  r.flag2 = continuous;
  r.flag3 = persist;
  return master_command_submit(r);
}

static bool master_cmd_set_afterrun_power_profile(bool enabled, uint16_t power, bool persist) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_AFTERRUN_PROFILE;
  r.flag1 = enabled;
  r.w1 = power;
  r.flag2 = persist;
  return master_command_submit(r);
}

static bool master_cmd_persist_control_settings() {
  MasterCommandRequest r;
  r.type = MASTER_CMD_PERSIST_CONTROL_SETTINGS;
  return master_command_submit(r);
}

static bool master_cmd_set_io_output(uint8_t addr, uint16_t mask, uint16_t value) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_IO_OUTPUT;
  r.addr = addr;
  r.w1 = mask;
  r.w2 = value;
  return master_command_submit(r);
}

static bool master_cmd_set_module_power(uint8_t addr, uint16_t power) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_MODULE_POWER;
  r.addr = addr;
  r.w1 = power;
  return master_command_submit(r);
}

static bool master_cmd_set_module_output(uint8_t addr, bool enabled, uint16_t power) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_MODULE_OUTPUT;
  r.addr = addr;
  r.flag1 = enabled;
  r.w1 = power;
  return master_command_submit(r);
}

static bool master_cmd_calibrate_fanio_filter(
    uint8_t addr, uint8_t action, uint16_t warn_raw, uint16_t full_raw) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_CALIBRATE_FANIO_FILTER;
  r.addr = addr;
  r.a = action;
  r.w1 = warn_raw;
  r.w2 = full_raw;
  return master_command_submit(r);
}

static bool master_cmd_set_weller_speed(uint8_t addr, uint8_t percent) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_WELLER_SPEED;
  r.addr = addr;
  r.a = percent;
  return master_command_submit(r);
}

static bool master_cmd_reset_weller_filter(uint8_t addr) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_RESET_WELLER_FILTER;
  r.addr = addr;
  return master_command_submit(r);
}

static bool master_cmd_set_weller_filter_runtime(uint8_t addr, uint16_t minutes) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_WELLER_FILTER_RUNTIME;
  r.addr = addr;
  r.w1 = minutes;
  return master_command_submit(r);
}

static bool master_cmd_set_display_settings(
    uint8_t addr, uint8_t brightness, uint8_t language,
    uint8_t theme, uint8_t screensaver_min) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_DISPLAY_SETTINGS;
  r.addr = addr;
  r.a = brightness;
  r.b = language;
  r.c = theme;
  r.d = screensaver_min;
  return master_command_submit(r);
}

static bool master_cmd_set_universal_profile(
    uint8_t addr,
    const char* profile,
    const char* station,
    uint32_t baud,
    const char* frame,
    const char* protocol,
    const char* checksum,
    const char* line_end,
    const char* profile_text) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_UNIVERSAL_PROFILE;
  r.addr = addr;
  r.s1 = profile;
  r.s2 = station;
  r.dw1 = baud;
  r.s3 = frame;
  r.s4 = protocol;
  r.s5 = checksum;
  r.s6 = line_end;
  r.s7 = profile_text;
  return master_command_submit(r);
}

static bool master_cmd_read_universal_profile(
    uint8_t addr, char* out, size_t out_len,
    uint32_t* out_crc, bool* out_truncated) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_READ_UNIVERSAL_PROFILE;
  r.addr = addr;
  r.out_text = out;
  r.out_len = out_len;
  r.out_crc = out_crc;
  r.out_truncated = out_truncated;
  return master_command_submit(r);
}

static bool master_cmd_set_universal_entity(
    uint8_t addr, uint8_t entity_id, const uint8_t* data, uint8_t len) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_UNIVERSAL_ENTITY;
  r.addr = addr;
  r.a = entity_id;
  r.bytes = data;
  r.len = len;
  return master_command_submit(r);
}

static bool master_cmd_set_jbc_usb_station_name(uint8_t addr, const char* name) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_JBC_USB_STATION_NAME;
  r.addr = addr;
  r.s1 = name;
  return master_command_submit(r);
}

static bool master_cmd_set_jbc_usb_config(
    uint8_t addr, const uint8_t* data, uint8_t len) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_JBC_USB_CONFIG;
  r.addr = addr;
  r.bytes = data;
  r.len = len;
  return master_command_submit(r);
}

static bool master_cmd_set_main_input(
    uint8_t source_type, uint8_t source_addr, uint8_t source_bit,
    bool persist = true) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_MAIN_INPUT;
  r.a = source_type;
  r.b = source_addr;
  r.c = source_bit;
  r.flag1 = persist;
  return master_command_submit(r);
}

static bool master_cmd_set_preferred_output(uint8_t addr) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_PREFERRED_OUTPUT;
  r.addr = addr;
  return master_command_submit(r);
}

static bool master_cmd_set_jbc_input_enabled(bool enabled) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_JBC_INPUT_ENABLED;
  r.flag1 = enabled;
  return master_command_submit(r);
}

static bool master_cmd_set_io_input_route(uint8_t addr, uint8_t bit, bool enabled) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_IO_INPUT_ROUTE;
  r.addr = addr;
  r.a = bit;
  r.flag1 = enabled;
  return master_command_submit(r);
}

static bool master_cmd_set_input_rule(
    uint8_t index, const MasterScheduler::InputActionRule& rule) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_INPUT_RULE;
  r.a = index;
  r.rule = rule;
  return master_command_submit(r);
}

static bool master_cmd_module_reboot(uint8_t addr) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_MODULE_REBOOT;
  r.addr = addr;
  return master_command_submit(r);
}

static bool master_cmd_set_led_config(bool enabled, uint8_t brightness_pct) {
  MasterCommandRequest r;
  r.type = MASTER_CMD_SET_LED_CONFIG;
  r.flag1 = enabled;
  r.a = brightness_pct;
  return master_command_submit(r);
}
