#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <initializer_list>
#include <type_traits>
#include "../../benchmarks/DisplayRgbReference/src/OfeReferencePlan.h"
int main() {
  using namespace ofe_reference;
  static_assert(std::is_trivial<Selection>::value, "Selection is a plain command result");
  Selection stored = DEFAULT_SELECTION;
  assert(valid(stored) && stored.lines == 28 && stored.wifi == 1);
  for (char key = '1'; key <= '4'; ++key) {
    Selection s = DEFAULT_SELECTION;
    assert(command(key, s) && valid(s));
    assert(s.lines == ((key == '2' || key == '4') ? 48U : 28U));
    assert(s.wifi == (key <= '2' ? 1U : 0U));
  }
  for (int key = 0; key < 256; ++key) {
    if (key >= '1' && key <= '4') continue;
    Selection s = {48, 0};
    assert(!command(static_cast<char>(key), s));
    assert(s.lines == 48 && s.wifi == 0);
  }
  for (auto bad : {Selection{0, 1}, Selection{480, 1}, Selection{28, 2}}) {
    assert(!valid(bad));
  }
  static_assert(drawBytes(28) == 44800 && drawBytes(48) == 76800, "Exact SRAM tile sizes");
  assert(!fits(TEST_RESERVE - 1, 0));
  assert(fits(TEST_RESERVE + drawBytes(48), drawBytes(48)));
  assert(!fits(TEST_RESERVE + drawBytes(48) - 1, drawBytes(48)));
  assert(!fits(SIZE_MAX, SIZE_MAX));
  puts("PASS: all reference commands, invalid input preserves selection, exact tile sizes and reserve boundaries");
}
