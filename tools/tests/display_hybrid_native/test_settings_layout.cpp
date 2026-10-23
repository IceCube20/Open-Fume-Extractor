#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct String : std::string {
  using std::string::string;
  String(int n) : std::string(std::to_string(n)) {}
};
using lv_color_t = uint32_t;
using lv_event_t = void;
using lv_event_cb_t = void (*)(lv_event_t*);
struct lv_font_t { int height; };
static lv_font_t small_font{18}, value_font{22};
#define UI_FONT_DEFAULT (&small_font)
#define UI_FONT_18 (&value_font)
enum { LV_OPA_TRANSP, LV_OBJ_FLAG_SCROLLABLE, LV_EVENT_VALUE_CHANGED, LV_TEXT_ALIGN_CENTER };
struct lv_obj_t {
  lv_obj_t* parent;
  int id, x=0, y=0, width=0, height=0, selected=0;
  std::string kind="group", text, options;
  lv_event_cb_t callback=nullptr;
};
static std::vector<std::unique_ptr<lv_obj_t>> nodes;
static lv_obj_t *ui_system_brightness, *ui_system_language, *ui_system_theme,
  *ui_system_screensaver, *ui_system_backlight_mode, *ui_system_backlight_delay,
  *ui_system_benchmark_button;
#if defined(ST7796_PERCEPTUAL_BACKLIGHT)
static lv_obj_t* ui_system_color_profile;
static uint8_t display_color_profile=2;
#endif
static uint8_t display_language=1, display_brightness_pct=85, display_theme=0;
static uint8_t screensaver_timeout_min=5;
static const char* tr(const char* en, const char* de) { return display_language ? de : en; }
static lv_color_t lv_color_hex(uint32_t c) { return c; }
static lv_color_t ui_theme_color(uint32_t dark, uint32_t light) { return display_theme ? light : dark; }
static lv_obj_t* lv_obj_create(lv_obj_t* parent) {
  nodes.push_back(std::make_unique<lv_obj_t>());
  auto* n=nodes.back().get(); n->parent=parent; n->id=int(nodes.size()-1); return n;
}
static void lv_obj_set_pos(lv_obj_t* n,int x,int y) { n->x=x; n->y=y; }
static void lv_obj_set_size(lv_obj_t* n,int w,int h) { n->width=w; n->height=h; }
static void lv_obj_set_height(lv_obj_t* n,int h) { n->height=h; }
static void lv_obj_set_style_pad_all(lv_obj_t*,int,int) {}
static void lv_obj_set_style_border_width(lv_obj_t*,int,int) {}
static void lv_obj_set_style_bg_opa(lv_obj_t*,int,int) {}
static void lv_obj_set_style_bg_color(lv_obj_t*,lv_color_t,int) {}
static void lv_obj_set_style_radius(lv_obj_t*,int,int) {}
static void lv_obj_set_style_text_align(lv_obj_t*,int,int) {}
static void lv_obj_clear_flag(lv_obj_t*,int) {}
static lv_obj_t* lv_label(lv_obj_t* p,const char* text,int x,int y,lv_color_t,
                         const lv_font_t* font=UI_FONT_DEFAULT,int width=0) {
  auto* n=lv_obj_create(p); n->kind="label"; n->text=text;
  lv_obj_set_pos(n,x,y); lv_obj_set_size(n,width,font->height); return n;
}
static lv_obj_t* lv_dropdown_create(lv_obj_t* p) { auto* n=lv_obj_create(p); n->kind="select"; return n; }
static void lv_dropdown_set_options(lv_obj_t* n,const char* text) { n->options=text; }
static void lv_dropdown_set_selected(lv_obj_t* n,int v) { n->selected=v; }
static void lv_apply_dropdown_theme(lv_obj_t*) {}
static void lv_obj_add_event_cb(lv_obj_t* n,lv_event_cb_t cb,int,void*) { n->callback=cb; }
static void lv_brightness_down_event(lv_event_t*) {}
static void lv_brightness_up_event(lv_event_t*) {}
static void lv_language_event(lv_event_t*) {}
static void lv_theme_event(lv_event_t*) {}
static void lv_screensaver_timeout_event(lv_event_t*) {}
static void lv_backlight_settings_event(lv_event_t*) {}
static void lv_st7796_color_profile_event(lv_event_t*) {}
static void display_benchmark_open_event(lv_event_t*) {}
class OfeDisplayWifiUi { public: static void openEvent(lv_event_t*) {} };
static uint8_t screensaver_selection_from_timeout(uint8_t) { return 3; }
static lv_obj_t* lv_small_button(lv_obj_t* p,int x,int y,int w,const char* text,lv_event_cb_t cb) {
  auto* n=lv_obj_create(p); n->kind="button"; n->text=text; n->callback=cb;
  lv_obj_set_pos(n,x,y); lv_obj_set_size(n,w,38); return n;
}
#include "generated_settings_controls.inc.h"
#include "../../../Module/DisplayModule_320x480/src/OfeDisplaySettingsUi.inc.h"

static int absolute_x(const lv_obj_t* n) { return n->x+(n->parent ? absolute_x(n->parent) : 0); }
static int absolute_y(const lv_obj_t* n) { return n->y+(n->parent ? absolute_y(n->parent) : 0); }
int main(int argc,char** argv) {
  int width=argc>1 ? std::atoi(argv[1]) : 456;
  display_language=argc>2 ? std::atoi(argv[2]) : 1;
  auto* root=lv_obj_create(nullptr); root->width=width; root->height=2000;
  lv_create_display_settings(root,width);
  assert(ui_system_language->selected==display_language && ui_system_theme->selected==display_theme);
  assert(ui_system_screensaver->selected==3);
  assert(ui_system_language->callback==lv_language_event && ui_system_theme->callback==lv_theme_event);
  assert(ui_system_screensaver->callback==lv_screensaver_timeout_event);
  assert(ui_system_backlight_mode->callback==lv_backlight_settings_event);
  assert(ui_system_backlight_delay->callback==lv_backlight_settings_event);
  assert(ui_system_benchmark_button->callback==display_benchmark_open_event);
#if defined(ST7796_PERCEPTUAL_BACKLIGHT)
  assert(ui_system_color_profile->selected==2);
  assert(ui_system_color_profile->callback==lv_st7796_color_profile_event);
#endif
  ui_system_backlight_mode->selected=1;
  ui_system_backlight_delay->selected=4;
  for (auto& owned:nodes) {
    auto* n=owned.get();
    if (!n->parent) continue;
    assert(n->x>=0 && n->y>=0 && n->width>0 && n->height>0);
    assert(n->x+n->width<=n->parent->width && n->y+n->height<=n->parent->height);
    if (n->kind=="button" || n->kind=="select") assert(n->height>=44);
    if (n->kind=="select") {
      std::istringstream options(n->options);
      for (int i=0;i<=n->selected;++i) assert(std::getline(options,n->text));
    }
  }
  for (auto& a:nodes) for (auto& b:nodes) {
    if (a->id>=b->id || a->kind=="group" || b->kind=="group") continue;
    assert(absolute_x(a.get())+a->width<=absolute_x(b.get()) ||
      absolute_x(b.get())+b->width<=absolute_x(a.get()) ||
      absolute_y(a.get())+a->height<=absolute_y(b.get()) ||
      absolute_y(b.get())+b->height<=absolute_y(a.get()));
  }
  std::cout<<"[";
  bool first=true;
  for (auto& n:nodes) {
    if (n->kind=="group") continue;
    if (!first) std::cout<<",";
    first=false;
    std::cout<<"{\"kind\":"<<std::quoted(n->kind)<<",\"text\":"<<std::quoted(n->text)
      <<",\"x\":"<<absolute_x(n.get())<<",\"y\":"<<absolute_y(n.get())
      <<",\"width\":"<<n->width<<",\"height\":"<<n->height<<"}";
  }
  std::cout<<"]\n";
}
