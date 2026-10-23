#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ofe_rgb_tile {
#if defined(__GNUC__)
#define OFE_RGB_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define OFE_RGB_INLINE __forceinline
#else
#define OFE_RGB_INLINE inline
#endif

OFE_RGB_INLINE void copy180Pair(const uint16_t*& src, uint16_t*& dst, uint32_t mask) {
  src -= 2;
  uint32_t pair;
#if defined(__GNUC__)
  // SDK flags may disable ordinary memcpy builtins; keep these four bytes inline.
  __builtin_memcpy(&pair, __builtin_assume_aligned(src, 4), sizeof(pair));
#else
  memcpy(&pair, src, sizeof(pair));
#endif
  pair = ((pair >> 16) | (pair << 16)) ^ mask;
#if defined(__GNUC__)
  __builtin_memcpy(__builtin_assume_aligned(dst, 4), &pair, sizeof(pair));
#else
  memcpy(dst, &pair, sizeof(pair));
#endif
  dst += 2;
}

// Aligned little-endian RGB565 pairs: reverse two pixels per load/store.
// memcpy keeps the accesses alias-safe; the compiler can use aligned 32-bit
// transfers after assume_aligned. Odd rows/columns retain the scalar path.
OFE_RGB_INLINE void copy180Direct(uint16_t* framebuffer, int stride, const uint16_t* source,
                          int dst_x, int dst_y, int w, int h, uint16_t xor_mask) {
  if (!framebuffer || !source || w <= 0 || h <= 0) return;
  const uint16_t* src = source + (size_t)w * h;
  uint16_t* row = framebuffer + (size_t)dst_y * stride + dst_x;
  for (int y = 0; y < h; ++y) {
    uint16_t* dst = row;
    int remaining = w;
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if ((((uintptr_t)src ^ (uintptr_t)dst) & 3U) == 0) {
      if (((uintptr_t)dst & 3U) && remaining) {
        *dst++ = (*--src) ^ xor_mask;
        --remaining;
      }
      const uint32_t mask = (uint32_t)xor_mask | ((uint32_t)xor_mask << 16);
      while (remaining >= 8) {
        copy180Pair(src, dst, mask);
        copy180Pair(src, dst, mask);
        copy180Pair(src, dst, mask);
        copy180Pair(src, dst, mask);
        remaining -= 8;
      }
      while (remaining >= 2) {
        copy180Pair(src, dst, mask);
        remaining -= 2;
      }
    }
#endif
    while (remaining >= 8) {
      dst[0] = (*--src) ^ xor_mask;
      dst[1] = (*--src) ^ xor_mask;
      dst[2] = (*--src) ^ xor_mask;
      dst[3] = (*--src) ^ xor_mask;
      dst[4] = (*--src) ^ xor_mask;
      dst[5] = (*--src) ^ xor_mask;
      dst[6] = (*--src) ^ xor_mask;
      dst[7] = (*--src) ^ xor_mask;
      dst += 8;
      remaining -= 8;
    }
    while (remaining--) *dst++ = (*--src) ^ xor_mask;
    row += stride;
  }
}
#undef OFE_RGB_INLINE

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
