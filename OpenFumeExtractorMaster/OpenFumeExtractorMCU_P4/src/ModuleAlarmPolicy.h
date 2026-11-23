#pragma once
#include "ModuleRegistry.h"
#include "DisplayModuleHealth.h"

namespace ofe_module_alarm {
struct State {
  uint16_t faults = 0;
  bool offline = false, connection_warning = false, connection_error = false;
  bool warning() const { return connection_warning || (faults & 0x0002U); }
  bool error() const { return offline || connection_error || (faults & 0xFFFDU); }
};

inline bool supportsConnectionWarning(const ModuleRecord& rec) {
  return (rec.caps & (jbc_rs485::CAP_JBC_BUS | jbc_rs485::CAP_JBC_USB |
                      jbc_rs485::CAP_WELLER_INTERFACE)) != 0 ||
    rec.type == jbc_rs485::MODULE_UNIVERSAL_RS232 || rec.type == jbc_rs485::MODULE_MODBUS_RTU;
}

inline State evaluate(const ModuleRecord& rec, uint8_t active_output, uint8_t preferred_output) {
  using namespace jbc_rs485;
  State state;
  if (!rec.online) { state.offline = true; return state; }
  state.faults = rec.output_fault_mask | rec.io_fault_mask;
  const bool local_link_fault = (rec.type == MODULE_WELLER_ZERO_SMOG ||
    rec.type == MODULE_UNIVERSAL_RS232 || rec.type == MODULE_MODBUS_RTU) && (state.faults & 1U);
  const bool required = rec.addr == active_output || rec.addr == preferred_output;
  // These modules use bit 0 for a missing device connection, not a hardware fault.
  // An old output role or enabled flag must not promote an optional link to an error.
  if (local_link_fault && !required) state.faults &= 0xFFFEU;
  if (!required && !rec.role_output && !rec.output_enabled)
    state.faults &= 0xF8E0U;
  const bool disconnected =
    ((rec.caps & CAP_JBC_BUS) && (!(rec.jbc_link_flags & FAST_FLAG_CONNECTED) || !rec.station_addr)) ||
    ((rec.caps & CAP_JBC_USB) && !(rec.jbc_link_flags & FAST_FLAG_CONNECTED)) ||
    ((rec.caps & CAP_WELLER_INTERFACE) && rec.weller_uart_age_sec > 10) || local_link_fault;
  // A required output must remain an error even when optional warnings are muted.
  state.connection_error = disconnected && required;
  state.connection_warning = disconnected && !required && !rec.connection_warnings_muted && !state.faults;
  return state;
}
}
