#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ofe_rgb_tile {
// Keep PSRAM reads sequential; the caller supplies its existing internal tile
// and in-place rotation routine. The original LVGL pixels are never modified.
template <typename Rotate>
bool copy180(uint16_t* framebuffer, int screen_w, int screen_h,
             int x, int y, int w, int h, const uint16_t* source,
             uint16_t* scratch, size_t scratch_pixels, uint16_t xor_mask,
             Rotate rotate) {
  if (!framebuffer || !source || !scratch || w <= 0 || h <= 0 ||
      x < 0 || y < 0 || w > screen_w || h > screen_h ||
      x > screen_w - w || y > screen_h - h || scratch_pixels < (size_t)w) return false;

  const int rows_per_chunk = (int)(scratch_pixels / (size_t)w);
  const int dst_x = screen_w - x - w;
  for (int first = 0; first < h; first += rows_per_chunk) {
    const int rows = (h - first < rows_per_chunk) ? h - first : rows_per_chunk;
    const size_t count = (size_t)rows * w;
    memcpy(scratch, source + (size_t)first * w, count * sizeof(uint16_t));
    rotate(scratch, (uint32_t)count, xor_mask);
    uint16_t* dst = framebuffer + (size_t)(screen_h - y - first - rows) * screen_w + dst_x;
    for (int row = 0; row < rows; ++row) {
      memcpy(dst, scratch + (size_t)row * w, (size_t)w * sizeof(uint16_t));
      dst += screen_w;
    }
  }
  return true;
}
}
