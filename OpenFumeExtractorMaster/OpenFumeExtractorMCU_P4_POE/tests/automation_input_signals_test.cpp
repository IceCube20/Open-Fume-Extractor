// Run: g++ -std=c++11 -Isrc tests/automation_input_signals_test.cpp -o /tmp/ofe_signal_test && /tmp/ofe_signal_test
#include "AutomationInputSignals.h"
#include <assert.h>
#include <stdio.h>

using namespace ofe_automation_inputs;

int main() {
  // Legacy JBC FAE: linked + station + no Work = Stand, even when Stand mask is 0.
  assert(jbcStandActive(true, true, true, true, 0, 0));
  assert(!jbcStandActive(true, true, true, true, 1, 0));
  assert(!jbcStandActive(true, true, true, false, 0, 0));
  assert(!jbcStandActive(true, true, false, true, 0, 0));
  assert(!jbcStandActive(false, true, true, true, 0, 0));
  // USB must NOT derive Stand from idle: use its real Stand mask instead.
  assert(!jbcStandActive(true, false, true, true, 0, 0));
  assert(jbcStandActive(true, false, true, true, 0, 2));
  assert(!jbcStandActive(true, false, false, true, 0, 2));

  // Assigned/selected is independent of the extractor running state.
  assert(mainOutputSelected(0x20, 0x20));
  assert(mainOutputSelected(0x20, 0x20) && !mainOutputActive(0x20, 0x20, false));
  assert(mainOutputSelected(0x20, 0x20) && mainOutputActive(0x20, 0x20, true));
  assert(!mainOutputSelected(0x21, 0x20));
  assert(!mainOutputActive(0x21, 0x20, true));
  assert(!mainOutputActive(0, 0, true));
  assert(!mainOutputSelected(0, 0));
  // With automatic selection, the assigned output can still be active.
  assert(!mainOutputSelected(0, 0x20) && mainOutputActive(0x20, 0x20, true));

  assert(warningFaults(0) == 0 && errorFaults(0) == 0);
  assert(warningFaults(0x0002) == 0x0002 && errorFaults(0x0002) == 0);
  assert(warningFaults(0x0100) == 0 && errorFaults(0x0100) == 0x0100);
  // With both bits, warning must still be independently active.
  assert(warningFaults(0x0102) == 0x0002 && errorFaults(0x0102) == 0x0100);
  assert(warningFaults(0x0006) == 0x0002 && errorFaults(0x0006) == 0x0004);
  puts("AutomationInputSignals: all tests passed");
  return 0;
}
