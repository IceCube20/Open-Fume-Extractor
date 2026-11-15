#pragma once
#include <cstdint>
#include <vector>
#include <cassert>
constexpr int RMT_TX_MODE = 1, RMT_MEM_NUM_BLOCKS_2 = 2;
constexpr uint32_t RMT_WAIT_FOR_EVER = UINT32_MAX;
struct rmt_data_t { uint32_t duration0 = 0, level0 = 0, duration1 = 0, level1 = 0; };
inline unsigned rmt_writes = 0, rmt_inits = 0;
inline std::vector<rmt_data_t> last_frame;
inline bool rmtInit(int pin, int mode, int blocks, uint32_t frequency) {
  assert(pin == 4 && mode == RMT_TX_MODE && blocks == 2 && frequency == 10000000);
  ++rmt_inits; return true;
}
inline bool rmtWrite(int pin, rmt_data_t* symbols, size_t count, uint32_t timeout) {
  assert(pin == 4 && count == 64 && timeout == RMT_WAIT_FOR_EVER);
  last_frame.assign(symbols, symbols + count); ++rmt_writes; return true;
}
