#pragma once
#include <stdint.h>

namespace ofe_local_diag {
static constexpr uint16_t RESPONSE_TIMEOUT = 0x0800U;
static constexpr uint16_t CHECKSUM_ERRORS = 0x1000U;
static constexpr uint16_t MODBUS_EXCEPTION = 0x2000U;
static constexpr uint16_t WARNING_MASK = RESPONSE_TIMEOUT | CHECKSUM_ERRORS | MODBUS_EXCEPTION;

// Small, bounded windows: isolated errors never latch a warning.
struct Window {
  uint32_t started = 0, last = 0;
  uint8_t failures = 0, healthy = 0;
  bool active = false;
  void fail(uint32_t now) {
    if (!failures || uint32_t(now - started) > 10000U) {started = now; failures = 0;}
    if (failures < 3) ++failures;
    last = now; healthy = 0;
    if (failures >= 3) active = true;
  }
  void good() {if (++healthy >= 3) reset();}
  void reset() {started = last = 0; failures = healthy = 0; active = false;}
  bool current(uint32_t now) {
    if (failures && uint32_t(now - last) >= 30000U) reset();
    return active;
  }
};
struct State {
  Window timeout, checksum, exception;
  void reset() {timeout.reset(); checksum.reset(); exception.reset();}
  void response(uint32_t now, uint8_t code = 0) {
    timeout.good();
    if (code) exception.fail(now); else exception.good();
  }
  uint16_t faults(uint32_t now) {
    return (timeout.current(now) ? RESPONSE_TIMEOUT : 0) |
           (checksum.current(now) ? CHECKSUM_ERRORS : 0) |
           (exception.current(now) ? MODBUS_EXCEPTION : 0);
  }
};
}
