#pragma once
#include "ModuleRegistry.h"
#include <math.h>

// Stable IDs: 16 controls per port, then four PH thermocouples and FE flow.
static constexpr uint8_t HA_JBC_CONTROL_COUNT = 69;
struct HaJbcControl {
  uint8_t id = 0, port = 0, field = 0, level = 0;
  const char* component = "number";
  const char* unit = "\u00b0C";
  const char* icon = "mdi:thermometer";
  char name[96] = {};
  float value = 0, minimum = 50, maximum = 500, step = 1;
  uint16_t divisor = 9;
  bool valid = false;
};

static inline bool ha_jbc_control(const ModuleRecord& m, uint8_t id, bool german, HaJbcControl& out) {
  using namespace jbc_rs485;
  if (id >= HA_JBC_CONTROL_COUNT || m.type != MODULE_JBC_USB) return false;
  const JbcUsbCoreState core = jbc_usb_core_state(m);
  out = HaJbcControl(); out.id = id;
  const bool hot_air = core.family == JBC_USB_CORE_HA;
  const bool sold = core.family == JBC_USB_CORE_SOLD;
  const bool ph = core.family == JBC_USB_CORE_PH;
  const bool fe = core.family == JBC_USB_CORE_FE;
  uint16_t low = sold ? m.jbc_usb_sold_min_temp : (hot_air ? m.jbc_usb_ha_min_temp : m.jbc_usb_ph_min_temp);
  uint16_t high = sold ? m.jbc_usb_sold_max_temp : (hot_air ? m.jbc_usb_ha_max_temp : m.jbc_usb_ph_max_temp);
  out.minimum = low ? low / 9.0f : 50; out.maximum = high ? high / 9.0f : 500;
  if (out.maximum < out.minimum) return false;
  if (id >= 64) {
    if (id == 68) {
      if (!fe || m.jbc_usb_command_protocol != 2) return false;
      out.field = 1; out.divisor = 10; out.unit = "%"; out.icon = "mdi:fan";
      out.minimum = 20; out.maximum = 100; out.step = 10;
      out.value = 20 + (m.jbc_usb_fe_selected_flow_x_mil - 450.0f) * 80 / 130;
      out.valid = (m.jbc_usb_fe_service_flags & 4U) != 0;
      snprintf(out.name, sizeof(out.name), "%s", german ? "Soll-Luftstrom" : "Set flow");
    } else {
      if (!ph || m.jbc_usb_command_protocol != 2) return false;
      out.port = id - 64;
      const JbcUsbPhTcState& tc = m.jbc_usb_ph_tc[out.port];
      if (!tc.flags && out.port >= 2) return false;
      out.value = tc.selected_temp / 9.0f; out.valid = (tc.flags & 8U) != 0;
      snprintf(out.name, sizeof(out.name), "TC%u %s", out.port + 1U, german ? "Solltemperatur" : "Set temperature");
    }
  } else {
    if (!sold && !hot_air) return false;
    out.port = id / 16; out.field = id % 16;
    if (out.port >= core.port_count || out.port >= 4) return false;
    const JbcUsbPortState& p = m.jbc_usb_ports[out.port];
    const char* label = nullptr;
    if (out.field == 0) {
      out.value = p.selected_temp / 9.0f;
      out.valid = sold ? (p.detail_value_flags & 1U) != 0 : (p.ha_value_flags & 2U) != 0;
      label = german ? "Solltemperatur" : "Set temperature";
    } else if (out.field == 1) {
      if (!hot_air || m.jbc_usb_command_protocol != 2) return false;
      out.value = p.selected_flow_permille / 10.0f; out.valid = (p.ha_value_flags & 4U) != 0;
      label = german ? "Soll-Luftstrom" : "Set flow";
    } else {
      if (hot_air && !strcmp(m.jbc_usb_model, "JTSE") && !strcmp(m.jbc_usb_model_type, "CAP") && m.jbc_usb_model_version >= 1) return false;
      out.valid = sold ? (p.detail_value_flags & 0x10U) != 0 : (p.ha_value_flags & 0x400U) != 0;
      if (out.field == 2 || out.field == 3) {
        out.value = out.field == 2 ? (p.levels_on ? 1 : 0) : p.selected_level;
        out.component = out.field == 2 ? "switch" : "select";
        out.unit = nullptr; out.icon = "mdi:format-list-numbered";
        out.minimum = 0; out.maximum = out.field == 2 ? 1 : 2;
        label = out.field == 2 ? (german ? "Temperaturlevel aktiv" : "Temperature levels enabled") : (german ? "Temperaturlevel" : "Temperature level");
      } else {
        out.level = (out.field - 4) / 4;
        const uint8_t kind = (out.field - 4) % 4;
        if (sold && kind >= 2) return false;
        if (kind == 0) {
          out.component = "switch"; out.unit = nullptr; out.minimum = 0; out.maximum = 1;
          out.icon = "mdi:toggle-switch"; out.value = p.level_on[out.level] ? 1 : 0;
          label = german ? "aktiv" : "enabled";
        } else if (kind == 1) { out.value = p.level_temp[out.level] / 9.0f; label = german ? "Temperatur" : "Temperature"; }
        else if (kind == 2) { out.value = p.level_flow_permille[out.level] / 10.0f; label = german ? "Luftstrom" : "Flow"; }
        else { out.value = p.level_ext_temp[out.level] / 9.0f; label = "External TC"; }
        snprintf(out.name, sizeof(out.name), "Port %u Level %u %s", out.port + 1U, out.level + 1U, label);
      }
    }
    if (out.field < 4) snprintf(out.name, sizeof(out.name), "Port %u %s", out.port + 1U, label);
    if (out.field == 1 || (out.field >= 4 && (out.field - 4) % 4 == 2)) {
      out.unit = "%"; out.icon = "mdi:fan"; out.divisor = 10; out.step = 0.1f;
      out.minimum = m.jbc_usb_ha_min_flow ? m.jbc_usb_ha_min_flow / 10.0f : 10;
      out.maximum = m.jbc_usb_ha_max_flow ? m.jbc_usb_ha_max_flow / 10.0f : 100;
    }
    out.valid = out.valid && p.valid;
  }
  out.valid = out.valid && core.valid && core.linked && m.online;
  if (!strcmp(out.component, "select") && out.value > 2) out.valid = false;
  if (out.value < 0 || out.value >= 65535.0f / out.divisor) out.valid = false;
  return true;
}

static inline bool ha_jbc_control_payload(const ModuleRecord& m, uint8_t id, float value, uint8_t* payload, uint8_t& len) {
  using namespace jbc_rs485;
  HaJbcControl control;
  if (!ha_jbc_control(m, id, false, control) || !control.valid || !isfinite(value) ||
      value < control.minimum || value > control.maximum) return false;
  if ((!strcmp(control.component, "switch") || !strcmp(control.component, "select")) && floorf(value) != value) return false;
  const uint16_t raw = (uint16_t)lroundf(value * control.divisor);
  if (id >= 64 || control.field < 2) {
    payload[0] = control.field == 1 ? JBC_USB_CONFIG_SELECTED_FLOW : JBC_USB_CONFIG_SELECTED_TEMP;
    payload[1] = control.port; put_u16_le(payload + 2, raw); len = 4; return true;
  }
  // Level commands carry a full snapshot. Reject incomplete caches rather than
  // replacing unread settings with zero/default values.
  const JbcUsbPortState& p = m.jbc_usb_ports[control.port];
  payload[0] = JBC_USB_CONFIG_LEVELS; payload[1] = control.port;
  payload[2] = p.levels_on ? 1 : 0; payload[3] = p.selected_level <= 2 ? p.selected_level : 0;
  payload[4] = 0;
  for (uint8_t level = 0; level < 3; ++level) {
    if (p.level_on[level]) payload[4] |= 1U << level;
    put_u16_le(payload + 5 + level * 2, p.level_temp[level]);
    put_u16_le(payload + 11 + level * 2, p.level_flow_permille[level]);
    put_u16_le(payload + 17 + level * 2, p.level_ext_temp[level]);
  }
  if (control.field == 2) payload[2] = value ? 1 : 0;
  else if (control.field == 3) payload[3] = (uint8_t)value;
  else {
    const uint8_t kind = (control.field - 4) % 4;
    if (kind == 0) {
      if (value) payload[4] |= 1U << control.level; else payload[4] &= ~(1U << control.level);
    } else put_u16_le(payload + 5 + (kind - 1) * 6 + control.level * 2, raw);
  }
  len = 23; return true;
}
