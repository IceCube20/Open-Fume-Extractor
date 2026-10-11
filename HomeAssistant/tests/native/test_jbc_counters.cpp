#include <cassert>
#include <cstdio>
#include "JbcUsbHaCounters.h"
#include "JbcUsbHaControls.h"
#include "ModulePowerSaveState.h"
#include "HaEntityIcons.h"
namespace jbc_rs485 {
void put_u16_le(uint8_t* out, uint16_t value) { out[0] = value & 255; out[1] = value >> 8; }
uint16_t get_u16_le(const uint8_t* in) { return in[0] | (uint16_t(in[1]) << 8); }
}

int main() {
  using namespace jbc_rs485;
  ModuleRecord m;
  for (uint8_t type = 1; type <= 9; ++type) {
    m.type = type; m.online = true; m.telemetry_valid = true;
    m.eco_mode = true; m.light_sleep = false; m.eco_since_ms = 1000; m.module_uptime_s = 100;
    assert(!module_update_uptime(m, 100) && m.eco_mode);
    assert(!module_update_uptime(m, 101) && m.eco_mode);
    assert(module_update_uptime(m, 0) && !m.eco_mode && !m.light_sleep && !m.eco_since_ms);
    assert(m.online && m.module_uptime_s == 0);
    assert(!module_update_uptime(m, 1));
    m.telemetry_valid = false; m.module_uptime_s = 100;
    assert(!module_update_uptime(m, 0));
  }
  m.telemetry_valid = true; m.eco_mode = true; m.module_uptime_s = UINT32_MAX;
  assert(module_update_uptime(m, 0) && !m.eco_mode);
  assert(!strcmp(ha_entity_icon("feedback_power", "sensor", "%"), "mdi:fan"));
  for (int channel = 1; channel <= 8; ++channel) {
    char key[8];
    snprintf(key, sizeof(key), "in%d", channel);
    assert(!strcmp(ha_entity_icon(key, "binary_sensor"), "mdi:import"));
    snprintf(key, sizeof(key), "out%d", channel);
    assert(!strcmp(ha_entity_icon(key, "switch"), "mdi:export"));
  }
  assert(!strcmp(ha_entity_icon("jbc_p4_temp_c", "sensor", "°C"), "mdi:thermometer"));
  assert(!strcmp(ha_entity_icon("jbc_tc1_set_temp_c", "sensor"), "mdi:thermometer"));
  for (const auto& icon : HA_ENTITY_ICONS) assert(icon.key[0] && !strncmp(icon.icon, "mdi:", 4));
  m.type = MODULE_JBC_USB; m.online = true; m.jbc_link_flags = 1;
  strcpy(m.jbc_usb_model, "DDE"); m.jbc_usb_port_count = 4; m.jbc_usb_command_protocol = 2;
  for (auto& p : m.jbc_usb_ports) {
    p.valid = true; p.detail_value_flags = 0x11; p.selected_temp = 3150;
    p.levels_on = 1; p.selected_level = 1;
    for (int level = 0; level < 3; ++level) { p.level_on[level] = 1; p.level_temp[level] = (250 + level * 50) * 9; }
  }
  HaJbcControl control; uint8_t payload[23] = {}, len = 0;
  assert(ha_jbc_control(m, 0, false, control) && control.valid && control.value == 350);
  assert(!ha_jbc_control(m, 1, false, control));
  assert(ha_jbc_control(m, 48, true, control) && !strcmp(control.name, "Port 4 Solltemperatur"));
  assert(ha_jbc_control_payload(m, 5, 270, payload, len) && len == 23);
  assert(get_u16_le(payload + 5) == 2430 && get_u16_le(payload + 7) == 2700 && get_u16_le(payload + 9) == 3150);
  assert(ha_jbc_control_payload(m, 8, 0, payload, len) && payload[4] == 5 && payload[2] == 1);
  assert(ha_jbc_control_payload(m, 2, 0, payload, len) && payload[2] == 0 && payload[4] == 7);
  assert(!ha_jbc_control_payload(m, 3, 1.5f, payload, len));
  assert(!ha_jbc_control_payload(m, 0, 501, payload, len));
  assert(!ha_jbc_control_payload(m, 0, NAN, payload, len));
  m.jbc_link_flags = 0; assert(!ha_jbc_control_payload(m, 0, 350, payload, len)); m.jbc_link_flags = 1;
  m.jbc_usb_ports[0].detail_value_flags = 1; assert(!ha_jbc_control_payload(m, 5, 270, payload, len));
  strcpy(m.jbc_usb_model, "JTSE"); strcpy(m.jbc_usb_model_type, "CAP"); m.jbc_usb_model_version = 1;
  m.jbc_usb_ports[0].ha_value_flags = 0x406;
  assert(ha_jbc_control(m, 1, false, control)); assert(!ha_jbc_control(m, 2, false, control));
  assert(ha_jbc_control_payload(m, 1, 55.5f, payload, len) && len == 4 && get_u16_le(payload + 2) == 555);
  m.jbc_usb_model_version = 0;
  assert(ha_jbc_control_payload(m, 14, 65.5f, payload, len) && len == 23);
  assert(get_u16_le(payload + 15) == 655 && get_u16_le(payload + 9) == 3150);
  assert(ha_jbc_control_payload(m, 15, 150, payload, len) && len == 23);
  assert(get_u16_le(payload + 21) == 1350 && get_u16_le(payload + 9) == 3150);
  strcpy(m.jbc_usb_model, "CLM");
  for (uint8_t id = 0; id < HA_JBC_CONTROL_COUNT; ++id) assert(!ha_jbc_control(m, id, false, control));
  strcpy(m.jbc_usb_model, "PHBE"); m.jbc_usb_ph_tc[0].flags = 8; m.jbc_usb_ph_tc[0].selected_temp = 900;
  assert(ha_jbc_control_payload(m, 64, 150, payload, len) && get_u16_le(payload + 2) == 1350);
  strcpy(m.jbc_usb_model, "F2"); m.jbc_usb_fe_service_flags = 4; m.jbc_usb_fe_selected_flow_x_mil = 515;
  assert(ha_jbc_control_payload(m, 68, 60, payload, len) && payload[0] == JBC_USB_CONFIG_SELECTED_FLOW && get_u16_le(payload + 2) == 600);
  static_assert(HA_JBC_COUNTER_COUNT == 73, "Update coverage when extending the catalogue");
  for (size_t index = 0; index < HA_JBC_COUNTER_COUNT; ++index) {
    const auto& def = HA_JBC_COUNTERS[index];
    JbcUsbPortState port{};
    assert(def.key && *def.key && def.de && *def.de && def.en && *def.en);
    assert(def.value_size == 4 || def.value_size == 8);
    assert(def.value_offset + def.value_size <= sizeof(port));
    assert(def.flags_offset + sizeof(uint16_t) <= sizeof(port));
    assert(def.flags_mask != 0);
    assert(!ha_jbc_counter_supported(port, def.family, def));

    auto* bytes = reinterpret_cast<uint8_t*>(&port);
    memcpy(bytes + def.flags_offset, &def.flags_mask, sizeof(def.flags_mask));
    assert(ha_jbc_counter_supported(port, def.family, def));
    assert(!ha_jbc_counter_supported(port, def.family ^ 0xffU, def));
    const uint64_t value = def.value_size == 8 ? 0xfedcba9876543210ULL : 0xfedcba98ULL;
    memcpy(bytes + def.value_offset, &value, def.value_size);
    assert(ha_jbc_counter_value(port, def) == value);

    if ((def.flags_mask & (def.flags_mask - 1U)) != 0) {
      const uint16_t incomplete = def.flags_mask & (def.flags_mask - 1U);
      memcpy(bytes + def.flags_offset, &incomplete, sizeof(incomplete));
      assert(!ha_jbc_counter_supported(port, def.family, def));
    }
  }
  puts("JBC HA catalogue: station controls/payloads and all 73 counter cache offsets passed");
}
