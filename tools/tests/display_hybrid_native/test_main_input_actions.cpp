#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "generated_main_input_actions.h"

int main() {
  MasterScheduler::InputActionRule rule;
  rule.enabled = true;
  for (uint8_t kind = 0; kind < 4; ++kind) {
    scheduler.calls = 0;
    rule.target_type = uint8_t(MasterScheduler::INPUT_TGT_SELECT_INPUT_NONE + kind);
    rule.target_addr = kind ? 32 : 0;
    rule.target_bit = kind == 3 ? 254 : kind == 2 ? 7 : 0;
    rule.last_active = false; rule.edge_armed = false;
    scheduler.applyInputRuleTarget(rule, true);
    assert(scheduler.calls == 0); // Loading a high rule must not select anything.
    scheduler.applyInputRuleTarget(rule, false);
    scheduler.applyInputRuleTarget(rule, true);
    assert(scheduler.calls == 1 && scheduler.type == kind);
    assert(scheduler.addr == rule.target_addr && scheduler.bit == rule.target_bit);
    assert(scheduler.persisted);
    scheduler.applyInputRuleTarget(rule, true);
    scheduler.applyInputRuleTarget(rule, false);
    assert(scheduler.calls == 1); // Neither held HIGH nor falling LOW writes settings.
    scheduler.applyInputRuleTarget(rule, true);
    assert(scheduler.calls == 2);
  }
  scheduler.calls = 0;
  for (const char* signal : {"extractor:select_input:0:0:0", "extractor:select_input:1:0:0",
       "extractor:select_input:1:16:0", "extractor:select_input:2:32:7", "extractor:select_input:3:96:254"}) {
    assert(logic_output_is_event_signal(signal));
    const unsigned before = scheduler.calls;
    assert(logic_apply_output_signal(signal, false) && scheduler.calls == before);
    assert(logic_apply_output_signal(signal, true) && scheduler.calls == before + 1);
    assert(scheduler.persisted);
  }
  for (const char* signal : {"extractor:select_input:4:32:0", "extractor:select_input:3:256:20",
       "extractor:select_input:3:32:255", "extractor:select_input:2:32:16",
       "extractor:select_input:0:32:0", "extractor:select_input:1:16:1",
       "extractor:select_input:3:0:20"}) {
    const unsigned before = scheduler.calls;
    assert(!logic_apply_output_signal(signal, true) && scheduler.calls == before);
  }
  assert(logic_output_is_event_signal("extractor:select_output:32"));
  assert(logic_apply_output_signal("extractor:select_output:32", true));
  assert(scheduler.output == 32);
  std::puts("PASS: actual main-input routing/Designer actions, safe startup, rising edges, persistence, all input kinds and invalid selections");
}
