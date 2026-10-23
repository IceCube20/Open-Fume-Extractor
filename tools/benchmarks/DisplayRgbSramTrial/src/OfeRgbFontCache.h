#pragma once
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <stddef.h>
#include <stdint.h>

#ifndef OFE_RGB_FONT_CACHE
#define OFE_RGB_FONT_CACHE 0
#endif

namespace ofe_rgb_font_cache {

// Cache the original glyphs, not a replacement font. LVGL itself expands their
// alpha samples; metrics, character maps, kerning and fallback remain unchanged.
class FontCache {
 public:
  explicit FontCache(const lv_font_t* source) : source_(source) {}
  FontCache(const FontCache&) = delete;
  FontCache& operator=(const FontCache&) = delete;
  ~FontCache() { if (storage_) heap_caps_free(storage_); }

  const lv_font_t* font() const { return storage_ ? &font_ : source_; }
  size_t bytes() const { return bytes_; }

  bool init() {
    if (storage_) return true;
    // This fast path is verified against LVGL's static-A8 bitmap interface.
    // Older backends keep the original font without changing their behavior.
#if OFE_RGB_FONT_CACHE && LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6
    if (!source_ || source_->get_glyph_dsc != lv_font_get_glyph_dsc_fmt_txt ||
        source_->get_glyph_bitmap != lv_font_get_bitmap_fmt_txt || !source_->dsc) return false;
    const auto& original = *static_cast<const lv_font_fmt_txt_dsc_t*>(source_->dsc);
    if (original.bpp != 4 || original.bitmap_format != LV_FONT_FMT_TXT_PLAIN ||
        original.stride || original.are_glyphs_dynamic_loaded || !original.cmaps ||
        !original.glyph_dsc || !original.glyph_bitmap || !original.cmap_num) return false;

    uint32_t count = 1;
    for (uint16_t i = 0; i < original.cmap_num; ++i) {
      const auto& map = original.cmaps[i];
      if (map.type != LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY || map.glyph_id_start != count ||
          !map.range_length || map.range_length > 1024U - count) return false;
      count += map.range_length;
    }
    constexpr size_t limit = 64U * 1024U;
    constexpr size_t alignment = LV_DRAW_BUF_ALIGN;
    static_assert(alignment && !(alignment & (alignment - 1)), "Draw alignment must be a power of two");
    size_t total = aligned(count * sizeof(lv_font_fmt_txt_glyph_dsc_t), alignment);
    for (uint32_t i = 1; i < count; ++i) {
      const auto& g = original.glyph_dsc[i];
      const size_t size = static_cast<size_t>(lv_draw_buf_width_to_stride(g.box_w, LV_COLOR_FORMAT_A8)) * g.box_h;
      if (total > limit || size > limit - total) return false;
      total = aligned(total + size, alignment);
    }
    if (total > limit) return false;
    auto* block = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      alignment < 64 ? 64 : alignment, total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!block) return false;
    auto* glyphs = reinterpret_cast<lv_font_fmt_txt_glyph_dsc_t*>(block);
    const size_t table_bytes = aligned(count * sizeof(*glyphs), alignment);
    auto* pixels = block + table_bytes;
    size_t offset = 0;
    glyphs[0] = original.glyph_dsc[0];
    for (uint32_t i = 1; i < count; ++i) {
      glyphs[i] = original.glyph_dsc[i];
      glyphs[i].bitmap_index = offset;
      const auto& g = original.glyph_dsc[i];
      const uint32_t stride = lv_draw_buf_width_to_stride(g.box_w, LV_COLOR_FORMAT_A8);
      const uint32_t size = stride * g.box_h;
      if (g.box_w && g.box_h) {
        lv_font_glyph_dsc_t input = {};
        input.resolved_font = source_;
        input.gid.index = i;
        input.box_w = g.box_w;
        input.box_h = g.box_h;
        input.format = LV_FONT_GLYPH_FORMAT_A4;
        lv_draw_buf_t output = {};
        if (lv_draw_buf_init(&output, g.box_w, g.box_h, LV_COLOR_FORMAT_A8,
                             stride, pixels + offset, size) != LV_RESULT_OK ||
            source_->get_glyph_bitmap(&input, &output) != &output) {
          heap_caps_free(block);
          return false;
        }
      }
      offset = aligned(offset + size, alignment);
    }
    descriptor_ = original;
    descriptor_.glyph_bitmap = pixels;
    descriptor_.glyph_dsc = glyphs;
    descriptor_.bpp = 8;
    descriptor_.stride = LV_DRAW_BUF_STRIDE_ALIGN;
    font_ = *source_;
    font_.dsc = &descriptor_;
    font_.static_bitmap = 1;
    bytes_ = total;
    storage_ = block;
    return true;
#else
    return false;
#endif
  }

 private:
  static size_t aligned(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
  }
  const lv_font_t* source_;
  lv_font_t font_ = {};
  lv_font_fmt_txt_dsc_t descriptor_ = {};
  void* storage_ = nullptr;
  size_t bytes_ = 0;
};

}  // namespace ofe_rgb_font_cache
