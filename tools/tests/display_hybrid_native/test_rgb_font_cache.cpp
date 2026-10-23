#include <lvgl.h>
#include <src/core/lv_global.h>
#include <src/draw/lv_draw_buf_private.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../../../Module/DisplayModule_800x480/src/OfeRgbFontCache.h"

extern "C" {
LV_FONT_DECLARE(openfume_montserrat_14_de);
lv_global_t lv_global;
// The host test exercises the actual LVGL font and draw-buffer implementation,
// not its embedded allocator, event loop or display hardware.
void* lv_malloc(size_t n) { return malloc(n); }
void* lv_malloc_zeroed(size_t n) { return calloc(1, n); }
void lv_free(void* p) { free(p); }
void* lv_realloc(void* p, size_t n) { return realloc(p, n); }
// These APIs are referenced by unused helpers in the linked LVGL units.
// Abort if the font test ever accidentally depends on them.
lv_fs_res_t lv_fs_open(lv_fs_file_t*, const char*, lv_fs_mode_t) { abort(); }
lv_fs_res_t lv_fs_close(lv_fs_file_t*) { abort(); }
lv_fs_res_t lv_fs_write(lv_fs_file_t*, const void*, uint32_t, uint32_t*) { abort(); }
void lv_image_cache_drop(const void*) { abort(); }
lv_result_t lv_draw_buf_convert_premultiply(lv_draw_buf_t*) { abort(); }
}

static void compare_glyph(const lv_font_t* original, const lv_font_t* cached,
                          uint32_t letter, uint32_t next) {
  lv_font_glyph_dsc_t a = {}, b = {};
  const bool found = lv_font_get_glyph_dsc(original, &a, letter, next);
  assert(found == lv_font_get_glyph_dsc(cached, &b, letter, next));
  assert(a.adv_w == b.adv_w && a.box_w == b.box_w && a.box_h == b.box_h);
  assert(a.ofs_x == b.ofs_x && a.ofs_y == b.ofs_y && a.is_placeholder == b.is_placeholder);
  if (!found) return;
  assert(a.gid.index == b.gid.index);
  if (a.resolved_font != original) {
    assert(a.resolved_font == b.resolved_font);  // Symbols still use the original fallback.
    return;
  }
  assert(b.resolved_font == cached && b.format == LV_FONT_GLYPH_FORMAT_A8);
  if (!a.box_w || !a.box_h) return;
  const uint32_t stride = lv_draw_buf_width_to_stride(a.box_w, LV_COLOR_FORMAT_A8);
  const uint32_t size = stride * a.box_h;
  std::vector<uint8_t> expected(size + 32, 0xAD), actual(size + 32, 0xBC);
  lv_draw_buf_t out_a = {}, out_b = {};
  assert(lv_draw_buf_init(&out_a, a.box_w, a.box_h, LV_COLOR_FORMAT_A8,
                         stride, expected.data() + 16, size) == LV_RESULT_OK);
  assert(lv_draw_buf_init(&out_b, b.box_w, b.box_h, LV_COLOR_FORMAT_A8,
                         stride, actual.data() + 16, size) == LV_RESULT_OK);
  assert(lv_font_get_glyph_bitmap(&a, &out_a) == &out_a);
  assert(lv_font_get_glyph_bitmap(&b, &out_b) == &out_b);
  assert(memcmp(out_a.data, out_b.data, size) == 0);
  const auto* raw = static_cast<const uint8_t*>(lv_font_get_glyph_static_bitmap(&b));
  assert(raw && b.stride == stride && memcmp(raw, out_a.data, size) == 0);
  for (size_t i = 0; i < 16; ++i) {
    assert(expected[i] == 0xAD && expected[size + 16 + i] == 0xAD);
    assert(actual[i] == 0xBC && actual[size + 16 + i] == 0xBC);
  }
}

int main() {
  lv_draw_buf_init_handlers();
  const auto* source = &openfume_montserrat_14_de;
#if !OFE_RGB_FONT_CACHE
  ofe_rgb_font_cache::FontCache cache(source);
  assert(!cache.init() && cache.font() == source && cache.bytes() == 0);
  assert(!font_heap::allocations && !font_heap::live);
  puts("PASS: cache-disabled build keeps the exact original font without allocations");
#else
  unsigned comparisons = 0;
  {
    ofe_rgb_font_cache::FontCache cache(source);
    assert(cache.font() == source && cache.bytes() == 0);
    font_heap::fail = true;
    assert(!cache.init() && cache.font() == source && !font_heap::live);
    font_heap::fail = false;
    assert(cache.init());
    assert(cache.bytes() == font_heap::live && cache.bytes() < 64 * 1024);
    const auto* font = cache.font();
    assert(font != source && font->static_bitmap);
    assert(font->line_height == source->line_height && font->base_line == source->base_line);
    assert(font->underline_position == source->underline_position);
    assert(font->underline_thickness == source->underline_thickness);
    assert(font->fallback == source->fallback && font->kerning == source->kerning);
    assert(font->subpx == source->subpx);
    const auto& before = *static_cast<const lv_font_fmt_txt_dsc_t*>(source->dsc);
    const auto& after = *static_cast<const lv_font_fmt_txt_dsc_t*>(font->dsc);
    assert(before.bpp == 4 && after.bpp == 8);
    assert(after.cmaps == before.cmaps && after.kern_dsc == before.kern_dsc);
    const unsigned allocations = font_heap::allocations;
    assert(cache.init() && font_heap::allocations == allocations);
    // Every Latin-1 glyph, German umlaut, empty glyph, bullet, symbol fallback,
    // unsupported character and next-character pair retains its layout/pixels.
    const uint32_t extra[] = {0, '\t', 0x2022, 0xF0F3, 0xF1EB, 0x1F600};
    for (uint32_t c = 0; c < 256; ++c) {
      for (uint32_t next = 0; next < 256; ++next) {
        if (c == '\t') continue;  // Tab has special blank-glyph metrics, no ink.
        compare_glyph(source, font, c, next);
        ++comparisons;
      }
    }
    for (auto c : extra) { compare_glyph(source, font, c, 0); ++comparisons; }
    assert(font_heap::allocations == allocations);  // No allocation during rendering.
    printf("PASS: %u original/cached glyph comparisons; %zu PSRAM bytes; no further cache allocations\n",
           comparisons, cache.bytes());
  }
  assert(!font_heap::live);
  ofe_rgb_font_cache::FontCache null_cache(nullptr);
  assert(!null_cache.init() && null_cache.font() == nullptr);
  auto bad = *source;
  auto descriptor = *static_cast<const lv_font_fmt_txt_dsc_t*>(source->dsc);
  bad.dsc = &descriptor;
  descriptor.bpp = 1;
  ofe_rgb_font_cache::FontCache unsupported(&bad);
  assert(!unsupported.init() && unsupported.font() == &bad);
  descriptor.bpp = 4;
  auto map = descriptor.cmaps[0];
  descriptor.cmaps = &map;
  descriptor.cmap_num = 1;
  map.glyph_id_start = 2;
  ofe_rgb_font_cache::FontCache noncontiguous(&bad);
  assert(!noncontiguous.init() && noncontiguous.font() == &bad);
  map.glyph_id_start = 1;
  map.range_length = 1024;
  ofe_rgb_font_cache::FontCache oversized(&bad);
  assert(!oversized.init() && oversized.font() == &bad);
  assert(!font_heap::live);
  puts("PASS: allocation failure, original fallback, idempotence and unsupported/oversized map rejection");
#endif
}
