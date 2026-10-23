#pragma once
#include <stdint.h>
#include <vector>
#include <assert.h>
class FakeWire {
public:
  bool begin_ok = false;
  int begin_calls = 0, failures = 0;
  uint8_t address = 0;
  std::vector<uint8_t> bytes;
  std::vector<std::vector<uint8_t>> frames;
  bool begin(int sda, int scl, int frequency) {
    assert(sda == 45 && scl == 46 && frequency == 400000);
    ++begin_calls;
    return begin_ok;
  }
  void beginTransmission(uint8_t addr) { address = addr; bytes.clear(); }
  void write(uint8_t value) { bytes.push_back(value); }
  int endTransmission() {
    assert(address == 0x2F && bytes.size() == 2);
    frames.push_back(bytes);
    if (failures) { --failures; return 2; }
    return 0;
  }
};
inline FakeWire Wire;
