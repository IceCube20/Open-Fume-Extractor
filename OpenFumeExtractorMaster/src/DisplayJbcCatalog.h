#pragma once
#include "bus/OfeJbcDetailWire.h"
#include "JbcUsbHaControls.h"
#include "JbcUsbHaCounters.h"
extern bool master_developer_mode_enabled_for_modules();

namespace ofe_jbc_catalog {
using namespace ofe_jbc_detail;
inline uint32_t revision(const ModuleRecord& m) {
  uint32_t h = 2166136261UL;
  auto mix = [&](const void* ptr, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(ptr);
    while (size--) { h ^= *p++; h *= 16777619UL; }
  };
  mix(&m.uid, sizeof(m.uid));
  mix(m.jbc_usb_model_raw, sizeof(m.jbc_usb_model_raw));
  mix(m.jbc_usb_device_id, sizeof(m.jbc_usb_device_id));
  mix(&m.jbc_usb_command_protocol, sizeof(m.jbc_usb_command_protocol));
  const bool linked = jbc_usb_core_state(m).linked;
  mix(&linked, sizeof(linked));
  return h;
}
inline void duration(char* out, size_t size, uint64_t minutes) {
  snprintf(out, size, "%llud %lluh %llum", (unsigned long long)(minutes / 1440),
    (unsigned long long)((minutes / 60) % 24), (unsigned long long)(minutes % 60));
}
// Stream one requested entry: no per-module catalogue allocation or bus reads.
inline uint8_t entry(const ModuleRecord& m, uint8_t section, uint8_t port, bool de,
                     uint8_t wanted, Row& output) {
  const JbcUsbCoreState c = jbc_usb_core_state(m);
  uint8_t count = 0;
  auto add = [&](const char* en, const char* german, const char* value) {
    if (count == wanted) { output = Row(); copy(output.label, sizeof(output.label), de ? german : en); copy(output.text, sizeof(output.text), value); }
    ++count;
  };
  auto number = [&](const char* en, const char* german, int64_t value, const char* unit = "") {
    char text[64]; snprintf(text, sizeof(text), "%lld%s%s", (long long)value, *unit ? " " : "", unit); add(en, german, text);
  };
  auto boolean = [&](const char* en, const char* german, bool value) { add(en, german, value ? (de ? "An" : "On") : (de ? "Aus" : "Off")); };
  auto seconds = [&](const char* en, const char* german, uint32_t value) {
    char text[32]; snprintf(text, sizeof(text), "%lu min %02lu s", (unsigned long)(value / 60), (unsigned long)(value % 60)); add(en, german, text);
  };
  auto percent = [&](const char* en, const char* german, uint16_t permille) {
    char text[24]; snprintf(text, sizeof(text), "%u.%u %%", permille / 10, permille % 10); add(en, german, text);
  };
  if (section == STATION) {
    add("Station name", "Stationsname", m.jbc_usb_station_name);
    add("Model", "Modell", m.jbc_usb_model_raw);
    add("Device family", "Stationsklasse", jbc_usb_core_family_name(c.family));
    boolean("JBC connected", "JBC verbunden", c.linked);
    number("Ports", "Ports", c.port_count);
    boolean("Work", "Work", c.work_active); boolean("Stand", "Stand", c.stand_active);
    add("Connection mode", "Verbindungsmodus", c.connect_mode_valid ? (c.control_mode ? "CONTROL" : "MONITOR") : "-");
    const char* error = jbc_usb_core_known_station_error_name(c.station_error);
    if (c.station_error_valid) { if (error) add("Station error", "Stationsfehler", error); else number("Station error", "Stationsfehler", c.station_error); }
    add("Software", "Software", m.jbc_usb_sw_version); add("Hardware", "Hardware", m.jbc_usb_hw_version);
    add("Model type", "Modelltyp", m.jbc_usb_model_type); number("Model version", "Modellversion", m.jbc_usb_model_version);
    char id[65] = {};
    for (uint8_t i = 0; i < m.jbc_usb_device_id_len && i < 32; ++i) snprintf(id + 2 * i, sizeof(id) - 2 * i, "%02X", m.jbc_usb_device_id[i]);
    add("Device ID", "Device ID", id);
  } else if (section == PORT && port < c.port_count && port < 4) {
    const JbcUsbCorePort& p = c.ports[port];
    add("State", "Status", p.valid ? jbc_usb_core_state_name(p.state) : (de ? "Keine Daten" : "No data"));
    if (p.valid) {
      const uint8_t tool = jbc_usb_core_generic_tool_id(c.family, m.jbc_usb_model, p.tool);
      const char* name = jbc_usb_core_known_tool_name(tool);
      if (c.family == JBC_USB_CORE_SOLD || c.family == JBC_USB_CORE_HA) {
        if (name) add("Tool", "Werkzeug", name); else number("Tool", "Werkzeug", tool);
        number("Temperature", "Temperatur", p.actual_temp / 9, "\302\260C");
        if (p.selected_temp_valid) number("Set temperature", "Solltemperatur", p.selected_temp / 9, "\302\260C");
        percent("Power", "Leistung", p.power_permille);
        if (c.family == JBC_USB_CORE_SOLD) {
          add("Next mode", "N\303\244chster Modus", p.future_mode == 'S' ? "SLEEP" : p.future_mode == 'H' ? "HIBERNATION" : p.future_mode == 'N' || p.future_mode == 0 ? "NONE" : "-");
          seconds("Time to mode change", "Zeit bis Moduswechsel", p.transition_countdown_s);
        } else {
          percent("Flow", "Luftstrom", p.flow_permille);
          if (p.selected_flow_valid) percent("Set flow", "Soll-Luftstrom", p.selected_flow_permille);
          seconds("Time to stop", "Zeit bis Stopp", (uint32_t(p.time_to_stop) + 9) / 10);
        }
      }
      const char* error = jbc_usb_core_known_tool_error_name(jbc_usb_core_tool_error_code(c.family, p.error));
      if (error) add("Tool error", "Werkzeugfehler", error); else number("Tool error", "Werkzeugfehler", p.error);
    }
    if (c.family == JBC_USB_CORE_CL) {
      add("Cleaning mode", "Reinigungsmodus", p.mode == 1 ? "DETECTION" : p.mode == 2 ? "CONTINUOUS" : p.mode == 4 ? "CALIBRATING" : "-");
      if (p.motors_valid) boolean("Motors", "Motoren", p.motors_on);
      if (p.door_valid) boolean("Door open", "T\303\274r offen", p.door_open);
    } else if (c.family == JBC_USB_CORE_PH) {
      for (uint8_t tc = 0; tc < 4; ++tc) {
        char label[48]; snprintf(label, sizeof(label), "TC%u %s", tc + 1, de ? "Temperatur" : "Temperature");
        if (c.ph_tc[tc].actual_valid) number(label, label, c.ph_tc[tc].actual_temp / 9, "\302\260C");
        snprintf(label, sizeof(label), "TC%u %s", tc + 1, de ? "Solltemperatur" : "Set temperature");
        if (c.ph_tc[tc].selected_valid) number(label, label, c.ph_tc[tc].selected_temp / 9, "\302\260C");
      }
      if (p.heater_valid) boolean("Heater", "Heizung", p.heater_on);
      percent("Heater power", "Heizleistung", p.power_permille);
      if (p.selected_power_valid) percent("Set power", "Sollleistung", p.selected_power_permille);
      if (p.active_zones_valid) number("Active zones", "Aktive Zonen", p.active_zones);
      seconds("Time to stop", "Zeit bis Stopp", (uint32_t(p.time_to_stop) + 9) / 10);
    } else if (c.family == JBC_USB_CORE_FE) {
      add("Suction level", "Absaugstufe", p.mode == 0 ? (de ? "Hoch" : "High") : p.mode == 1 ? (de ? "Mittel" : "Medium") :
        p.mode == 2 ? (de ? "Niedrig" : "Low") : p.mode == 3 ? (de ? "Benutzer" : "Custom") : "-");
      if (c.continuous_valid) boolean("Continuous", "Dauerabsaugung", c.continuous_on);
      if (p.intake_work_valid) boolean("Work intake", "Work Ansaugung", p.intake_work_on);
      if (p.intake_stand_valid) boolean("Stand intake", "Stand Ansaugung", p.intake_stand_on);
      number("Work stop delay (raw)", "Work Stoppzeit (raw)", p.fe_time_to_stop_work);
      number("Stand stop delay (raw)", "Stand Stoppzeit (raw)", p.fe_time_to_stop_stand);
    } else if (c.family == JBC_USB_CORE_SF) {
      if (p.sf_feeding_valid) boolean("Feeding", "Zinnzufuhr", p.sf_feeding);
      if (p.sf_tool_enabled_valid) boolean("Tool enabled", "Werkzeug aktiviert", p.sf_tool_enabled);
      number("Program", "Programm", p.sf_selected_program);
      if (p.sf_speed_valid) { char text[32]; snprintf(text, sizeof(text), "%u.%u mm/s", p.sf_speed_tenth_mm_s / 10, p.sf_speed_tenth_mm_s % 10); add("Speed", "Geschwindigkeit", text); }
      if (p.sf_length_valid) { char text[32]; snprintf(text, sizeof(text), "%u.%u mm", p.sf_length_tenth_mm / 10, p.sf_length_tenth_mm % 10); add("Length", "L\303\244nge", text); }
    }
  } else if (section == COUNTERS && port < c.port_count && port < 4) {
    for (size_t i = 0; i < HA_JBC_COUNTER_COUNT; ++i) {
      const auto& def = HA_JBC_COUNTERS[i];
      if (!ha_jbc_counter_supported(m.jbc_usb_ports[port], c.family, def)) continue;
      char text[64]; const uint64_t value = ha_jbc_counter_value(m.jbc_usb_ports[port], def);
      if (def.unit) duration(text, sizeof(text), value); else snprintf(text, sizeof(text), "%llu", (unsigned long long)value);
      add(def.en, def.de, text);
    }
  } else if (section == SETTINGS) {
    for (uint8_t id = 0; id < HA_JBC_CONTROL_COUNT; ++id) {
      if (id < 64 && id / 16 != port) continue;
      HaJbcControl ctl;
      if (!ha_jbc_control(m, id, de, ctl)) continue;
      // A preheater is one station port with up to four thermocouples.
      // Keep all its TC setpoints reachable under that station port.
      if (c.family == JBC_USB_CORE_PH ? port != 0 : ctl.port != port) continue;
      if (!isfinite(ctl.minimum) || !isfinite(ctl.maximum) || ctl.minimum * 10 < INT16_MIN ||
          ctl.maximum * 10 > INT16_MAX) continue;
      if (count == wanted) {
        output = Row(); output.id = id;
        const bool valid = ctl.valid && isfinite(ctl.value) && ctl.value >= ctl.minimum && ctl.value <= ctl.maximum;
        output.writable = valid;
        output.kind = !strcmp(ctl.component, "switch") ? SWITCH : !strcmp(ctl.component, "select") ? SELECT : NUMBER;
        copy(output.label, sizeof(output.label), ctl.name);
        output.value = valid ? int16_t(lroundf(ctl.value * 10)) : 0;
        output.minimum = int16_t(ceilf(ctl.minimum * 10)); output.maximum = int16_t(floorf(ctl.maximum * 10));
        output.step = int16_t(lroundf(ctl.step * 10)); if (output.step < 1) output.step = 1;
        if (!valid) copy(output.text, sizeof(output.text), "-");
        else if (output.kind == SWITCH) copy(output.text, sizeof(output.text), ctl.value ? (de ? "An" : "On") : (de ? "Aus" : "Off"));
        else if (output.kind == SELECT) snprintf(output.text, sizeof(output.text), "Level %u", unsigned(ctl.value) + 1);
        else snprintf(output.text, sizeof(output.text), "%.1f %s", double(ctl.value), ctl.unit ? ctl.unit : "");
      }
      ++count;
    }
  } else if (section == DIAGNOSTICS) {
    char version[32]; snprintf(version, sizeof(version), "%u.%u.%u%s", m.fw_major, m.fw_minor, m.fw_patch, m.fw_suffix);
    add("Module firmware", "Modul-Firmware", version);
    char serial[17]; snprintf(serial, sizeof(serial), "%016llX", (unsigned long long)m.uid);
    add("Module serial number", "Modul-Seriennummer", serial);
    add("Module transport", "Modul-Verbindung", "RS485");
    boolean("Power saving", "Energiesparen", m.eco_mode);
    number("CPU", "CPU", m.module_cpu_load_pct, "%");
    char uptime[64]; duration(uptime, sizeof(uptime), m.module_uptime_s / 60); add("Uptime", "Laufzeit", uptime);
    if (master_developer_mode_enabled_for_modules()) {
      number("Free heap", "Freier Heap", m.module_heap_free / 1024, "KiB");
      number("Maximum loop time", "Maximale Loopzeit", m.module_loop_max_ms, "ms");
    }
    add("Protocol", "Protokoll", m.jbc_usb_protocol_text);
    number("USB RX", "USB RX", m.jbc_usb_usb_rx_bytes, "B"); number("USB TX", "USB TX", m.jbc_usb_usb_tx_bytes, "B");
    number("RX frames", "RX Frames", m.jbc_usb_rx_frames); number("TX frames", "TX Frames", m.jbc_usb_tx_frames);
    number("USB errors", "USB Fehler", m.jbc_usb_usb_errors); number("BCC errors", "BCC Fehler", m.jbc_usb_bcc_errors);
    number("Frame errors", "Frame Fehler", m.jbc_usb_frame_errors); number("Decode errors", "Dekodierfehler", m.jbc_usb_decode_errors);
    number("Handshake errors", "Handshake-Fehler", m.jbc_usb_handshake_errors);
    if (m.jbc_usb_cp_diag_valid) {
      number("UART baud", "UART Baud", m.jbc_usb_cp_baud); number("UART errors", "UART Fehler", m.jbc_usb_cp_comm_errors);
      number("Input queue", "Eingangspuffer", m.jbc_usb_cp_in_queue); number("Output queue", "Ausgangspuffer", m.jbc_usb_cp_out_queue);
    }
  }
  return count;
}
}
