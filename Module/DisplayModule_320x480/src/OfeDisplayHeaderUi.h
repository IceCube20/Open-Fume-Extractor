#pragma once
#include <lvgl.h>
#include <string.h>

namespace ofe_display_header {
inline void center_glyph(lv_obj_t* label, uint32_t codepoint) {
  const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  lv_font_glyph_dsc_t glyph = {};
  if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0)) return;
  // LVGL positions fallback glyphs using the label font's line metrics.
  const int32_t ink_top = font->line_height - font->base_line - glyph.box_h - glyph.ofs_y;
  const int32_t top = (20 - (int32_t)glyph.box_h) / 2 - ink_top;
  if (lv_obj_get_style_pad_top(label, LV_PART_MAIN) != top) lv_obj_set_style_pad_top(label, top, 0);
}

inline void center_clock(lv_obj_t* label) {
  const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  const bool date = strchr(lv_label_get_text(label), '\n') != nullptr;
  const int32_t height = font->line_height * (date ? 2 : 1) +
    (date ? lv_obj_get_style_text_line_space(label, LV_PART_MAIN) : 0);
  lv_obj_set_height(label, 34);
  const int32_t top = (34 - height) / 2;
  if (lv_obj_get_style_pad_top(label, LV_PART_MAIN) != top)
    lv_obj_set_style_pad_top(label, top, 0);
}
}
