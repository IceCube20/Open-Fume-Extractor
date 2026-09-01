#pragma once

#include <Arduino.h>
#include "bus/Rs485PeripheralBus.h"

struct UniversalEntityState {
  uint8_t id = 0;
  uint8_t len = 0;
  uint16_t age_ms = 0xFFFF;
  uint8_t data[32] = {0};
};


struct JbcUsbSoldPeripheralState {
  uint8_t flags = 0; // bit0 config valid, bit1 status valid, bit2 active
  uint8_t version = 0;
  char hash_mcu_uid[5] = {0};
  char datetime[15] = {0};
  uint8_t type = 0;
  uint8_t port = 0xFF;
  uint8_t function = 0;
  uint8_t activation = 0;
  uint8_t delay = 0;
  uint8_t pd_status = 0;
};

struct JbcUsbPhTcState {
  uint8_t flags = 0; // bit0 actual, bit1 warning, bit2 mode, bit3 selected temp
  uint16_t actual_temp = 0;
  uint8_t warning = 0;
  uint8_t mode = 0;
  uint16_t selected_temp = 0;
};

struct JbcUsbSfProgramState {
  uint8_t flags = 0; // bit0 valid, bit1 enabled
  char name[9] = {0};
  uint16_t length[3] = {0,0,0};
  uint16_t speed[3] = {0,0,0};
};

struct JbcUsbPortState {
  bool valid = false;
  uint8_t tool = 0;
  uint8_t error = 0;
  uint8_t status_flags = 0;
  uint16_t temperature = 0;
  uint16_t power_permille = 0;
  uint16_t time_to_sleep_hibern = 0;
  uint16_t time_to_stop = 0;
  uint8_t future_mode = 0;
  // CLM/CLMU detail telemetry (JbcUsbModule 0.1.40 D7/v5). cl_flags: bit0
  // motors valid, bit1 door valid, bit2 global counters, bit3 partial counters,
  // bit4 ConnectStatus valid, bit5 CONTROL.
  uint16_t cl_flags = 0;
  bool cl_motors_on = false;
  bool cl_door_open = false;
  uint32_t cl_counter_plug_min = 0;
  uint32_t cl_counter_cleaning_continuous_min = 0;
  uint32_t cl_counter_cleaning_detection_min = 0;
  uint32_t cl_counter_work_cycles = 0;
  uint32_t cl_counter_door_open_cycles = 0;
  uint32_t cl_partial_plug_min = 0;
  uint32_t cl_partial_cleaning_continuous_min = 0;
  uint32_t cl_partial_cleaning_detection_min = 0;
  uint32_t cl_partial_work_cycles = 0;
  uint32_t cl_partial_door_open_cycles = 0;
  // SOLD/P02 0.1.24+: raw M_R_TOOLLASTSTATE (0x57), valid when
  // detail_value_flags bit10 is set. bit5=QSTLock, bit6=ActiveCleaning.
  uint8_t detail_flags = 0;
  uint8_t sleep_delay_min = 0;
  uint8_t hiber_delay_min = 0;
  uint8_t delay_config_flags = 0;
  // SOLD extra validity: bit10 ToolLastStatus, bit11 EnabledPort, bit12 EnabledPort ON.
  uint16_t detail_value_flags = 0;
  uint16_t selected_temp = 0;
  uint16_t sleep_temp = 0;
  int16_t adjust_temp = 0;
  // SOLD cartridge/service telemetry (JbcUsbModule 0.1.20+).
  uint8_t cartridge_on = 0;
  int16_t cartridge_jbc_code = 0;
  int16_t cartridge_adjust_300 = 0;
  int16_t cartridge_adjust_400 = 0;
  uint8_t cartridge_group = 0;
  uint8_t cartridge_family = 0;
  int16_t tip_temp_a = 0;
  int16_t tip_temp_b = 0;
  int16_t cartridge_ma_a = 0;
  int16_t cartridge_ma_b = 0;
  int16_t cartridge_power_permille_a = 0;
  int16_t cartridge_power_permille_b = 0;
  uint32_t counter_plug_min = 0;
  uint32_t counter_work_min = 0;
  uint32_t counter_sleep_min = 0;
  uint32_t counter_hiber_min = 0;
  uint32_t counter_idle_min = 0;
  uint32_t counter_sleep_cycles = 0;
  uint32_t counter_desold_cycles = 0;
  // SOLD diagnostics from JbcUsbModule 0.1.26 D8/v1 suffix.
  // bit0 MOS temp, bit1 tool type, bit2 last error, bit3 max alarm, bit4 min alarm.
  uint8_t sold_diag_flags = 0;
  uint16_t sold_mos_temp = 0;
  uint8_t sold_tool_type = 0;
  uint8_t sold_tool_last_error = 0;
  int16_t sold_alarm_max_temp = 0;
  int16_t sold_alarm_max_delay_tenth_sec = 0;
  int16_t sold_alarm_min_temp = 0;
  int16_t sold_alarm_min_delay_tenth_sec = 0;
  // SOLD completion telemetry from JbcUsbModule 0.1.33 DA/v1.
  // bit0 partial time counters, bit1 partial cycles, bit2 selected profile,
  // bit3 profile mode, bit4 assistant configuration.
  uint16_t sold_extra_flags = 0;
  uint32_t sold_partial_plug_min = 0;
  uint32_t sold_partial_work_min = 0;
  uint32_t sold_partial_sleep_min = 0;
  uint32_t sold_partial_hiber_min = 0;
  uint32_t sold_partial_idle_min = 0;
  uint32_t sold_partial_sleep_cycles = 0;
  uint32_t sold_partial_desold_cycles = 0;
  uint8_t sold_profile_mode = 0;
  char sold_selected_profile[13] = {0};
  bool sold_assistant_on = false;
  int16_t sold_assistant_warning = 0;
  int16_t sold_assistant_error = 0;
  // SOLD safe read-only completion telemetry from JbcUsbModule 0.1.34 DD/v1.
  uint16_t sold_readonly_port_flags = 0;
  uint16_t sold_fixed_temp = 0;
  bool sold_fixed_temp_on = false;
  uint8_t sold_assistant_warning_code = 0;
  int16_t sold_result_similarity = 0;
  int16_t sold_result_tenths = 0;
  int16_t sold_result_energy = 0;
  uint16_t sold_direct_power_permille = 0;
  // ALE Tin Feeder read-only service telemetry (JbcUsbModule 0.1.34 D5/v1).
  uint16_t sold_feeder_flags = 0;
  uint8_t sold_feeder_working_mode = 0;
  uint8_t sold_feeder_selected_program = 0;
  uint16_t sold_feeder_delivery_length = 0;
  uint16_t sold_feeder_delivery_speed = 0;
  uint8_t sold_feeder_tin_diameter = 0;
  uint8_t sold_feeder_remove_length = 0;
  bool sold_feeder_speed_length_readonly = false;
  uint16_t sold_feeder_selectable_programs = 0;
  bool sold_feeder_clogging_detection = false;
  bool sold_feeder_motor_on = false;
  uint8_t sold_feeder_motor_direction = 0; // 0 ADD_TIN, 1 REMOVE_TIN
  uint16_t sold_feeder_program_length[5][3] = {{0}};
  uint16_t sold_feeder_program_speed[5][3] = {{0}};
  // D4/v1 unique grouped k26 counter extensions (ALE/CDE).
  uint16_t sold_special_counter_flags = 0;
  uint32_t sold_tin_deliver_cycles = 0;
  uint32_t sold_tin_length = 0;
  uint32_t sold_partial_tin_deliver_cycles = 0;
  uint32_t sold_partial_tin_length = 0;
  uint32_t sold_cde_sold_number = 0;
  uint32_t sold_cde_energy_delivered = 0;
  uint32_t sold_cde_sold_total = 0;
  uint32_t sold_cde_sold_per_min = 0;
  uint32_t sold_cde_sold_ok = 0;
  uint32_t sold_cde_partial_sold_number = 0;
  uint32_t sold_cde_partial_energy_delivered = 0;
  uint32_t sold_cde_partial_sold_total = 0;
  uint32_t sold_cde_partial_sold_per_min = 0;
  uint32_t sold_cde_partial_sold_ok = 0;
  // HA/JT/JTSE detail telemetry (JbcUsbModule 0.1.18+). Bits 12/13 from
  // module 0.1.28 carry station ConnectStatus valid / CONTROL.
  uint16_t ha_value_flags = 0;
  uint16_t protection_temp = 0;
  uint16_t selected_flow_permille = 0;
  uint16_t selected_ext_temp = 0;
  uint16_t actual_ext_temp = 0;
  int16_t ha_adjust_temp = 0;
  uint16_t configured_time_to_stop = 0;
  uint8_t external_tc_mode = 0;
  uint8_t start_mode = 0;
  uint8_t profile_mode = 0;
  uint8_t levels_on = 0;
  uint8_t selected_level = 0;
  uint8_t level_on[3] = {0,0,0};
  uint16_t level_temp[3] = {0,0,0};
  uint16_t level_flow_permille[3] = {0,0,0};
  uint16_t level_ext_temp[3] = {0,0,0};
  uint32_t ha_counter_plug_min = 0;
  uint32_t ha_counter_work_min = 0;
  uint32_t ha_counter_work_cycles = 0;
  uint32_t ha_counter_suction_cycles = 0;
  // HA diagnostics from JbcUsbModule 0.1.29 D9/v1. bit0 air temp, bit1 power,
  // bit2 flow, bit3 tool, bit4 error, bit5 raw tool status, bit6 partial counters,
  // bit7 direct heater state, bit8 direct suction state.
  uint16_t ha_diag_flags = 0;
  uint16_t ha_diag_air_temp = 0;
  uint16_t ha_diag_power_permille = 0;
  uint16_t ha_diag_flow_permille = 0;
  uint8_t ha_diag_tool = 0;
  uint8_t ha_diag_error = 0;
  uint8_t ha_diag_status = 0;
  uint8_t ha_diag_heater_state = 0;
  uint8_t ha_diag_suction_state = 0;
  uint32_t ha_partial_plug_min = 0;
  uint32_t ha_partial_work_min = 0;
  uint32_t ha_partial_work_cycles = 0;
  uint32_t ha_partial_suction_cycles = 0;
  // PH/Preheater complete UpdateData_PH telemetry (0.1.43 E5/v1).
  uint16_t ph_flags = 0;
  uint8_t ph_work_mode = 0;
  uint8_t ph_heater_status = 0;
  uint32_t ph_configured_time_to_stop = 0;
  uint16_t ph_selected_power = 0;
  uint8_t ph_active_zones = 0;
  uint32_t ph_counter_plug_min = 0;
  uint32_t ph_counter_work_min_power = 0;
  uint32_t ph_counter_work_min_temp = 0;
  uint32_t ph_counter_work_min_profile = 0;
  uint32_t ph_counter_work_cycles_power = 0;
  uint32_t ph_counter_work_cycles_temp = 0;
  uint32_t ph_counter_work_cycles_profile = 0;
  uint32_t ph_partial_plug_min = 0;
  uint32_t ph_partial_work_min_power = 0;
  uint32_t ph_partial_work_min_temp = 0;
  uint32_t ph_partial_work_min_profile = 0;
  uint32_t ph_partial_work_cycles_power = 0;
  uint32_t ph_partial_work_cycles_temp = 0;
  uint32_t ph_partial_work_cycles_profile = 0;
  // FE/Fume Extractor complete UpdateData_FE telemetry (0.1.44 E9/v1).
  // fe_flags bit0/1 WORK valid/ON, bit2/3 STAND valid/ON, bit4/5 TTS valid,
  // bit6 pedal action, bit7 pedal mode, bit8 global, bit9 partial counters.
  uint16_t fe_flags = 0;
  uint16_t fe_time_to_stop_work = 0;
  uint16_t fe_time_to_stop_stand = 0;
  uint8_t fe_pedal_action = 0;
  uint8_t fe_pedal_mode = 0;
  // 0.1.53 ED/v1 remaining safe FE public getters.
  uint16_t fe_service_flags = 0;
  uint8_t fe_stand_intakes = 0;
  uint16_t fe_suction_delay_work = 0;
  uint16_t fe_suction_delay_stand = 0;
  uint8_t fe_pedal_connected = 0;
  uint32_t fe_counter_plug_min = 0;
  uint32_t fe_counter_idle_min = 0;
  uint32_t fe_counter_work_intake_min = 0;
  uint32_t fe_counter_stand_intake_min = 0;
  uint32_t fe_counter_work_cycles = 0;
  uint32_t fe_partial_plug_min = 0;
  uint32_t fe_partial_idle_min = 0;
  uint32_t fe_partial_work_intake_min = 0;
  uint32_t fe_partial_stand_intake_min = 0;
  uint32_t fe_partial_work_cycles = 0;
  // SF/Solder Feeder complete UpdateData_SF telemetry (0.1.45 EB/v1).
  uint16_t sf_flags = 0;
  uint16_t sf_speed_tenth_mm_s = 0;
  uint16_t sf_length_tenth_mm = 0;
  uint8_t sf_feeding_state = 0;
  uint16_t sf_feeding_value_raw = 0;
  uint8_t sf_feeding_selected_program = 0;
  uint8_t sf_current_program_step = 0;
  uint64_t sf_counter_tin_length = 0;
  uint32_t sf_counter_plug_min = 0;
  uint32_t sf_counter_work_min = 0;
  uint32_t sf_counter_idle_min = 0;
  uint32_t sf_counter_work_cycles = 0;
  uint64_t sf_partial_tin_length = 0;
  uint32_t sf_partial_plug_min = 0;
  uint32_t sf_partial_work_min = 0;
  uint32_t sf_partial_idle_min = 0;
  uint32_t sf_partial_work_cycles = 0;
};

struct ModuleRecord {
  static const size_t UNIVERSAL_DESCRIPTOR_TEXT_MAX = 16384;
  static const uint8_t UNIVERSAL_ENTITY_MAX = 64;

  uint8_t addr = jbc_rs485::ADDR_INVALID;
  uint8_t type = jbc_rs485::MODULE_UNKNOWN;
  uint16_t hw_version = 0;
  uint8_t fw_major = 0;
  uint8_t fw_minor = 0;
  uint8_t fw_patch = 0;
  char fw_suffix[8] = {0};
  uint64_t uid = 0;
  uint32_t caps = 0;
  uint32_t last_seen_ms = 0;
  uint16_t timeout_count = 0;
  uint8_t consecutive_timeouts = 0;
  uint32_t miss_count = 0;
  uint32_t last_timeout_ms = 0;
  uint8_t last_timeout_cmd = 0;
  uint16_t crc_error_count = 0;
  bool online = false;
  bool came_online = false;
  bool seen_in_scan = false;
  bool role_jbc = false;
  bool role_output = false;
  uint8_t jbc_addr = 0;
  uint8_t station_addr = 0;
  uint8_t jbc_link_flags = 0;
  uint8_t jbc_work_mask = 0;
  uint8_t jbc_stand_mask = 0;
  uint16_t jbc_event_seq = 0;
  uint8_t jbc_usb_link_state = 0;
  uint8_t jbc_usb_frame_protocol = 0;
  uint8_t jbc_usb_command_protocol = 0;
  uint8_t jbc_usb_port_count = 0;
  bool jbc_usb_port_count_from_model = false;
  uint16_t jbc_usb_cp_vid = 0;
  uint16_t jbc_usb_cp_pid = 0;
  uint16_t jbc_usb_model_version = 0;
  char jbc_usb_protocol_text[12] = {0};
  char jbc_usb_model_raw[32] = {0};
  char jbc_usb_model[24] = {0};
  char jbc_usb_model_type[16] = {0};
  char jbc_usb_sw_version[24] = {0};
  char jbc_usb_hw_version[24] = {0};
  char jbc_usb_station_name[17] = {0};
  // JbcUsbModule 0.1.23+: station-level QST readback. valid/state bits:
  // bit0 QSTActivate, bit1 QSTStatus. Since module 0.1.24 detail_flags is the
  // real P02 M_R_TOOLLASTSTATE (0x57): bit5 QSTLock, bit6 ActiveCleaning.
  // detail_value_flags bit10 validates it; bit11 validates EnabledPort, bit12=ON.
  uint8_t jbc_usb_qst_valid_flags = 0;
  uint8_t jbc_usb_qst_state_flags = 0;
  // SOLD station diagnostics from D8/v1. valid bits: bit0 Trafo temp,
  // bit1 control/monitor readback, bit2 Trafo error temp, bit3 MOS error temp.
  uint8_t jbc_usb_sold_station_diag_flags = 0;
  uint16_t jbc_usb_sold_trafo_temp = 0;
  uint16_t jbc_usb_sold_trafo_error_temp = 0;
  uint16_t jbc_usb_sold_mos_error_temp = 0;
  bool jbc_usb_sold_control_mode = false;
  // SOLD completion station telemetry from JbcUsbModule 0.1.33 DB/v1.
  // flags: bit0 PIN enabled valid, bit1 enabled, bit2 PIN valid, bit3 configured,
  // bit4 min temp, bit5 max temp, bit6 robot config, bit7 robot status valid,
  // bit8 robot status ON, bit9 peripheral count valid.
  uint16_t jbc_usb_sold_extra_station_flags = 0;
  uint16_t jbc_usb_sold_min_temp = 0;
  uint16_t jbc_usb_sold_max_temp = 0;
  char jbc_usb_sold_pin[5] = {0};
  uint8_t jbc_usb_sold_robot_config[7] = {0};
  uint8_t jbc_usb_sold_peripheral_count = 0;
  uint8_t jbc_usb_sold_peripheral_transmitted = 0;
  JbcUsbSoldPeripheralState jbc_usb_sold_peripherals[4];
  // Comprehensive safe read-only SOLD station telemetry (0.1.34 DC/DF).
  uint32_t jbc_usb_sold_readonly_flags = 0;
  bool jbc_usb_sold_remote_mode = false;
  uint8_t jbc_usb_sold_temp_unit = 0;
  bool jbc_usb_sold_n2_mode = false;
  bool jbc_usb_sold_help_text = false;
  uint16_t jbc_usb_sold_power_limit = 0;
  bool jbc_usb_sold_beep = false;
  uint8_t jbc_usb_sold_interface[7] = {0};
  uint16_t jbc_usb_sold_graph_temp_max = 0;
  uint16_t jbc_usb_sold_graph_temp_min = 0;
  uint16_t jbc_usb_sold_graph_temp_range = 0;
  uint16_t jbc_usb_sold_graph_power_max = 0;
  uint16_t jbc_usb_sold_graph_power_min = 0;
  bool jbc_usb_sold_autoclean = false;
  uint16_t jbc_usb_sold_autoclean_temp = 0;
  uint16_t jbc_usb_sold_autoclean_seconds = 0;
  uint8_t jbc_usb_sold_ground_type = 0;
  uint8_t jbc_usb_sold_station_interface[4] = {0};
  uint8_t jbc_usb_sold_datetime[7] = {0};
  uint8_t jbc_usb_sold_ethernet[23] = {0};
  char jbc_usb_sold_frontal[21] = {0};
  // HA station diagnostics from D9/v1. valid bits: remote, unit, temp limits,
  // flow limits, external-TC limits, selected profile, robot config/status.
  uint16_t jbc_usb_ha_station_diag_flags = 0;
  bool jbc_usb_ha_remote_mode = false;
  uint8_t jbc_usb_ha_temp_unit = 0;
  uint16_t jbc_usb_ha_max_temp = 0;
  uint16_t jbc_usb_ha_min_temp = 0;
  uint16_t jbc_usb_ha_max_flow = 0;
  uint16_t jbc_usb_ha_min_flow = 0;
  uint16_t jbc_usb_ha_max_ext_temp = 0;
  uint16_t jbc_usb_ha_min_ext_temp = 0;
  char jbc_usb_ha_selected_profile[13] = {0};
  uint8_t jbc_usb_ha_robot_config[7] = {0};
  bool jbc_usb_ha_robot_status = false;
  uint8_t jbc_usb_ha_security_flags = 0;
  char jbc_usb_ha_pin[5] = {0};
  bool jbc_usb_ha_beep = false;
  // PH/Preheater station telemetry (0.1.43 E4/E6/E7).
  uint32_t jbc_usb_ph_station_flags = 0;
  int16_t jbc_usb_ph_max_power = 0;
  int16_t jbc_usb_ph_min_power = 0;
  uint16_t jbc_usb_ph_max_temp = 0;
  uint16_t jbc_usb_ph_min_temp = 0;
  char jbc_usb_ph_pin[5] = {0};
  bool jbc_usb_ph_beep = false;
  uint8_t jbc_usb_ph_robot_config[7] = {0};
  uint8_t jbc_usb_ph_profile_points_setting = 0;
  uint8_t jbc_usb_ph_profile_consignment = 0;
  uint8_t jbc_usb_ph_profile_tc_regulation = 0;
  int16_t jbc_usb_ph_profile_teach_interval = 0;
  JbcUsbPhTcState jbc_usb_ph_tc[4];
  uint8_t jbc_usb_ph_profile_count = 0;
  int16_t jbc_usb_ph_profile_time[47] = {0};
  int16_t jbc_usb_ph_profile_value[47] = {0};
  uint8_t jbc_usb_ph_teach_count = 0;
  int16_t jbc_usb_ph_teach_value[94] = {0};
  // FE/Fume Extractor station state (0.1.44 E8/v1 + 0.1.53 ED/v1).
  uint16_t jbc_usb_fe_station_flags = 0;
  uint8_t jbc_usb_fe_robot_config[7] = {0};
  uint16_t jbc_usb_fe_service_flags = 0;
  uint16_t jbc_usb_fe_flow_x_mil = 0;
  uint16_t jbc_usb_fe_speed_rpm = 0;
  uint16_t jbc_usb_fe_selected_flow_x_mil = 0;
  uint16_t jbc_usb_fe_filter_status = 0;
  char jbc_usb_fe_pin[5] = {0};
  bool jbc_usb_fe_beep = false;
  // PH/SF service-only getters omitted by their original UpdateData loops.
  bool jbc_usb_ph_remote_valid = false;
  bool jbc_usb_ph_remote_mode = false;
  bool jbc_usb_ph_conti_valid = false;
  uint8_t jbc_usb_ph_conti_speed = 0;
  uint8_t jbc_usb_ph_conti_ports = 0;
  bool jbc_usb_sf_conti_valid = false;
  uint8_t jbc_usb_sf_conti_speed = 0;
  uint8_t jbc_usb_sf_conti_ports = 0;
  // SF/Solder Feeder station state + programs (0.1.45 EA/EC).
  uint16_t jbc_usb_sf_station_flags = 0;
  char jbc_usb_sf_pin[5] = {0};
  uint8_t jbc_usb_sf_length_unit = 0;
  uint8_t jbc_usb_sf_robot_config[7] = {0};
  uint8_t jbc_usb_sf_program_list[35] = {0};
  JbcUsbSfProgramState jbc_usb_sf_programs[35];
  uint8_t jbc_usb_device_id_len = 0;
  uint8_t jbc_usb_device_id[32] = {0};
  uint32_t jbc_usb_state_last_ms = 0;
  JbcUsbPortState jbc_usb_ports[4];
  uint16_t jbc_usb_station_error = 0xFFFF;
  // Last raw system/error values read back from the JBC bridge itself.
  // The master may expose a locally-computed system error immediately, but
  // these fields tell us whether the bridge has actually accepted it already.
  uint16_t jbc_filter_life = 0;
  uint16_t jbc_filter_sat = 0;
  uint16_t jbc_stat_error = 0;
  bool jbc_settings_valid = false;
  uint8_t jbc_suction_level = 0;
  uint16_t jbc_select_flow = 0;
  uint16_t jbc_delay_work_sec = 0;
  uint16_t jbc_delay_stand_sec = 0;
  uint8_t jbc_stand_intakes = 0;
  uint8_t jbc_continuous = 0;
  // Raw native JBC-FAE GET_STATE values kept separately for developer
  // diagnostics. The ordinary settings fields may be normalized to Master
  // desired values after a mismatch, so they are not a trustworthy RX trace.
  uint8_t jbc_dbg_suction_level_rx = 0;
  uint16_t jbc_dbg_select_flow_rx = 0;
  uint16_t jbc_dbg_delay_work_sec_rx = 0;
  uint16_t jbc_dbg_delay_stand_sec_rx = 0;
  uint8_t jbc_dbg_stand_intakes_rx = 0;
  uint8_t jbc_dbg_continuous_rx = 0;
  uint16_t jbc_actual_flow = 0;
  uint16_t jbc_speed_rpm = 0;
  uint8_t jbc_extractor_output_active = 0;
  bool jbc_extractor_output_valid = false;
  uint8_t jbc_device_id_len = 0;
  uint8_t jbc_device_id[64] = {0};
  uint16_t io_input_mask = 0;
  uint16_t io_output_mask = 0;
  uint16_t io_fault_mask = 0;
  char io_main_alias[19] = {0};
  char io_in1_alias[19] = {0};
  char io_in2_alias[19] = {0};
  char io_out1_alias[19] = {0};
  char io_out2_alias[19] = {0};
  bool output_status_valid = false;
  bool output_enabled = false;
  uint16_t output_power = 0;
  uint16_t output_rpm = 0;
  uint16_t output_fault_mask = 0;
  bool route_in1_output = false;
  bool route_in2_output = false;
  uint8_t weller_speed_percent = 0;
  uint8_t weller_filter_status = 0;
  uint16_t weller_filter_runtime_minutes = 0;
  uint16_t weller_programmed_filter_minutes = 0;
  uint16_t weller_fan_rpm = 0;
  uint16_t weller_version = 0;
  uint8_t weller_work_light = 0;
  uint16_t weller_uart_age_sec = 0xFFFF;
  uint16_t fanio_filter_saturation_permille = 0;
  int16_t fanio_filter_pressure_raw = 0;
  int16_t fanio_filter_zero_raw = 0;
  int16_t fanio_filter_clean_raw = 0;
  int16_t fanio_filter_warn_raw = 0;
  int16_t fanio_filter_full_raw = 0;
  uint8_t fanio_filter_flags = 0;
  uint8_t fanio_filter_cal_quality = 0;
  bool telemetry_valid = false;
  uint32_t module_heap_free = 0;
  uint32_t module_heap_min = 0;
  uint32_t module_uptime_s = 0;
  uint8_t module_cpu_load_pct = 0;
  uint16_t module_loop_max_ms = 0;
  // JBC USB / CP210x transport diagnostics. These are populated from the
  // JbcUsbModule extended telemetry payload and stay zero for all other modules.
  uint32_t jbc_usb_usb_rx_bytes = 0;
  uint32_t jbc_usb_usb_tx_bytes = 0;
  uint32_t jbc_usb_rx_frames = 0;
  uint32_t jbc_usb_tx_frames = 0;
  uint16_t jbc_usb_usb_errors = 0;
  uint16_t jbc_usb_bcc_errors = 0;
  uint16_t jbc_usb_frame_errors = 0;
  uint16_t jbc_usb_decode_errors = 0;
  uint16_t jbc_usb_handshake_errors = 0;
  uint8_t jbc_usb_decode_last_cmd = 0;
  uint8_t jbc_usb_decode_last_got_len = 0;
  uint8_t jbc_usb_decode_last_expected_min = 0xFF;
  uint8_t jbc_usb_decode_last_expected_max = 0xFF;
  uint8_t jbc_usb_decode_top_cmd[3] = {0,0,0};
  uint16_t jbc_usb_decode_top_count[3] = {0,0,0};
  uint32_t jbc_usb_cp_baud = 0;
  uint16_t jbc_usb_cp_line_ctl = 0;
  uint8_t jbc_usb_cp_mdmsts = 0;
  uint32_t jbc_usb_cp_comm_errors = 0;
  uint32_t jbc_usb_cp_hold_reasons = 0;
  uint32_t jbc_usb_cp_in_queue = 0;
  uint32_t jbc_usb_cp_out_queue = 0;
  bool jbc_usb_cp_diag_valid = false;
  bool led_status_valid = false;
  uint8_t led_ofe_event = 0;
  uint8_t led_evt_event = 0;
  uint8_t display_view_mode = 0;
  uint8_t display_view_arg = 0;
  uint8_t display_brightness_pct = 0;
  uint8_t display_language = 0;
  uint8_t display_theme = 0;
  uint8_t display_screensaver_min = 0;
  uint8_t display_universal_entity_start = 0;
  bool universal_descriptor_valid = false;
  uint32_t universal_descriptor_crc = 0;
  uint8_t universal_descriptor_chunks = 0;
  uint32_t universal_descriptor_last_ms = 0;
  char universal_descriptor[UNIVERSAL_DESCRIPTOR_TEXT_MAX] = {0};
  bool universal_entities_valid = false;
  uint8_t universal_entity_count = 0;
  uint32_t universal_entities_last_ms = 0;
  UniversalEntityState universal_entities[UNIVERSAL_ENTITY_MAX];
  char name[24] = {0};
  char label[24] = {0};

};

// ---------------------------------------------------------------------------
// Compact JBC USB core telemetry
//
// This is the deliberately small, shared data model used by MQTT and the OFE
// displays.  The full DLL-faithful diagnostics stay in ModuleRecord/WebStatus;
// this view carries only operational values that are useful in normal use.
// ---------------------------------------------------------------------------
enum JbcUsbCoreFamily : uint8_t {
  JBC_USB_CORE_UNKNOWN = 0,
  JBC_USB_CORE_SOLD = 1,
  JBC_USB_CORE_HA = 2,
  JBC_USB_CORE_CL = 3,
  JBC_USB_CORE_PH = 4,
  JBC_USB_CORE_FE = 5,
  JBC_USB_CORE_SF = 6,
};

enum JbcUsbCorePortStateCode : uint8_t {
  JBC_USB_STATE_IDLE = 0,
  JBC_USB_STATE_WORK = 1,
  JBC_USB_STATE_STAND = 2,
  JBC_USB_STATE_SLEEP = 3,
  JBC_USB_STATE_HIBERNATION = 4,
  JBC_USB_STATE_COOLING = 5,
  JBC_USB_STATE_SUCTION = 6,
  JBC_USB_STATE_CLEANING = 7,
  JBC_USB_STATE_FEEDING = 8,
  JBC_USB_STATE_NO_TOOL = 9,
  JBC_USB_STATE_EXTRACTOR = 10,
};

struct JbcUsbCorePort {
  bool valid = false;
  uint8_t state = JBC_USB_STATE_IDLE;
  uint8_t tool = 0;
  uint8_t error = 0;
  uint16_t actual_temp = 0;       // JBC raw temperature, display as raw/9 °C
  uint16_t selected_temp = 0;     // effective setpoint, JBC raw /9 °C
  bool selected_temp_valid = false;
  uint16_t power_permille = 0;    // 0..1000 = 0..100.0 %
  uint16_t flow_permille = 0;     // HA live airflow, 0..1000 = 0..100.0 %
  uint16_t selected_flow_permille = 0;
  bool selected_flow_valid = false;
  uint16_t time_to_stop = 0;      // HA/PH DLL raw deciseconds
  uint16_t transition_countdown_s = 0; // SOLD seconds until FutureMode
  uint8_t future_mode = 0;        // SOLD 'S', 'H' or 'N'
  // Family-specific compact values.  They are populated only for the matching
  // family and remain zero otherwise.
  uint8_t mode = 0;               // CL cleaner mode / FE suction level / SF mode
  uint16_t selected_power_permille = 0; // PH selected power
  bool selected_power_valid = false;
  uint8_t active_zones = 0;       // PH
  bool active_zones_valid = false;
  bool heater_valid = false;
  bool heater_on = false;
  bool motors_valid = false;
  bool motors_on = false;
  bool door_valid = false;
  bool door_open = false;
  bool intake_work_valid = false;
  bool intake_work_on = false;
  bool intake_stand_valid = false;
  bool intake_stand_on = false;
  uint16_t fe_time_to_stop_work = 0;   // DLL raw, unit intentionally unspecified
  uint16_t fe_time_to_stop_stand = 0;  // DLL raw, unit intentionally unspecified
  bool sf_speed_valid = false;
  uint16_t sf_speed_tenth_mm_s = 0;
  bool sf_length_valid = false;
  uint16_t sf_length_tenth_mm = 0;
  bool sf_feeding_valid = false;
  bool sf_feeding = false;
  uint8_t sf_selected_program = 0;
  bool sf_tool_enabled_valid = false;
  bool sf_tool_enabled = false;
};

struct JbcUsbCoreTc {
  bool actual_valid = false;
  uint16_t actual_temp = 0;
  bool selected_valid = false;
  uint16_t selected_temp = 0;
};

struct JbcUsbCoreState {
  bool valid = false;
  bool linked = false;
  uint8_t family = JBC_USB_CORE_UNKNOWN;
  uint8_t port_count = 0;
  bool work_active = false;
  bool stand_active = false;
  bool station_error_valid = false;
  uint16_t station_error = 0;
  bool connect_mode_valid = false;
  bool control_mode = false;
  bool continuous_valid = false;   // FE
  bool continuous_on = false;
  JbcUsbCoreTc ph_tc[4];
  JbcUsbCorePort ports[4];
};

static inline uint8_t jbc_usb_core_family_for_model(const char* model) {
  if (!model || !model[0]) return JBC_USB_CORE_UNKNOWN;
  // Match the WebStatus normalization: station model tokens can contain
  // separators such as CD/CF while the family table uses compact names.
  char key[24] = {0};
  uint8_t n = 0;
  for (const char* p = model; *p && n < sizeof(key) - 1; ++p) {
    char c = *p;
    if (c == '/' || c == '-' || c == '_' || c == ' ' || c == '\t') continue;
    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    key[n++] = c;
  }
  const char* sold[] = {"CA","CDCF","CDN","CP","CSCV","CDE","CFE","CAE","CPE","CSVE","DD","DDE","DDR","DI","DM","DME","HD","HDE","HDR","LC","NA","NAE","PSE","SM","WS","ALE"};
  for (size_t i = 0; i < sizeof(sold)/sizeof(sold[0]); ++i) if (!strcmp(key, sold[i])) return JBC_USB_CORE_SOLD;
  if (!strcmp(key, "JT") || !strcmp(key, "JTSE")) return JBC_USB_CORE_HA;
  if (!strcmp(key, "CLM") || !strcmp(key, "CLMU")) return JBC_USB_CORE_CL;
  if (!strcmp(key, "PH") || !strcmp(key, "PHBE") || !strcmp(key, "PHNE") || !strcmp(key, "PHSE") || !strcmp(key, "PHXL")) return JBC_USB_CORE_PH;
  if (!strcmp(key, "F1") || !strcmp(key, "F2") || !strcmp(key, "F2W") || !strcmp(key, "F4W")) return JBC_USB_CORE_FE;
  if (!strcmp(key, "SF")) return JBC_USB_CORE_SF;
  return JBC_USB_CORE_UNKNOWN;
}

static inline uint16_t jbc_usb_core_sold_setpoint(const JbcUsbPortState& p, bool& valid) {
  valid = false;
  if ((p.detail_value_flags & 0x0010U) && p.levels_on) {
    const uint8_t sel = p.selected_level;
    if (sel < 3 && p.level_on[sel]) { valid = true; return p.level_temp[sel]; }
    return 0;
  }
  if (p.detail_value_flags & 0x0001U) { valid = true; return p.selected_temp; }
  return 0;
}

static inline uint8_t jbc_usb_core_port_state(uint8_t family, const JbcUsbPortState& p) {
  if (!p.valid) return JBC_USB_STATE_IDLE;
  if (family == JBC_USB_CORE_SOLD) {
    if (!p.tool) return JBC_USB_STATE_NO_TOOL;
    if (p.status_flags & 0x04) return JBC_USB_STATE_HIBERNATION;
    if (p.status_flags & 0x02) return JBC_USB_STATE_SLEEP;
    if (p.status_flags & 0x01) return JBC_USB_STATE_STAND;
    if (p.status_flags & 0x08) return JBC_USB_STATE_EXTRACTOR;
    return JBC_USB_STATE_WORK;
  }
  if (family == JBC_USB_CORE_HA) {
    if (!p.tool) return JBC_USB_STATE_NO_TOOL;
    if (p.status_flags & 0x20) return JBC_USB_STATE_WORK;
    if (p.status_flags & 0x01) return JBC_USB_STATE_STAND;
    if (p.status_flags & 0x40) return JBC_USB_STATE_COOLING;
    if (p.status_flags & 0x80) return JBC_USB_STATE_SUCTION;
    return JBC_USB_STATE_IDLE;
  }
  if (family == JBC_USB_CORE_CL) {
    return ((p.cl_flags & 0x0001U) && p.cl_motors_on) ? JBC_USB_STATE_CLEANING : JBC_USB_STATE_IDLE;
  }
  if (family == JBC_USB_CORE_PH) return (p.status_flags & 0x20) ? JBC_USB_STATE_WORK : JBC_USB_STATE_IDLE;
  if (family == JBC_USB_CORE_SF) return ((p.sf_flags & 0x0004U) && p.sf_feeding_state) ? JBC_USB_STATE_FEEDING : JBC_USB_STATE_IDLE;
  return JBC_USB_STATE_IDLE;
}

static inline JbcUsbCoreState jbc_usb_core_state(const ModuleRecord& m) {
  JbcUsbCoreState c;
  if (m.type != jbc_rs485::MODULE_JBC_USB && !(m.caps & jbc_rs485::CAP_JBC_USB)) return c;
  c.valid = true;
  c.linked = m.online && ((m.jbc_link_flags & 0x01U) != 0);
  c.family = jbc_usb_core_family_for_model(m.jbc_usb_model);
  c.port_count = m.jbc_usb_port_count > 4 ? 4 : m.jbc_usb_port_count;
  c.work_active = m.jbc_work_mask != 0;
  c.stand_active = m.jbc_stand_mask != 0;
  c.station_error_valid = m.jbc_usb_station_error != 0xFFFFU;
  c.station_error = c.station_error_valid ? m.jbc_usb_station_error : 0;

  if (c.family == JBC_USB_CORE_SOLD) {
    c.connect_mode_valid = (m.jbc_usb_sold_station_diag_flags & 0x02U) != 0;
    c.control_mode = m.jbc_usb_sold_control_mode;
  } else if (c.family == JBC_USB_CORE_HA) {
    const uint16_t f = m.jbc_usb_ports[0].ha_value_flags;
    c.connect_mode_valid = (f & 0x1000U) != 0;
    c.control_mode = (f & 0x2000U) != 0;
  } else if (c.family == JBC_USB_CORE_CL) {
    const uint16_t f = m.jbc_usb_ports[0].cl_flags;
    c.connect_mode_valid = (f & 0x0010U) != 0;
    c.control_mode = (f & 0x0020U) != 0;
  } else if (c.family == JBC_USB_CORE_PH) {
    c.connect_mode_valid = (m.jbc_usb_ph_station_flags & 0x00000100UL) != 0;
    c.control_mode = (m.jbc_usb_ph_station_flags & 0x00000200UL) != 0;
    for (uint8_t i = 0; i < 4; ++i) {
      const JbcUsbPhTcState& t = m.jbc_usb_ph_tc[i];
      c.ph_tc[i].actual_valid = (t.flags & 0x01U) != 0;
      c.ph_tc[i].actual_temp = t.actual_temp;
      c.ph_tc[i].selected_valid = (t.flags & 0x08U) != 0;
      c.ph_tc[i].selected_temp = t.selected_temp;
    }
  } else if (c.family == JBC_USB_CORE_FE) {
    c.connect_mode_valid = (m.jbc_usb_fe_station_flags & 0x0004U) != 0;
    c.control_mode = (m.jbc_usb_fe_station_flags & 0x0008U) != 0;
    c.continuous_valid = (m.jbc_usb_fe_station_flags & 0x0001U) != 0;
    c.continuous_on = (m.jbc_usb_fe_station_flags & 0x0002U) != 0;
  } else if (c.family == JBC_USB_CORE_SF) {
    c.connect_mode_valid = (m.jbc_usb_sf_station_flags & 0x0080U) != 0;
    c.control_mode = (m.jbc_usb_sf_station_flags & 0x0100U) != 0;
  }

  for (uint8_t i = 0; i < c.port_count; ++i) {
    const JbcUsbPortState& p = m.jbc_usb_ports[i];
    JbcUsbCorePort& d = c.ports[i];
    d.valid = p.valid;
    d.state = jbc_usb_core_port_state(c.family, p);
    d.tool = p.tool;
    d.error = p.error;
    d.actual_temp = p.temperature;
    d.power_permille = p.power_permille;
    d.time_to_stop = p.time_to_stop;
    if (c.family == JBC_USB_CORE_SOLD) {
      d.selected_temp = jbc_usb_core_sold_setpoint(p, d.selected_temp_valid);
      d.transition_countdown_s = p.time_to_sleep_hibern;
      d.future_mode = p.future_mode;
    } else if (c.family == JBC_USB_CORE_HA) {
      d.selected_temp_valid = (p.ha_value_flags & 0x0002U) != 0;
      d.selected_temp = p.selected_temp;
      d.flow_permille = p.time_to_sleep_hibern; // DLL live HA flow field
      d.selected_flow_valid = (p.ha_value_flags & 0x0004U) != 0;
      d.selected_flow_permille = p.selected_flow_permille;
    } else if (c.family == JBC_USB_CORE_CL) {
      d.mode = p.future_mode;
      d.motors_valid = (p.cl_flags & 0x0001U) != 0;
      d.motors_on = p.cl_motors_on;
      d.door_valid = (p.cl_flags & 0x0002U) != 0;
      d.door_open = p.cl_door_open;
    } else if (c.family == JBC_USB_CORE_PH) {
      d.mode = p.ph_work_mode;
      d.heater_valid = (p.ph_flags & 0x0002U) != 0;
      d.heater_on = p.ph_heater_status != 0;
      d.selected_power_valid = (p.ph_flags & 0x0008U) != 0;
      if (d.selected_power_valid) d.selected_power_permille = p.ph_selected_power;
      d.active_zones_valid = (p.ph_flags & 0x0010U) != 0;
      if (d.active_zones_valid) d.active_zones = p.ph_active_zones;
      // The base PH live packet carries TimeToStop in the common field.
    } else if (c.family == JBC_USB_CORE_FE) {
      d.mode = p.future_mode;
      d.intake_work_valid = (p.fe_flags & 0x0001U) != 0;
      d.intake_work_on = (p.fe_flags & 0x0002U) != 0;
      d.intake_stand_valid = (p.fe_flags & 0x0004U) != 0;
      d.intake_stand_on = (p.fe_flags & 0x0008U) != 0;
      d.fe_time_to_stop_work = p.fe_time_to_stop_work;
      d.fe_time_to_stop_stand = p.fe_time_to_stop_stand;
    } else if (c.family == JBC_USB_CORE_SF) {
      d.mode = p.future_mode;
      d.sf_selected_program = p.time_to_sleep_hibern > 255U ? 255U : (uint8_t)p.time_to_sleep_hibern;
      d.sf_speed_valid = (p.sf_flags & 0x0001U) != 0;
      d.sf_speed_tenth_mm_s = p.sf_speed_tenth_mm_s;
      d.sf_length_valid = (p.sf_flags & 0x0002U) != 0;
      d.sf_length_tenth_mm = p.sf_length_tenth_mm;
      d.sf_feeding_valid = (p.sf_flags & 0x0004U) != 0;
      d.sf_feeding = p.sf_feeding_state != 0;
      if (p.sf_flags & 0x0004U) d.sf_selected_program = p.sf_feeding_selected_program;
      d.sf_tool_enabled_valid = (p.sf_flags & 0x0008U) != 0;
      d.sf_tool_enabled = (p.sf_flags & 0x0010U) != 0;
    }
  }
  return c;
}

static inline const char* jbc_usb_core_family_name(uint8_t family) {
  switch (family) {
    case JBC_USB_CORE_SOLD: return "SOLD";
    case JBC_USB_CORE_HA: return "HA";
    case JBC_USB_CORE_CL: return "CL";
    case JBC_USB_CORE_PH: return "PH";
    case JBC_USB_CORE_FE: return "FE";
    case JBC_USB_CORE_SF: return "SF";
    default: return "UNKNOWN";
  }
}

static inline const char* jbc_usb_core_state_name(uint8_t state) {
  switch (state) {
    case JBC_USB_STATE_WORK: return "WORK";
    case JBC_USB_STATE_STAND: return "STAND";
    case JBC_USB_STATE_SLEEP: return "SLEEP";
    case JBC_USB_STATE_HIBERNATION: return "HIBERNATION";
    case JBC_USB_STATE_COOLING: return "COOLING";
    case JBC_USB_STATE_SUCTION: return "SUCTION";
    case JBC_USB_STATE_CLEANING: return "CLEANING";
    case JBC_USB_STATE_FEEDING: return "FEEDING";
    case JBC_USB_STATE_NO_TOOL: return "NO_TOOL";
    case JBC_USB_STATE_EXTRACTOR: return "EXTRACTOR";
    default: return "IDLE";
  }
}

// WebStatus-equivalent friendly names for the compact MQTT/display path.
// Keep these mappings in lock-step with jbcUsbGenericToolId(),
// jbcUsbToolName(), jbcUsbToolErrorName() and jbcUsbStationErrorName().
static inline void jbc_usb_core_normalize_model(const char* model, char* out, size_t out_len) {
  if (!out || !out_len) return;
  out[0] = 0;
  if (!model) return;
  size_t n = 0;
  for (const char* p = model; *p && n + 1 < out_len; ++p) {
    char c = *p;
    if (c == '/' || c == '-' || c == '_' || c == ' ' || c == '\t') continue;
    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    out[n++] = c;
  }
  out[n] = 0;
}

static inline uint8_t jbc_usb_core_generic_tool_id(uint8_t family, const char* model, uint8_t raw_tool) {
  char key[24];
  jbc_usb_core_normalize_model(model, key, sizeof(key));
  if (family == JBC_USB_CORE_SOLD) {
    if (!strcmp(key, "HD") || !strcmp(key, "HDE")) return 9;
    if (!strcmp(key, "NA")) {
      if (raw_tool == 0) return 0;
      if (raw_tool == 1) return 7;
      if (raw_tool == 3) return 8;
      return raw_tool;
    }
    if (!strcmp(key, "ALE")) return 10;
    return raw_tool;
  }
  if (family == JBC_USB_CORE_HA && raw_tool > 0) return (uint8_t)(raw_tool + 30U);
  return raw_tool;
}

static inline const char* jbc_usb_core_known_tool_name(uint8_t generic_tool) {
  switch (generic_tool) {
    case 1: return "T210";
    case 2: return "T245";
    case 3: return "PA";
    case 4: return "HT";
    case 5: return "DS";
    case 6: return "DR";
    case 7: return "NT105";
    case 8: return "NP105";
    case 9: return "T470";
    case 10: return "ALE250";
    case 31: return "JT";
    case 32: return "TE";
    case 33: return "PHS";
    case 34: return "PHB";
    default: return nullptr;
  }
}

static inline uint8_t jbc_usb_core_tool_error_code(uint8_t family, uint8_t raw_error) {
  if (!raw_error) return 0;
  if (family == JBC_USB_CORE_HA) return (uint8_t)(raw_error + 20U);
  if (family == JBC_USB_CORE_PH) return (uint8_t)(raw_error + 40U);
  return raw_error;
}

static inline const char* jbc_usb_core_known_tool_error_name(uint8_t code) {
  switch (code) {
    case 0: return "NO_ERROR";
    case 1: return "SHORTCIRCUIT";
    case 2: return "SHORTCIRCUIT_NR";
    case 3: return "OPENCIRCUIT";
    case 4: return "NO_TOOL";
    case 5: return "WRONGTOOL";
    case 6: return "DETECTIONTOOL";
    case 7: return "MAXPOWER";
    case 8: return "STOPOVERLOAD_MOS";
    case 9: return "TIN_FEEDER_CLOGGING";
    case 21: return "AIR_PUMP_ERROR";
    case 22: return "PROTECION_TC_HIGH";
    case 23: return "REGULATION_TC_HIGH";
    case 24: return "EXTERNAL_TC_MISSING";
    case 25: return "SELECTED_TEMP_NOT_REACHED";
    case 26: return "HIGH_HEATER_INTENSITY";
    case 27: return "LOW_HEATER_RESISTANCE";
    case 28: return "WRONG_HEATER";
    case 29: return "NOTOOL_HA";
    case 30: return "DETECTIONTOOL_HA";
    case 41: return "SELECTED_TEMP_NOT_REACHED_PH";
    case 42: return "LOW_HEATER_INTENSITY";
    case 43: return "TC1_NOT_CONNECTED";
    case 44: return "TC2_NOT_CONNECTED";
    case 45: return "TC3_NOT_CONNECTED";
    case 46: return "TC4_NOT_CONNECTED";
    case 47: return "TC1_LIMIT_REACHED";
    case 48: return "TC2_LIMIT_REACHED";
    case 49: return "TC3_LIMIT_REACHED";
    case 50: return "TC4_LIMIT_REACHED";
    default: return nullptr;
  }
}

static inline const char* jbc_usb_core_known_station_error_name(uint16_t value) {
  switch (value) {
    case 0: return "NO_ERROR";
    case 1: return "STOPOVERLOAD_TRAFO";
    case 2: return "WRONGSENSOR_TRAFO";
    case 3: return "MEMORY";
    case 4: return "MAINSFREQUENCY";
    case 5: return "STATION_MODEL";
    case 6: return "NOT_MCU_TOOLS";
    default: return nullptr;
  }
}

class ModuleRegistry {
public:
  static const uint8_t MAX_MODULES = 16;

  // Allocate the record table after Arduino/PSRAM initialization. Keeping the
  // large ModuleRecord array out of static internal DRAM is important because
  // every record contains an 8 KiB Universal/Modbus descriptor cache.
  bool begin();
  bool usesPsram() const { return records_psram_; }
  size_t storageBytes() const { return sizeof(ModuleRecord) * MAX_MODULES; }

  void clear();
  ModuleRecord* upsert(uint8_t addr);
  ModuleRecord* find(uint8_t addr);
  const ModuleRecord* find(uint8_t addr) const;

  // Enforce the physical identity invariant: one UID may exist at only one
  // RS485 address. If the same UID was remembered at an old address, migrate
  // or merge that record into `addr` instead of creating an offline duplicate.
  ModuleRecord* bindUidToAddress(uint64_t uid, uint8_t addr);
  uint8_t count() const { return count_; }
  ModuleRecord& at(uint8_t index) { return records_[index]; }
  const ModuleRecord& at(uint8_t index) const { return records_[index]; }
  ModuleRecord* firstWithCaps(uint32_t caps);
  const ModuleRecord* firstWithCaps(uint32_t caps) const;
  void clearRoles();
  void markAllScanUnseen();
  uint8_t removeScanUnseen();
  void sortByAddress();
private:
  void removeAt(uint8_t index);
  static void mergeHistory(ModuleRecord& dst, const ModuleRecord& src);

  ModuleRecord* records_ = nullptr;
  uint8_t count_ = 0;
  bool records_psram_ = false;
};
