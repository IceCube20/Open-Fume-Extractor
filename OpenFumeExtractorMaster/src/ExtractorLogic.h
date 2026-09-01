#pragma once

#include <Arduino.h>
#include "bus/Rs485PeripheralBus.h"

struct JbcModuleState {
  bool valid = false;
  uint8_t module_addr = 0;
  uint8_t link_flags = 0;
  uint8_t jbc_addr = 0;
  uint8_t station_addr = 0;
  uint8_t base_state = 0;
  uint8_t work_mask = 0;
  uint8_t stand_mask = 0;
  uint8_t suction_level = 0;
  uint16_t select_flow = 0;
  uint16_t actual_flow = 0;
  uint16_t speed_rpm = 0;
  uint16_t delay_work_sec = 0;
  uint16_t delay_stand_sec = 0;
  uint8_t stand_intakes = 0;
  uint8_t continuous = 0;
  uint8_t extractor_output_active = 0;
  bool extractor_output_valid = false;
  uint16_t filter_life = 0;
  uint16_t filter_sat = 0;
  uint16_t stat_error = 0;
  uint8_t usb_connect = 0;
  uint8_t device_id_len = 0;
  uint8_t device_id[64] = {0};
  uint32_t last_update_ms = 0;
};

struct OutputModuleState {
  bool valid = false;
  bool enabled = false;
  uint16_t power = 0;
  uint16_t rpm = 0;
  uint16_t fault_mask = 0;
  uint32_t last_update_ms = 0;
};

class ExtractorLogic {
public:
  bool updateFromFastPoll(uint8_t module_addr, const jbc_rs485::FastPollState& fast);
  bool updateAggregateJbcState(uint8_t work_mask, uint8_t stand_mask, bool continuous);
  bool updateExternalInput(bool active);
  void updateJbcState(uint8_t module_addr, const JbcModuleState& state);
  void clearJbcConnectionState();
  void updateControlSettings(const JbcModuleState& state);
  void setAfterrunPowerProfile(bool enabled, uint16_t power);
  bool afterrunPowerProfileEnabled() const { return afterrun_power_enabled_; }
  uint16_t afterrunPower() const { return afterrun_power_; }
  void updateSystemError(uint16_t error_mask) { jbc_state_.stat_error = error_mask; jbc_state_.valid = true; }
  void updateSystemFilter(uint16_t filter_life, uint16_t filter_sat) {
    jbc_state_.filter_life = filter_life;
    jbc_state_.filter_sat = filter_sat;
    jbc_state_.valid = true;
  }
  void updateOutputState(const OutputModuleState& state);
  void tick();

  bool outputEnabled() const { return desired_output_enabled_; }
  uint16_t outputPower() const { return desired_power_; }
  uint8_t workMask() const { return work_mask_; }
  bool continuous() const { return continuous_; }
  bool externalInputActive() const { return external_input_active_; }
  uint32_t afterrunLeftMs() const;
  const JbcModuleState& jbcState() const { return jbc_state_; }
  const OutputModuleState& outputState() const { return output_state_; }
  bool outputDirty() const { return output_dirty_; }
  void markOutputDirty() { output_dirty_ = true; }
  void clearOutputDirty() { output_dirty_ = false; }

private:
  uint16_t targetPowerForJbc() const;
  bool applyOutputState(bool previous_trigger_active);

  uint8_t work_mask_ = 0;
  uint8_t stand_mask_ = 0;
  bool continuous_ = false;
  bool external_input_active_ = false;
  uint16_t last_event_seq_ = 0;
  uint8_t last_jbc_addr_ = 0;

  bool desired_output_enabled_ = false;
  uint16_t desired_power_ = 0;
  bool output_dirty_ = true;
  JbcModuleState jbc_state_;
  OutputModuleState output_state_;
  uint32_t afterrun_deadline_ms_ = 0;
  bool afterrun_active_ = false;
  bool afterrun_power_enabled_ = false;
  uint16_t afterrun_power_ = 300;
};
