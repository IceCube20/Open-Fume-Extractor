#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ModuleRegistry.h"

// Cache-only catalogue. Append entries: MQTT manifests persist these indices.
// Validity masks match the station-class telemetry; values never trigger discovery.
struct HaJbcCounterDef {
  const char* key;
  const char* de;
  const char* en;
  const char* unit;
  uint16_t value_offset;
  uint16_t flags_offset;
  uint16_t flags_mask;
  uint8_t value_size;
  uint8_t family;
};

#define HA_JBC_COUNTER(field, flags, mask, kind, de, en, unit) \
  {#field, de, en, unit, offsetof(JbcUsbPortState, field), offsetof(JbcUsbPortState, flags), \
   mask, sizeof(((JbcUsbPortState*)nullptr)->field), JBC_USB_CORE_##kind}

static const HaJbcCounterDef HA_JBC_COUNTERS[] = {
  HA_JBC_COUNTER(counter_plug_min, detail_value_flags, 0x0008U, SOLD, "Eingesteckt", "Plugged", "min"),
  HA_JBC_COUNTER(counter_work_min, detail_value_flags, 0x0008U, SOLD, "Work", "Work", "min"),
  HA_JBC_COUNTER(counter_sleep_min, detail_value_flags, 0x0008U, SOLD, "Sleep", "Sleep", "min"),
  HA_JBC_COUNTER(counter_hiber_min, detail_value_flags, 0x0008U, SOLD, "Hibernation", "Hibernation", "min"),
  HA_JBC_COUNTER(counter_idle_min, detail_value_flags, 0x0008U, SOLD, "Idle", "Idle", "min"),
  HA_JBC_COUNTER(counter_sleep_cycles, detail_value_flags, 0x0028U, SOLD, "Sleep-Zyklen", "Sleep cycles", nullptr),
  HA_JBC_COUNTER(counter_desold_cycles, detail_value_flags, 0x0028U, SOLD, "Entlötzyklen", "Desolder cycles", nullptr),
  HA_JBC_COUNTER(sold_partial_plug_min, sold_extra_flags, 0x0001U, SOLD, "Teil Eingesteckt", "Partial Plugged", "min"),
  HA_JBC_COUNTER(sold_partial_work_min, sold_extra_flags, 0x0001U, SOLD, "Teil Work", "Partial Work", "min"),
  HA_JBC_COUNTER(sold_partial_sleep_min, sold_extra_flags, 0x0001U, SOLD, "Teil Sleep", "Partial Sleep", "min"),
  HA_JBC_COUNTER(sold_partial_hiber_min, sold_extra_flags, 0x0001U, SOLD, "Teil Hibernation", "Partial Hibernation", "min"),
  HA_JBC_COUNTER(sold_partial_idle_min, sold_extra_flags, 0x0001U, SOLD, "Teil Idle", "Partial Idle", "min"),
  HA_JBC_COUNTER(sold_partial_sleep_cycles, sold_extra_flags, 0x0002U, SOLD, "Teil Sleep-Zyklen", "Partial Sleep cycles", nullptr),
  HA_JBC_COUNTER(sold_partial_desold_cycles, sold_extra_flags, 0x0002U, SOLD, "Teil Entlötzyklen", "Partial Desolder cycles", nullptr),
  HA_JBC_COUNTER(sold_partial_tin_deliver_cycles, sold_special_counter_flags, 0x0002U, SOLD, "Teil Zinnförderzyklen", "Partial Tin delivery cycles", nullptr),
  HA_JBC_COUNTER(sold_partial_tin_length, sold_special_counter_flags, 0x0002U, SOLD, "Teil Zinnlänge", "Partial Tin length", nullptr),
  HA_JBC_COUNTER(sold_cde_partial_sold_number, sold_special_counter_flags, 0x0008U, SOLD, "Teil Lötungen", "Partial Soldering count", nullptr),
  HA_JBC_COUNTER(sold_cde_partial_energy_delivered, sold_special_counter_flags, 0x0008U, SOLD, "Teil Abgegebene Energie", "Partial Delivered energy", nullptr),
  HA_JBC_COUNTER(sold_cde_partial_sold_total, sold_special_counter_flags, 0x0008U, SOLD, "Teil Lötungen gesamt", "Partial Total soldering count", nullptr),
  HA_JBC_COUNTER(sold_cde_partial_sold_per_min, sold_special_counter_flags, 0x0008U, SOLD, "Teil Lötungen pro Minute", "Partial Soldering per minute", nullptr),
  HA_JBC_COUNTER(sold_cde_partial_sold_ok, sold_special_counter_flags, 0x0008U, SOLD, "Teil Gültige Lötungen", "Partial Successful soldering count", nullptr),
  HA_JBC_COUNTER(ha_counter_plug_min, ha_value_flags, 0x0800U, HA, "Eingesteckt", "Plugged", "min"),
  HA_JBC_COUNTER(ha_counter_work_min, ha_value_flags, 0x0800U, HA, "Work", "Work", "min"),
  HA_JBC_COUNTER(ha_counter_work_cycles, ha_value_flags, 0x0800U, HA, "Arbeitszyklen", "Work cycles", nullptr),
  HA_JBC_COUNTER(ha_counter_suction_cycles, ha_value_flags, 0x0800U, HA, "Absaugzyklen", "Suction cycles", nullptr),
  HA_JBC_COUNTER(ha_partial_plug_min, ha_diag_flags, 0x0040U, HA, "Teil Eingesteckt", "Partial Plugged", "min"),
  HA_JBC_COUNTER(ha_partial_work_min, ha_diag_flags, 0x0040U, HA, "Teil Work", "Partial Work", "min"),
  HA_JBC_COUNTER(ha_partial_work_cycles, ha_diag_flags, 0x0040U, HA, "Teil Arbeitszyklen", "Partial Work cycles", nullptr),
  HA_JBC_COUNTER(ha_partial_suction_cycles, ha_diag_flags, 0x0040U, HA, "Teil Absaugzyklen", "Partial Suction cycles", nullptr),
  HA_JBC_COUNTER(cl_counter_plug_min, cl_flags, 0x0004U, CL, "Eingesteckt", "Plugged", "min"),
  HA_JBC_COUNTER(cl_counter_cleaning_continuous_min, cl_flags, 0x0004U, CL, "Reinigung kontinuierlich", "Continuous cleaning", "min"),
  HA_JBC_COUNTER(cl_counter_cleaning_detection_min, cl_flags, 0x0004U, CL, "Reinigung Erkennung", "Detection cleaning", "min"),
  HA_JBC_COUNTER(cl_counter_work_cycles, cl_flags, 0x0004U, CL, "Arbeitszyklen", "Work cycles", nullptr),
  HA_JBC_COUNTER(cl_counter_door_open_cycles, cl_flags, 0x0004U, CL, "Türöffnungen", "Door openings", nullptr),
  HA_JBC_COUNTER(cl_partial_plug_min, cl_flags, 0x0008U, CL, "Teil Eingesteckt", "Partial Plugged", "min"),
  HA_JBC_COUNTER(cl_partial_cleaning_continuous_min, cl_flags, 0x0008U, CL, "Teil Reinigung kontinuierlich", "Partial Continuous cleaning", "min"),
  HA_JBC_COUNTER(cl_partial_cleaning_detection_min, cl_flags, 0x0008U, CL, "Teil Reinigung Erkennung", "Partial Detection cleaning", "min"),
  HA_JBC_COUNTER(cl_partial_work_cycles, cl_flags, 0x0008U, CL, "Teil Arbeitszyklen", "Partial Work cycles", nullptr),
  HA_JBC_COUNTER(cl_partial_door_open_cycles, cl_flags, 0x0008U, CL, "Teil Türöffnungen", "Partial Door openings", nullptr),
  HA_JBC_COUNTER(ph_counter_plug_min, ph_flags, 0x0020U, PH, "Eingesteckt", "Plugged", "min"),
  HA_JBC_COUNTER(ph_counter_work_min_power, ph_flags, 0x0040U, PH, "Work Leistung", "Work power", "min"),
  HA_JBC_COUNTER(ph_counter_work_min_temp, ph_flags, 0x0040U, PH, "Work Temperatur", "Work temperature", "min"),
  HA_JBC_COUNTER(ph_counter_work_min_profile, ph_flags, 0x0040U, PH, "Work Profil", "Work profile", "min"),
  HA_JBC_COUNTER(ph_counter_work_cycles_power, ph_flags, 0x0080U, PH, "Zyklen Leistung", "Power cycles", nullptr),
  HA_JBC_COUNTER(ph_counter_work_cycles_temp, ph_flags, 0x0080U, PH, "Zyklen Temperatur", "Temperature cycles", nullptr),
  HA_JBC_COUNTER(ph_counter_work_cycles_profile, ph_flags, 0x0080U, PH, "Zyklen Profil", "Profile cycles", nullptr),
  HA_JBC_COUNTER(ph_partial_plug_min, ph_flags, 0x0100U, PH, "Teil Eingesteckt", "Partial Plugged", "min"),
  HA_JBC_COUNTER(ph_partial_work_min_power, ph_flags, 0x0200U, PH, "Teil Work Leistung", "Partial Work power", "min"),
  HA_JBC_COUNTER(ph_partial_work_min_temp, ph_flags, 0x0200U, PH, "Teil Work Temperatur", "Partial Work temperature", "min"),
  HA_JBC_COUNTER(ph_partial_work_min_profile, ph_flags, 0x0200U, PH, "Teil Work Profil", "Partial Work profile", "min"),
  HA_JBC_COUNTER(ph_partial_work_cycles_power, ph_flags, 0x0400U, PH, "Teil Zyklen Leistung", "Partial Power cycles", nullptr),
  HA_JBC_COUNTER(ph_partial_work_cycles_temp, ph_flags, 0x0400U, PH, "Teil Zyklen Temperatur", "Partial Temperature cycles", nullptr),
  HA_JBC_COUNTER(ph_partial_work_cycles_profile, ph_flags, 0x0400U, PH, "Teil Zyklen Profil", "Partial Profile cycles", nullptr),
  HA_JBC_COUNTER(fe_counter_plug_min, fe_flags, 0x0100U, FE, "Eingesteckt", "Plugged", "min"),
  HA_JBC_COUNTER(fe_counter_idle_min, fe_flags, 0x0100U, FE, "Idle", "Idle", "min"),
  HA_JBC_COUNTER(fe_counter_work_intake_min, fe_flags, 0x0100U, FE, "WORK Ansaugung", "WORK intake", "min"),
  HA_JBC_COUNTER(fe_counter_stand_intake_min, fe_flags, 0x0100U, FE, "STAND Ansaugung", "STAND intake", "min"),
  HA_JBC_COUNTER(fe_counter_work_cycles, fe_flags, 0x0100U, FE, "Arbeitszyklen", "Work cycles", nullptr),
  HA_JBC_COUNTER(fe_partial_plug_min, fe_flags, 0x0200U, FE, "Teil Eingesteckt", "Partial Plugged", "min"),
  HA_JBC_COUNTER(fe_partial_idle_min, fe_flags, 0x0200U, FE, "Teil Idle", "Partial Idle", "min"),
  HA_JBC_COUNTER(fe_partial_work_intake_min, fe_flags, 0x0200U, FE, "Teil WORK Ansaugung", "Partial WORK intake", "min"),
  HA_JBC_COUNTER(fe_partial_stand_intake_min, fe_flags, 0x0200U, FE, "Teil STAND Ansaugung", "Partial STAND intake", "min"),
  HA_JBC_COUNTER(fe_partial_work_cycles, fe_flags, 0x0200U, FE, "Teil Arbeitszyklen", "Partial Work cycles", nullptr),
  HA_JBC_COUNTER(sf_counter_tin_length, sf_flags, 0x0020U, SF, "Zinnlänge", "Tin length", nullptr),
  HA_JBC_COUNTER(sf_counter_plug_min, sf_flags, 0x0020U, SF, "Eingesteckt", "Plugged", "min"),
  HA_JBC_COUNTER(sf_counter_work_min, sf_flags, 0x0020U, SF, "Work", "Work", "min"),
  HA_JBC_COUNTER(sf_counter_idle_min, sf_flags, 0x0020U, SF, "Idle", "Idle", "min"),
  HA_JBC_COUNTER(sf_counter_work_cycles, sf_flags, 0x0020U, SF, "Arbeitszyklen", "Work cycles", nullptr),
  HA_JBC_COUNTER(sf_partial_tin_length, sf_flags, 0x0040U, SF, "Teil Zinnlänge", "Partial Tin length", nullptr),
  HA_JBC_COUNTER(sf_partial_plug_min, sf_flags, 0x0040U, SF, "Teil Eingesteckt", "Partial Plugged", "min"),
  HA_JBC_COUNTER(sf_partial_work_min, sf_flags, 0x0040U, SF, "Teil Work", "Partial Work", "min"),
  HA_JBC_COUNTER(sf_partial_idle_min, sf_flags, 0x0040U, SF, "Teil Idle", "Partial Idle", "min"),
  HA_JBC_COUNTER(sf_partial_work_cycles, sf_flags, 0x0040U, SF, "Teil Arbeitszyklen", "Partial Work cycles", nullptr),
};
#undef HA_JBC_COUNTER
static const size_t HA_JBC_COUNTER_COUNT = sizeof(HA_JBC_COUNTERS) / sizeof(HA_JBC_COUNTERS[0]);

static bool ha_jbc_counter_supported(const JbcUsbPortState& port, uint8_t family, const HaJbcCounterDef& def) {
  uint16_t flags = 0;
  memcpy(&flags, (const uint8_t*)&port + def.flags_offset, sizeof(flags));
  return family == def.family && (flags & def.flags_mask) == def.flags_mask;
}

static uint64_t ha_jbc_counter_value(const JbcUsbPortState& port, const HaJbcCounterDef& def) {
  uint64_t value = 0;
  memcpy(&value, (const uint8_t*)&port + def.value_offset, def.value_size);
  return value;
}

