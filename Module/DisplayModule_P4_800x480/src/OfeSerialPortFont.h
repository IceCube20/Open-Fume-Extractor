#pragma once
#include <lvgl.h>

// User-provided serial-port.svg, viewBox 0 0 24 24; cropped to its 14x19 ink bounds.
#define OFE_SYMBOL_SERIAL_PORT "\xEE\x80\x80"
namespace ofe_serial_port {
static const uint8_t bitmap[] = {
  0x3f, 0xf0, 0xff, 0xcf, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x7f, 0x8d, 0xfe, 0xf7, 0xfb, 0xdf, 0xef, 0x7f, 0xbd, 0xfe, 0xc0, 0x00, 0x03, 0x00, 0x0c, 0x00, 0x30, 0x00, 0xc0, 0x03, 0x00, 0x0c, 0x00, 0x30, 0x00
};
static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {
  {},
  {.bitmap_index=0, .adv_w=22*16, .box_w=14, .box_h=19, .ofs_x=4, .ofs_y=0}
};
static const lv_font_fmt_txt_cmap_t cmap[] = {
  {.range_start=0xE000, .range_length=1, .glyph_id_start=1,
   .unicode_list=nullptr, .glyph_id_ofs_list=nullptr, .list_length=0,
   .type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}
};
static const lv_font_fmt_txt_dsc_t descriptor = {
  .glyph_bitmap=bitmap, .glyph_dsc=glyphs, .cmaps=cmap,
  .kern_dsc=nullptr, .kern_scale=0, .cmap_num=1, .bpp=1,
  .kern_classes=0, .bitmap_format=0
};
inline const lv_font_t* font(const lv_font_t* fallback) {
  static lv_font_t value = []() {
    lv_font_t f = {};
    f.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt;
    f.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt;
    f.line_height=20;
    f.base_line=1;
    f.dsc=&descriptor;
    return f;
  }();
  value.fallback=fallback;
  return &value;
}
inline void center_icon(lv_obj_t* label, bool wireless) {
  const lv_font_t* f = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  lv_font_glyph_dsc_t g = {};
  if (!lv_font_get_glyph_dsc(f, &g, wireless ? 0xF1EB : 0xE000, 0)) return;
  // Center the actual ink, not the fallback font's baseline, in the 20px target.
  const int32_t ink_top = f->line_height - f->base_line - g.box_h - g.ofs_y;
  lv_obj_set_style_pad_top(label, (20 - (int32_t)g.box_h) / 2 - ink_top, 0);
}
}
