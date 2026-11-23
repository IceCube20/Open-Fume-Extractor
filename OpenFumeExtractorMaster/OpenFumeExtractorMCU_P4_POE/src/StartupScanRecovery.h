#pragma once

#include <stdint.h>

class StartupScanRecovery {
public:
  void arm(uint32_t now) { started_ms_ = now; next_ = 0; }
  void cancel() { next_ = 3; }

  bool takeDue(uint32_t now, bool blocked) {
    if (next_ >= 3) return false;
    const uint32_t elapsed = now - started_ms_;
    // Do not replay boot scans after a long update or maintenance operation.
    if (elapsed >= 30000UL) { cancel(); return false; }
    const uint32_t deadlines[] = {2000UL, 5000UL, 10000UL};
    if (blocked || elapsed < deadlines[next_]) return false;
    ++next_;
    return true;
  }

private:
  uint32_t started_ms_ = 0;
  uint8_t next_ = 3;
};
