#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
#include "../../../Module/DisplayModule_800x480/src/OfeRgbTileCopy.h"

#ifndef DISPLAY_LVGL_LARGE_TILES
#define DISPLAY_LVGL_LARGE_TILES 1
#endif
#define DISPLAY_LVGL_FULL_REFRESH 0
#define DISPLAY_ROTATION 2
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
alignas(64) static uint8_t render_memory[800 * 96 * 2];
alignas(64) static uint8_t internal_memory[800 * 32 * 2];
static bool allocation_fails;
static unsigned allocation_calls;
static void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps) {
  ++allocation_calls;
  assert(alignment == 64 && bytes == sizeof(render_memory));
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return allocation_fails ? nullptr : render_memory;
}
static struct { void println(const char*) {} } Serial;
static uint8_t* lvgl_draw_buf = internal_memory;
static bool lvgl_draw_buf_psram = false;
static uint32_t lvgl_buf_pixels = 800 * 32, lvgl_buf_bytes = sizeof(internal_memory);
static uint16_t* lvgl_transfer_scratch = nullptr;
static uint32_t lvgl_transfer_scratch_pixels = 0;
#include "generated_rgb_tiles.inc.h"

static void verify(int x, int y, int w, int h, size_t scratch_size, uint16_t mask) {
  constexpr size_t guard = 16, frame_pixels = 800 * 480;
  std::vector<uint16_t> actual(frame_pixels + 2 * guard, 0xBEEF), expected = actual;
  std::vector<uint16_t> scratch(scratch_size + 2 * guard, 0xCDEF);
  std::vector<uint16_t> input(w * h);
  for (size_t i = 0; i < input.size(); ++i) input[i] = uint16_t(i * 173 + x + y);
  const auto original = input;
  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < w; ++col) {
      expected[guard + (479 - y - row) * 800 + 799 - x - col] = input[row * w + col] ^ mask;
    }
  }
  assert(ofe_rgb_tile::copy180(actual.data() + guard, 800, 480, x, y, w, h,
    input.data(), scratch.data() + guard, scratch_size, mask, rotate_rgb565_180_inplace));
  assert(actual == expected && input == original);
  for (size_t i = 0; i < guard; ++i) {
    assert(scratch[i] == 0xCDEF && scratch[scratch_size + guard + i] == 0xCDEF);
  }
}

static std::vector<uint16_t> render_view(int lines, unsigned& calls) {
  std::vector<uint16_t> frame(800 * 480, 0xBEEF), scratch(800 * 32);
  for (int y = 52; y < 420; y += lines) {
    int h = std::min(lines, 420 - y);
    std::vector<uint16_t> tile(784 * h);
    for (int r = 0; r < h; ++r)
      for (int c = 0; c < 784; ++c) tile[r * 784 + c] = uint16_t((y + r) * 800 + c + 8);
    assert(ofe_rgb_tile::copy180(frame.data(), 800, 480, 8, y, 784, h, tile.data(),
      scratch.data(), scratch.size(), 0, rotate_rgb565_180_inplace));
    ++calls;
  }
  return frame;
}

int main() {
  for (uint16_t mask : { uint16_t(0), uint16_t(0xFFFF) }) {
    verify(0, 0, 800, 480, 800 * 32, mask);
    verify(8, 52, 784, 368, 800 * 32, mask);
    for (int w : {1, 2, 7, 31, 319, 783}) {
      for (int h : {1, 3, 95, 97}) {
        verify(0, 0, w, h, w, mask);
        verify(800 - w, 480 - h, w, h, 800 * 32, mask);
      }
    }
  }
  uint16_t one = 42;
  assert(!ofe_rgb_tile::copy180(&one, 1, 1, -1, 0, 1, 1, &one, &one, 1, 0, rotate_rgb565_180_inplace));
  assert(!ofe_rgb_tile::copy180(&one, 1, 1, 0, 0, 1, 1, &one, &one, 0, 0, rotate_rgb565_180_inplace));
  assert(one == 42);

  unsigned old_calls = 0, new_calls = 0;
  auto old_frame = render_view(32, old_calls);
  auto new_frame = render_view(96, new_calls);
  assert(old_frame == new_frame && old_calls == 12 && new_calls == 4);

  prepare_large_lvgl_tiles(0);
  assert(allocation_calls == 0);
#if DISPLAY_LVGL_LARGE_TILES
  allocation_fails = true;
  prepare_large_lvgl_tiles(800);
  assert(lvgl_draw_buf == internal_memory && !lvgl_draw_buf_psram && !lvgl_transfer_scratch);
  assert(lvgl_buf_bytes == sizeof(internal_memory) && lvgl_buf_pixels == 800 * 32);
  allocation_fails = false;
  prepare_large_lvgl_tiles(800);
  assert(lvgl_draw_buf == render_memory && lvgl_draw_buf_psram);
  assert(lvgl_transfer_scratch == (uint16_t*)internal_memory && lvgl_transfer_scratch_pixels == 800 * 32);
  assert(lvgl_buf_bytes == sizeof(render_memory) && lvgl_buf_pixels == 800 * 96);
  const auto calls = allocation_calls;
  prepare_large_lvgl_tiles(800);
  assert(allocation_calls == calls);
  puts("RGB tiles: pixel-identical 12->4 tiles, rotation/inversion/edges/guards, source preserved, allocation fallback and idempotence passed");
#else
  prepare_large_lvgl_tiles(800);
  assert(allocation_calls == 0);
  assert(lvgl_draw_buf == internal_memory && !lvgl_draw_buf_psram && !lvgl_transfer_scratch);
  assert(lvgl_buf_bytes == sizeof(internal_memory) && lvgl_buf_pixels == 800 * 32);
  assert(lvgl_transfer_scratch_pixels == 0);
  puts("RGB tiles: standard build keeps SRAM rendering, no PSRAM tile allocation");
#endif
}
