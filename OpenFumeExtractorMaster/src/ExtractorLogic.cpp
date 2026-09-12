#include "ExtractorLogic.h"

uint16_t ExtractorLogic::targetPowerForJbc() const {
  uint16_t target = 1000;
  if (jbc_state_.valid) {
    switch (jbc_state_.suction_level) {
      case 0: target = 1000; break; // High
      case 1: target = 600; break;  // Medium
      case 2: target = 300; break;  // Low
      default: {
        uint16_t percent = jbc_state_.select_flow;
        // select_flow is stored as tenths of percent: 100 = 10%, 1000 = 100%.
        if (percent >= 100) percent = (percent + 5U) / 10U;
        if (percent < 10) percent = 10;
        if (percent > 100) percent = 100;
        target = percent * 10U;
        break;
      }
    }
  }
  if (target < output_min_power_) target = output_min_power_;
  if (target > output_max_power_) target = output_max_power_;
  return target;
}

void ExtractorLogic::setOutputPowerBounds(uint16_t min_power, uint16_t max_power) {
  if (min_power < 1) min_power = 1;
  if (max_power > 1000) max_power = 1000;
  if (max_power < min_power) max_power = min_power;
  const bool changed = output_min_power_ != min_power || output_max_power_ != max_power;
  output_min_power_ = min_power;
  output_max_power_ = max_power;
  if (!changed || !desired_output_enabled_) return;

  uint16_t next_power = afterrun_active_ && afterrun_power_enabled_ ? afterrun_power_ : targetPowerForJbc();
  if (next_power < output_min_power_) next_power = output_min_power_;
  if (next_power > output_max_power_) next_power = output_max_power_;
  if (next_power != desired_power_) {
    desired_power_ = next_power;
    output_power_dirty_ = true;
  }
}

bool ExtractorLogic::applyOutputState(bool previous_trigger_active) {
  const bool next_enabled = continuous_ || automation_continuous_ || work_mask_ != 0 || external_input_active_;
  const uint16_t target_power = targetPowerForJbc();
  uint16_t next_power = next_enabled ? target_power : 0;

  if (next_enabled) {
    afterrun_deadline_ms_ = 0;
    afterrun_active_ = false;
  } else if (desired_output_enabled_ && previous_trigger_active) {
    const uint16_t delay_sec = jbc_state_.valid ? jbc_state_.delay_work_sec : 0;
    if (delay_sec > 0) {
      afterrun_deadline_ms_ = millis() + (uint32_t)delay_sec * 1000UL;
      afterrun_active_ = true;
      next_power = afterrun_power_enabled_ ? afterrun_power_ : (desired_power_ ? desired_power_ : target_power);
    }
  }

  const bool effective_enabled = next_enabled || afterrun_active_;
  if (afterrun_active_) next_power = afterrun_power_enabled_ ? afterrun_power_ : target_power;

  const bool enable_changed = effective_enabled != desired_output_enabled_;
  const bool power_changed = next_power != desired_power_;
  if (enable_changed || power_changed) {
    desired_output_enabled_ = effective_enabled;
    desired_power_ = next_power;
    if (enable_changed) output_enable_dirty_ = true;
    if (power_changed) output_power_dirty_ = true;
    return true;
  }
  return false;
}

bool ExtractorLogic::updateFromFastPoll(uint8_t module_addr, const jbc_rs485::FastPollState& fast) {
  const bool next_continuous = (fast.flags & jbc_rs485::FAST_FLAG_CONTINUOUS) != 0;
  const bool state_changed =
    module_addr != last_jbc_addr_ ||
    fast.event_seq != last_event_seq_ ||
    fast.work_mask != work_mask_ ||
    fast.stand_mask != stand_mask_ ||
    next_continuous != continuous_;

  if (!state_changed) return false;

  const bool had_trigger = work_mask_ != 0 || external_input_active_;

  last_jbc_addr_ = module_addr;
  last_event_seq_ = fast.event_seq;
  work_mask_ = fast.work_mask;
  stand_mask_ = fast.stand_mask;
  continuous_ = next_continuous;

  applyOutputState(had_trigger);

  return true;
}

bool ExtractorLogic::updateAggregateJbcState(uint8_t work_mask, uint8_t stand_mask, bool continuous) {
  const bool state_changed =
    work_mask != work_mask_ ||
    stand_mask != stand_mask_ ||
    continuous != continuous_;

  if (!state_changed) return false;

  const bool had_trigger = work_mask_ != 0 || external_input_active_;

  work_mask_ = work_mask;
  stand_mask_ = stand_mask;
  continuous_ = continuous;

  applyOutputState(had_trigger);

  return true;
}

bool ExtractorLogic::updateExternalInput(bool active) {
  if (active == external_input_active_) return false;
  const bool had_trigger = work_mask_ != 0 || external_input_active_;
  external_input_active_ = active;
  applyOutputState(had_trigger);
  return true;
}

bool ExtractorLogic::updateAutomationContinuous(bool active) {
  if (active == automation_continuous_) return false;
  const bool had_trigger = work_mask_ != 0 || external_input_active_;
  automation_continuous_ = active;
  applyOutputState(had_trigger);
  return true;
}

void ExtractorLogic::updateControlSettings(const JbcModuleState& state) {
  const bool had_trigger = work_mask_ != 0 || external_input_active_;
  jbc_state_.valid = true;
  jbc_state_.suction_level = state.suction_level;
  jbc_state_.select_flow = state.select_flow;
  jbc_state_.delay_work_sec = state.delay_work_sec;
  jbc_state_.delay_stand_sec = state.delay_stand_sec;
  jbc_state_.stand_intakes = state.stand_intakes;
  jbc_state_.continuous = state.continuous;
  applyOutputState(had_trigger);
}
void ExtractorLogic::clearJbcConnectionState() {
  jbc_state_.module_addr = 0;
  jbc_state_.link_flags = 0;
  jbc_state_.jbc_addr = 0;
  jbc_state_.station_addr = 0;
  jbc_state_.base_state = 0;
  jbc_state_.work_mask = 0;
  jbc_state_.stand_mask = 0;
  jbc_state_.device_id_len = 0;
  memset(jbc_state_.device_id, 0, sizeof(jbc_state_.device_id));
  jbc_state_.last_update_ms = millis();
}
void ExtractorLogic::updateJbcState(uint8_t module_addr, const JbcModuleState& state) {
  jbc_state_ = state;
  jbc_state_.valid = true;
  jbc_state_.module_addr = module_addr;
  jbc_state_.last_update_ms = millis();
  if (desired_output_enabled_) {
    const uint16_t target_power = targetPowerForJbc();
    const uint16_t next_power = afterrun_active_ && afterrun_power_enabled_ ? afterrun_power_ : target_power;
    if (next_power != desired_power_) {
      desired_power_ = next_power;
      output_power_dirty_ = true;
    }
  }
}

void ExtractorLogic::setAfterrunPowerProfile(bool enabled, uint16_t power) {
  if (power < 100) power = 100;
  if (power > 1000) power = 1000;
  afterrun_power_enabled_ = enabled;
  afterrun_power_ = power;
  if (afterrun_active_ && desired_output_enabled_) {
    const uint16_t next_power = enabled ? afterrun_power_ : targetPowerForJbc();
    if (next_power != desired_power_) {
      desired_power_ = next_power;
      output_power_dirty_ = true;
    }
  }
}
void ExtractorLogic::updateOutputState(const OutputModuleState& state) {
  output_state_ = state;
  output_state_.valid = true;
  output_state_.last_update_ms = millis();
}

void ExtractorLogic::tick() {
  if (!afterrun_active_) return;
  if ((int32_t)(millis() - afterrun_deadline_ms_) < 0) return;

  afterrun_active_ = false;
  afterrun_deadline_ms_ = 0;
  if (!continuous_ && !automation_continuous_ && work_mask_ == 0 && !external_input_active_ && desired_output_enabled_) {
    desired_output_enabled_ = false;
    desired_power_ = 0;
    output_enable_dirty_ = true;
    output_power_dirty_ = true;
  }
}

uint32_t ExtractorLogic::afterrunLeftMs() const {
  if (!afterrun_active_ || !afterrun_deadline_ms_) return 0;
  const int32_t left = (int32_t)(afterrun_deadline_ms_ - millis());
  return left > 0 ? (uint32_t)left : 0;
}
