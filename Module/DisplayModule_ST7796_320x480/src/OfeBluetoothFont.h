#pragma once
#include <lvgl.h>
// Lucide Bluetooth path rasterized at 20px; ISC, docs/licenses/Lucide-ISC.txt.
#define OFE_SYMBOL_BLUETOOTH "\xEE\x80\x81"
namespace ofe_bluetooth_icon {
static const uint8_t bitmap[] = {0x00,0x00,0x00,0x06,0x00,0x00,0x70,0x00,0x07,0x80,0x00,0x6c,0x00,0x66,0x60,0x07,0x6e,0x00,0x3f,0xc0,0x01,0xf8,0x00,0x0f,0x00,0x00,0xf0,0x00,0x1f,0x80,0x03,0xfc,0x00,0x76,0xe0,0x06,0x66,0x00,0x06,0xc0,0x00,0x78,0x00,0x07,0x00,0x00,0x60,0x00,0x00,0x00};
static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {
  {}, {.bitmap_index=0,.adv_w=20*16,.box_w=20,.box_h=20,.ofs_x=0,.ofs_y=0}
};
static const lv_font_fmt_txt_cmap_t cmap[] = {
  {.range_start=0xE001,.range_length=1,.glyph_id_start=1,.unicode_list=nullptr,
   .glyph_id_ofs_list=nullptr,.list_length=0,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}
};
static const lv_font_fmt_txt_dsc_t descriptor = {
  .glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=cmap,.kern_dsc=nullptr,
  .kern_scale=0,.cmap_num=1,.bpp=1,.kern_classes=0,.bitmap_format=0
};
inline const lv_font_t* font() {
  static const lv_font_t value=[]() {
    lv_font_t f={}; f.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt;
    f.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt; f.line_height=20; f.base_line=0;
    f.dsc=&descriptor; return f;
  }();
  return &value;
}
}
