#define _CRT_SECURE_NO_WARNINGS
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct lv_obj_t;
struct lv_event_t { void* data; lv_obj_t* target; int code=0; };
using lv_event_cb_t=void (*)(lv_event_t*);
struct lv_timer_t { void* data; };
using lv_color_t=uint32_t;
enum { LV_LABEL_LONG_WRAP, LV_LABEL_LONG_DOT, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_LEFT,
  LV_EVENT_CLICKED, LV_EVENT_VALUE_CHANGED, LV_EVENT_FOCUSED, LV_EVENT_ALL,
  LV_EVENT_READY, LV_EVENT_CANCEL, LV_DIR_VER, LV_ALIGN_BOTTOM_MID, LV_ANIM_OFF };
enum { LV_OBJ_FLAG_HIDDEN=1, LV_OBJ_FLAG_SCROLLABLE=2, LV_OPA_TRANSP=0, LV_OPA_50=128 };
#define UI_FONT_DEFAULT 14
#define UI_FONT_18 18
#define UI_FONT_28 28
static int lv_font_get_line_height(int font){return font+4;}
#define LV_SYMBOL_CLOSE "\xc3\x97"
#define LV_SYMBOL_REFRESH "\xe2\x86\xbb"
struct lv_obj_t {
  lv_obj_t* parent=nullptr;
  int x=0,y=0,width=0,height=18,flags=0,selected=0,font=14;
  bool deleted=false;
  std::string kind="group",text,options;
  lv_event_cb_t callback=nullptr;
};
static int screen_width=480,screen_height=320,label_writes=0;
static uint8_t display_language=1,display_theme=0;
static std::vector<std::unique_ptr<lv_obj_t>> nodes;
static lv_obj_t* lv_obj_create(lv_obj_t* parent) {
  nodes.push_back(std::make_unique<lv_obj_t>());
  auto* n=nodes.back().get(); n->parent=parent;
  n->width=parent ? parent->width : screen_width; return n;
}
static lv_obj_t* lv_layer_top() { static lv_obj_t layer; layer.width=screen_width; layer.height=screen_height; return &layer; }
static int lv_display_get_horizontal_resolution(void*) { return screen_width; }
static int lv_display_get_vertical_resolution(void*) { return screen_height; }
static void lv_obj_set_pos(lv_obj_t* n,int x,int y) { n->x=x; n->y=y; }
static void lv_obj_set_size(lv_obj_t* n,int w,int h) { n->width=w; n->height=h; }
static void lv_obj_set_width(lv_obj_t* n,int w) { n->width=w; }
static void lv_obj_set_height(lv_obj_t* n,int h) { n->height=h; }
static void lv_obj_set_style_pad_all(lv_obj_t*,int,int) {}
static void lv_obj_set_style_radius(lv_obj_t*,int,int) {}
static void lv_obj_set_style_border_width(lv_obj_t*,int,int) {}
static void lv_obj_set_style_border_color(lv_obj_t*,lv_color_t,int) {}
static void lv_obj_set_style_bg_color(lv_obj_t*,lv_color_t,int) {}
static void lv_obj_set_style_bg_opa(lv_obj_t*,int,int) {}
static void lv_obj_set_style_text_color(lv_obj_t*,lv_color_t,int) {}
static void lv_obj_set_style_shadow_width(lv_obj_t*,int,int) {}
static void lv_obj_set_style_text_align(lv_obj_t*,int,int) {}
static void lv_obj_set_style_text_font(lv_obj_t* n,int f,int) { n->font=f; if(n->kind=="label") n->height=f+4; }
static void lv_obj_set_scroll_dir(lv_obj_t*,int) {}
static void lv_obj_scroll_to_view(lv_obj_t*,int) {}
static void lv_obj_add_flag(lv_obj_t* n,int f) { n->flags|=f; }
static void lv_obj_remove_flag(lv_obj_t* n,int f) { n->flags&=~f; }
static void lv_obj_center(lv_obj_t* n) { n->x=(n->parent->width-n->width)/2; n->y=(n->parent->height-n->height)/2; }
static void lv_obj_align(lv_obj_t* n,int,int,int) { n->x=(n->parent->width-n->width)/2; n->y=n->parent->height-n->height; }
static void lv_obj_delete(lv_obj_t* n) {
  n->deleted=true;
  for (auto& child:nodes) if (child->parent==n) lv_obj_delete(child.get());
}
static lv_obj_t* lv_obj_get_child(lv_obj_t* p,int) { for (auto& n:nodes) if(n->parent==p && !n->deleted) return n.get(); return nullptr; }
static void lv_obj_add_event_cb(lv_obj_t* n,lv_event_cb_t cb,int,void*) { n->callback=cb; }
static lv_obj_t* create_kind(lv_obj_t* p,const char* kind) { auto* n=lv_obj_create(p); n->kind=kind; return n; }
static lv_obj_t* lv_label_create(lv_obj_t* p) { return create_kind(p,"label"); }
static void lv_label_set_text(lv_obj_t* n,const char* s) { n->text=s; ++label_writes; }
static const char* lv_label_get_text(lv_obj_t* n) { return n->text.c_str(); }
static void lv_label_set_long_mode(lv_obj_t*,int) {}
static lv_obj_t* lv_button_create(lv_obj_t* p) { return create_kind(p,"button"); }
static lv_obj_t* lv_textarea_create(lv_obj_t* p) { return create_kind(p,"input"); }
static void lv_textarea_set_one_line(lv_obj_t*,bool) {}
static void lv_textarea_set_max_length(lv_obj_t*,int) {}
static void lv_textarea_set_password_mode(lv_obj_t*,bool) {}
static void lv_textarea_set_accepted_chars(lv_obj_t*,const char*) {}
static void lv_textarea_set_text(lv_obj_t* n,const char* s) { n->text=s; }
static const char* lv_textarea_get_text(lv_obj_t* n) { return n->text.c_str(); }
static lv_obj_t* lv_dropdown_create(lv_obj_t* p) { return create_kind(p,"select"); }
static void lv_apply_dropdown_theme(lv_obj_t*) {}
static void lv_dropdown_set_options(lv_obj_t* n,const char* s) { n->options=s; }
static void lv_dropdown_set_selected(lv_obj_t* n,int s) { n->selected=s; }
static int lv_dropdown_get_selected(lv_obj_t* n) { return n->selected; }
static size_t strlcpy(char* dst,const char* src,size_t n) { if(n) snprintf(dst,n,"%s",src); return strlen(src); }
static size_t strlcat(char* dst,const char* src,size_t n) { size_t len=strlen(dst); if(len<n) strlcpy(dst+len,src,n-len); return len+strlen(src); }
static void lv_dropdown_get_selected_str(lv_obj_t* n,char* out,size_t len) {
  std::istringstream opts(n->options); std::string s;
  for(int i=0;i<=n->selected;++i) std::getline(opts,s); strlcpy(out,s.c_str(),len);
}
static lv_obj_t* lv_keyboard_create(lv_obj_t* p) { return create_kind(p,"keyboard"); }
static void lv_keyboard_set_textarea(lv_obj_t*,lv_obj_t*) {}
static lv_timer_t* lv_timer_create(void (*)(lv_timer_t*),int,void* data) { return new lv_timer_t{data}; }
static void lv_timer_delete(lv_timer_t* t) { delete t; }
static void* lv_timer_get_user_data(lv_timer_t* t) { return t->data; }
static void* lv_event_get_user_data(lv_event_t* e) { return e->data; }
static lv_obj_t* lv_event_get_target(lv_event_t* e) { return e->target; }
static int lv_event_get_code(lv_event_t* e) { return e->code; }
static lv_color_t ui_theme_color(uint32_t d,uint32_t l) { return display_theme ? l : d; }
static lv_color_t lv_color_black() { return 0; }
static lv_color_t lv_color_white() { return 0xffffff; }
enum { LV_PART_MAIN,LV_PART_ITEMS,LV_PART_SELECTED,LV_PART_INDICATOR,LV_PART_KNOB,
       LV_STATE_PRESSED,LV_STATE_DISABLED,LV_STATE_FOCUSED,LV_STATE_CHECKED,
       LV_GRAD_DIR_NONE,LV_OPA_COVER=255,LV_RADIUS_CIRCLE=32767,LV_SCROLLBAR_MODE_ACTIVE=1 };
struct FakeClass { const char* kind; };
static const FakeClass lv_label_class{"label"},lv_button_class{"button"},lv_dropdown_class{"select"},
  lv_switch_class{"switch"},lv_textarea_class{"input"},lv_slider_class{"slider"},lv_obj_class{"group"};
static bool lv_obj_check_type(lv_obj_t* n,const FakeClass* cls){return n->kind==cls->kind;}
static int lv_obj_get_child_count(lv_obj_t* p){int count=0;for(auto& n:nodes)if(n->parent==p&&!n->deleted)++count;return count;}
static int lv_obj_get_width(lv_obj_t* n){return n->width;}
static lv_obj_t* lv_dropdown_get_list(lv_obj_t*){return nullptr;}
static int lv_obj_get_height(lv_obj_t* n){return n->height;}
static int lv_obj_get_style_height(lv_obj_t* n,int){return n->height;}
static int lv_obj_get_style_width(lv_obj_t* n,int){return n->width;}
static lv_color_t lv_obj_get_style_text_color(lv_obj_t*,int){return 0xFFFFFF;}
static lv_color_t lv_color_hex(uint32_t n){return n;}
static bool lv_color_eq(lv_color_t a,lv_color_t b){return a==b;}
static void lv_obj_set_style_bg_grad_dir(lv_obj_t*,int,int){}
static void lv_obj_set_style_pad_top(lv_obj_t*,int,int){}
static void lv_obj_set_style_pad_bottom(lv_obj_t*,int,int){}
static void lv_obj_set_style_pad_left(lv_obj_t*,int,int){}
static void lv_obj_set_style_pad_right(lv_obj_t*,int,int){}
static void lv_obj_set_style_pad_row(lv_obj_t*,int,int){}
static void lv_obj_set_style_pad_column(lv_obj_t*,int,int){}
static void lv_obj_set_scrollbar_mode(lv_obj_t*,int){}
static void lv_obj_update_layout(lv_obj_t*){}
static void lv_obj_set_ext_click_area(lv_obj_t*,int){}
static lv_obj_t* lv_event_get_target_obj(lv_event_t* e){return e->target;}
static lv_obj_t* lv_small_button(lv_obj_t* p,int x,int y,int w,const char* s,lv_event_cb_t cb){
  auto* b=lv_button_create(p);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,44);
  auto* l=lv_label_create(b);lv_label_set_text(l,s);lv_obj_center(l);b->callback=cb;return b;
}
#include "pairing_state_fixture.h"
#define private public
#include "../../../Module/DisplayModule_320x480/src/OfeDisplayWifiUi.inc.h"
#undef private

static int absx(lv_obj_t* n) { return n->x+(n->parent ? absx(n->parent) : 0); }
static int absy(lv_obj_t* n) { return n->y+(n->parent ? absy(n->parent) : 0); }
static void bounds() {
  for (auto& owned:nodes) {
    auto* n=owned.get(); if(n->deleted) continue;
    if (!(n->x>=0 && n->y>=0 && n->width>0 && n->height>0))
      std::cerr<<"Bounds: "<<n->kind<<" "<<n->text<<" "<<n->x<<","<<n->y<<" "<<n->width<<"x"<<n->height<<"\n";
    assert(n->x>=0 && n->y>=0 && n->width>0 && n->height>0);
    if(n->parent) assert(n->x+n->width<=n->parent->width);
    if(n->kind=="button" || n->kind=="input" || n->kind=="select") assert(n->height>=44);
    if(n->kind=="label" && n->parent->kind=="button") assert(n->y+n->height<=n->parent->height);
  }
}
static void snapshot(OfeDisplayWifiUi& ui) {
  std::cout<<"["; bool first=true;
  for(auto& owned:nodes) {
    auto* n=owned.get();
    if(n->deleted || n->kind=="group" || n->kind=="keyboard" || n->parent->kind=="button") continue;
    bool in_form=n->parent==ui.form_;
    std::string text=n->kind=="button" ? lv_obj_get_child(n,0)->text : n->text;
    if(n->kind=="select") { std::istringstream options(n->options); for(int i=0;i<=n->selected;++i) std::getline(options,text); }
    if(!first) std::cout<<","; first=false;
    std::cout<<"{\"kind\":"<<std::quoted(n->kind)<<",\"text\":"<<std::quoted(text)
      <<",\"x\":"<<(in_form ? n->x : absx(n))<<",\"y\":"<<(in_form ? n->y : absy(n))
      <<",\"width\":"<<n->width<<",\"height\":"<<n->height<<",\"form\":"<<(in_form ? "true" : "false")<<"}";
  }
  std::cout<<"]\n";
}
int main(int argc,char** argv) {
  if(argc>1) screen_width=atoi(argv[1]); screen_height=screen_width==800 ? 480 : 320;
  if(argc>2) display_language=uint8_t(atoi(argv[2]));
  strcpy(display_wifi.data.config.ssid,"Homeassistant Devices");
  strcpy(display_wifi.data.config.host,"192.168.20.12"); strcpy(display_wifi.data.ip,"192.168.20.42");
  auto& ui=OfeDisplayWifiUi::instance(); ui.open(); bounds();
  assert(ui.root_->y==42 && ui.form_->y==50 && ui.form_->height==screen_height-96);
  assert(ui.form_->width==screen_width-24);
  int writes=label_writes; ui.refresh(); assert(label_writes==writes);
  if(screen_width==800) {
    for(auto& n:nodes) if(n->parent==ui.form_) assert(n->y+n->height<=ui.form_->height);
  }
  // No same-level fields overlap, even on the 480px displays.
  for(auto& a:nodes) for(auto& b:nodes) {
    if(a.get()==b.get() || a->parent!=ui.form_ || b->parent!=ui.form_) continue;
    assert(a->x+a->width<=b->x || b->x+b->width<=a->x || a->y+a->height<=b->y || b->y+b->height<=a->y);
  }
  snapshot(ui);
  lv_event_t focus{&ui,ui.key_}; ui.focusEvent(&focus);
  assert(!(ui.keyboard_->flags&LV_OBJ_FLAG_HIDDEN) && ui.form_->height>=44);
  assert(42+ui.form_->y+ui.form_->height<=42+ui.keyboard_->y);
  ui.hideKeyboard(); assert(ui.keyboard_->flags&LV_OBJ_FLAG_HIDDEN);
  lv_textarea_set_text(ui.ssid_,"Unsaved SSID"); ui.changedEvent(&focus);
  ++display_wifi.data.revision; ui.refresh(); assert(ui.ssid_->text=="Unsaved SSID");
  display_ble.data.active=true; display_ble.data.phase=ofe_ble::CONFIRM;
  ui.refresh(); bounds(); assert(ui.dialog_kind_==2 && ui.dialog_ && absy(ui.dialog_)==42);
  auto* panel=lv_obj_get_child(ui.dialog_,0);
  assert(panel->width<=480 && panel->y>=0 && panel->y+panel->height<=ui.root_->height);
  ui.dialogCancelEvent(&focus); assert(ui.dialog_==nullptr);
  display_ble.data.active=false; display_ble.data.phase=ofe_ble::CANCELLED;
  ui.forgetEvent(&focus); assert(ui.dialog_kind_==1); ui.dialogAcceptEvent(&focus);
  assert(display_wifi.forgets==1 && ui.reload_on_save_);
  ++display_wifi.data.revision; ui.refresh(); assert(ui.ssid_->text=="Homeassistant Devices");
  ui.key_->text="0123456789ABCDEF0123456789ABCDEF";
  ui.saveEvent(&focus); assert(display_wifi.saves==1);
  ui.bleStartEvent(&focus); assert(display_ble.active());
  ui.bleStartEvent(&focus); assert(!display_ble.active());
  ui.close(); assert(ui.root_==nullptr && ui.timer_==nullptr);
  ui.open(); bounds(); ui.close();
}
