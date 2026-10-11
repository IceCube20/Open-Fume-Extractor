#pragma once
#include <cstring>

struct HaEntityIcon { const char* key; const char* icon; };
static constexpr HaEntityIcon HA_ENTITY_ICONS[] = {
  {"output", "mdi:fan"}, {"power", "mdi:fan"}, {"feedback_power", "mdi:fan"},
  {"afterrun", "mdi:timer-outline"}, {"modules", "mdi:devices"},
  {"jbc_error", "mdi:alert-circle-outline"}, {"jbc_error_text", "mdi:alert-circle-outline"},
  {"alarm_count", "mdi:bell-alert-outline"}, {"alarm_text", "mdi:bell-alert-outline"},
  {"wifi_rssi", "mdi:wifi"}, {"ip", "mdi:ip-network"}, {"datetime", "mdi:calendar-clock"},
  {"mqtt_state", "mdi:lan-connect"}, {"mqtt_state_code", "mdi:lan-connect"},
  {"heap", "mdi:memory"}, {"heap_min", "mdi:memory"}, {"cpu", "mdi:chip"},
  {"firmware", "mdi:chip"}, {"uptime", "mdi:clock-outline"}, {"loop_max", "mdi:timer-outline"},
  {"transport", "mdi:lan"}, {"serial_number", "mdi:barcode"},
  {"eco", "mdi:sprout"}, {"work", "mdi:soldering-iron"}, {"stand", "mdi:car-brake-parking"},
  {"online", "mdi:lan-connect"}, {"jbc_link", "mdi:lan-connect"}, {"weller_link", "mdi:lan-connect"},
  {"station", "mdi:devices"}, {"device_id", "mdi:identifier"},
  {"jbc_model", "mdi:devices"}, {"jbc_family", "mdi:devices"}, {"jbc_station_name", "mdi:tag-text-outline"},
  {"jbc_port_count", "mdi:counter"}, {"jbc_connect_mode", "mdi:lan"},
  {"state", "mdi:list-status"}, {"jbc_cleaner_mode", "mdi:tune-variant"},
  {"jbc_door", "mdi:door"}, {"jbc_active_zones", "mdi:view-grid-outline"},
  {"jbc_intake_work", "mdi:soldering-iron"}, {"jbc_intake_stand", "mdi:car-brake-parking"},
  {"jbc_suction_level", "mdi:fan-speed-3"}, {"jbc_selected_power", "mdi:fan"},
  {"jbc_program", "mdi:format-list-numbered"}, {"jbc_speed", "mdi:speedometer"},
  {"jbc_length", "mdi:ruler"}, {"jbc_tool_enabled", "mdi:soldering-iron"},
  {"flow_permille", "mdi:fan"}, {"set_flow_permille", "mdi:fan"},
  {"power_permille", "mdi:fan"}, {"temp_c", "mdi:thermometer"}, {"set_temp_c", "mdi:thermometer"},
  {"rpm", "mdi:fan"}, {"feedback_rpm", "mdi:fan"},
  {"fault", "mdi:alert-circle-outline"}, {"output_fault", "mdi:alert-circle-outline"},
  {"in1", "mdi:import"}, {"in2", "mdi:import"}, {"out1", "mdi:export"}, {"out2", "mdi:export"},
  {"in3", "mdi:import"}, {"in4", "mdi:import"}, {"in5", "mdi:import"}, {"in6", "mdi:import"},
  {"in7", "mdi:import"}, {"in8", "mdi:import"},
  {"out3", "mdi:export"}, {"out4", "mdi:export"}, {"out5", "mdi:export"}, {"out6", "mdi:export"},
  {"out7", "mdi:export"}, {"out8", "mdi:export"},
  {"fan", "mdi:fan"}, {"light", "mdi:lightbulb"},
};

static inline const char* ha_entity_icon(const char* key, const char* component, const char* unit = nullptr) {
  if (!std::strncmp(key, "jbc_p", 5) && key[5] >= '1' && key[5] <= '4' && key[6] == '_') key += 7;
  if (!std::strncmp(key, "jbc_tc", 6)) return "mdi:thermometer";
  for (const auto& entry : HA_ENTITY_ICONS) if (!std::strcmp(key, entry.key)) return entry.icon;
  if (unit) {
    if (!std::strcmp(unit, "°C")) return "mdi:thermometer";
    if (!std::strcmp(unit, "KiB")) return "mdi:memory";
    if (!std::strcmp(unit, "dBm")) return "mdi:wifi";
    if (!std::strcmp(unit, "rpm")) return "mdi:fan";
    if (!std::strcmp(unit, "%")) return "mdi:percent";
    if (!std::strcmp(unit, "s") || !std::strcmp(unit, "ms") || !std::strcmp(unit, "min")) return "mdi:timer-outline";
    if (!std::strcmp(unit, "mm")) return "mdi:ruler";
    if (!std::strcmp(unit, "mm/s")) return "mdi:speedometer";
  }
  if (!std::strcmp(component, "switch")) return "mdi:toggle-switch";
  if (!std::strcmp(component, "binary_sensor")) return "mdi:checkbox-marked-circle-outline";
  if (!std::strcmp(component, "select")) return "mdi:format-list-bulleted";
  if (!std::strcmp(component, "number")) return "mdi:tune";
  if (!std::strcmp(component, "button")) return "mdi:gesture-tap-button";
  if (!std::strcmp(component, "text")) return "mdi:form-textbox";
  return "mdi:gauge";
}
