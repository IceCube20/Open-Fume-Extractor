#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static std::vector<std::string> packets;
static uint8_t cs4(const char* cmd) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < 4; ++i) sum = (uint8_t)(sum + (uint8_t)cmd[i]);
  return sum;
}
static void weller_queue_raw(const char* data, uint8_t length) {
  packets.emplace_back(data, length);
}
#include "generated_commands.inc.h"

static void expect(const char* first, const char* second = nullptr) {
  assert(packets.size() == (second ? 2 : 1));
  const char* expected[] = {first, second};
  for (size_t i = 0; i < packets.size(); ++i) {
    assert(packets[i].size() == 5 && packets[i].substr(0, 4) == expected[i]);
    assert((uint8_t)packets[i][4] == cs4(expected[i]));
  }
  packets.clear();
}
int main() {
  weller_set_filter_runtime(0); expect("f000", "g000");
  weller_set_filter_runtime(1); expect("f006");
  weller_set_filter_runtime(59); expect("f006");
  weller_set_filter_runtime(60); expect("f006");
  weller_set_filter_runtime(480); expect("f048");
  weller_set_filter_runtime(9600); expect("f960");
  weller_set_filter_runtime(65535); expect("f999");
  weller_reset_filter_runtime(); expect("g000");
  std::puts("PASS: production Weller disable sequence, checksums, bounds and independent reset");
}
