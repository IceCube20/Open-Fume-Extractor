#include <lvgl.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "OfeJbcDetailState.h"
#include "DisplayValueStream.h"
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
using std::isfinite;
struct String : std::string {
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  String(unsigned n) : std::string(std::to_string(n)) {}
  unsigned length()const{return unsigned(size());}
  int indexOf(const char* text)const{const auto at=find(text);return at==npos?-1:int(at);}
  void replace(const char* from,const char* to){size_t p=0;while((p=find(from,p))!=npos){std::string::replace(p,strlen(from),to);p+=strlen(to);}}
  String(int n,int base) {char buffer[20];snprintf(buffer,sizeof(buffer),base==16?"%X":"%d",n);assign(buffer);}
};
extern "C" {
LV_FONT_DECLARE(openfume_montserrat_14_de);
LV_FONT_DECLARE(openfume_montserrat_18_de);
LV_FONT_DECLARE(openfume_montserrat_22_de);
LV_FONT_DECLARE(openfume_montserrat_28_de);
}
#define UI_FONT_DEFAULT (&openfume_montserrat_14_de)
#define UI_FONT_18 (&openfume_montserrat_18_de)
#define UI_FONT_22 (&openfume_montserrat_22_de)
#define UI_FONT_28 (&openfume_montserrat_28_de)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
static void* heap_caps_malloc(size_t n,int) { return malloc(n); }
static uint32_t now_ms=100;
static uint32_t millis() { return now_ms; }
static bool german=true,dark=true;
static bool master_link_lost() { return false; }
static const char* tr(const char* en,const char* de) { return german?de:en; }
static lv_color_t ui_theme_color(uint32_t d,uint32_t l) { return lv_color_hex(dark?d:l); }
static ofe_jbc_detail::State state;
static ofe_jbc_detail::State* display_jbc_state=&state;
static int display_jbc_mux;
struct DisplayUniversalEntity {
  uint8_t id=0,type=0,flags=3;bool valid=true;int16_t value=0,min_value=0,max_value=100;
  char label[64]={},unit[12]={},options[192]={},text[32]={};
};
constexpr unsigned DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX=32;
enum {DISPLAY_UNI_SENSOR=1,DISPLAY_UNI_BINARY_SENSOR,DISPLAY_UNI_NUMBER,DISPLAY_UNI_SWITCH,
      DISPLAY_UNI_BUTTON,DISPLAY_UNI_TEXT,DISPLAY_UNI_SELECT};
static struct {
  uint8_t addr=0x12,type=0,suction=0,universal_entity_count=0,weller_speed=55;
  uint8_t fw_major=1,fw_minor=1,fw_patch=83;char fw_suffix[16]="RC";
  bool valid=true,main_configured=true;uint32_t caps=0;uint16_t select_flow=550,delay_work=12,delay_stand=5,output_power=550,io_outputs=0;
  uint32_t universal_descriptor_crc=1,universal_entity_total=0;uint16_t weller_programmed=960;
  char name[32]="JBC USB Werkbank",io_main_alias[40]="Absaugung Werkbank";
  DisplayUniversalEntity universal_entities[32];
  struct{char model[32]="DDE_CAP26_06";}jbc_usb_core;
} selected_module;
static lv_obj_t *ui_module_detail_screen,*ui_detail_values,*ui_detail_title,*ui_detail_controls;
static String jbc_usb_core_detail_text(decltype(selected_module)&) { return "JBC USB"; }
static void lv_set_text_c(lv_obj_t* obj,const char* text) { if(strcmp(lv_label_get_text(obj),text))lv_label_set_text(obj,text); }
static void lv_set_text(lv_obj_t* obj,const String& text) { lv_set_text_c(obj,text.c_str()); }
static lv_obj_t* lv_label(lv_obj_t* parent,const char* text,int x,int y,lv_color_t color,const lv_font_t* font,int width) {
  auto* obj=lv_label_create(parent);lv_label_set_text(obj,text);lv_obj_set_pos(obj,x,y);
  lv_obj_set_style_text_color(obj,color,0);lv_obj_set_style_text_font(obj,font,0);
  lv_obj_set_style_pad_all(obj,0,0);lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);
  if(width>0){lv_obj_set_width(obj,width);lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT);}return obj;
}
static lv_obj_t* lv_small_button(lv_obj_t* parent,int x,int y,int width,const char* text,lv_event_cb_t cb) {
  auto* b=lv_button_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,width,44);lv_obj_set_style_radius(b,6,0);
  lv_obj_set_style_bg_color(b,ui_theme_color(0x25364A,0xDAE4EE),0);
  auto* l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);return b;
}
static void lv_back_to_modules_event(lv_event_t*) {}
static void lv_update_module_detail();
#include "OfeDisplayDetailUi.inc.h"
#include "OfeJbcDetailUi.h"
static void lv_update_module_detail(){lv_update_jbc_detail(true);}
static lv_obj_t *ui_system_brightness,*ui_system_language,*ui_system_theme,*ui_system_color_profile;
static lv_obj_t *ui_system_screensaver,*ui_system_backlight_mode,*ui_system_backlight_delay,*ui_system_benchmark_button;
static uint8_t display_language=1,display_theme=0,display_color_profile=1,display_brightness_pct=55,screensaver_timeout_min=5;
static int settings_event=-1;
#define SETTINGS_EVENT(name,id) static void name(lv_event_t*){settings_event=id;}
SETTINGS_EVENT(lv_brightness_down_event,0)
SETTINGS_EVENT(lv_brightness_up_event,1)
SETTINGS_EVENT(lv_language_event,2)
SETTINGS_EVENT(lv_theme_event,3)
SETTINGS_EVENT(lv_st7796_color_profile_event,4)
SETTINGS_EVENT(lv_screensaver_timeout_event,5)
SETTINGS_EVENT(lv_backlight_settings_event,6)
SETTINGS_EVENT(display_benchmark_open_event,7)
SETTINGS_EVENT(lv_show_home_event,9)
SETTINGS_EVENT(lv_show_modules_event,10)
SETTINGS_EVENT(lv_show_alarms_event,11)
SETTINGS_EVENT(lv_show_system_event,12)
static size_t strlcpy(char* dst,const char* src,size_t n){if(n)snprintf(dst,n,"%s",src);return strlen(src);}
static size_t strlcat(char* dst,const char* src,size_t n){size_t len=strlen(dst);if(len<n)strlcpy(dst+len,src,n-len);return len+strlen(src);}
#include "../display_hybrid_native/pairing_state_fixture.h"
#define private public
#include "OfeDisplayWifiUi.inc.h"
#undef private
static uint8_t screensaver_selection_from_timeout(uint8_t minutes) { return minutes==5?3:0; }
#include "backlight_controls.inc.h"
#include "OfeDisplaySettingsUi.inc.h"
static int screen_width,screen_height;
static bool check_jbc_bounds=true;
static std::vector<uint16_t> pixels,draw;
static void flush(lv_display_t* d,const lv_area_t* a,uint8_t* data) {
  const auto* src=reinterpret_cast<uint16_t*>(data);
  for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x)pixels[y*screen_width+x]=*src++;
  lv_display_flush_ready(d);
}
static void render(const char* name) {
  lv_obj_update_layout(ui_module_detail_screen);lv_refr_now(nullptr);
  std::string file=std::string(name)+".ppm";
#ifdef ST7796_PERCEPTUAL_BACKLIGHT
  file="st7796-"+file;
#endif
  FILE* f=fopen(file.c_str(),"wb");assert(f);
  fprintf(f,"P6\n%d %d\n255\n",screen_width,screen_height);
  for(uint16_t c:pixels){unsigned char rgb[]={uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);
  if(!check_jbc_bounds)return;
  // Every visible control belongs inside its row, with no label/control overlap.
  for(uint8_t i=0;i<ofe_jbc_detail::WINDOW;++i){
    if(lv_obj_has_flag(jbc_detail_row[i],LV_OBJ_FLAG_HIDDEN))continue;
    lv_area_t row,label;lv_obj_get_coords(jbc_detail_row[i],&row);lv_obj_get_coords(jbc_detail_label[i],&label);
    if(!(label.x1>=row.x1&&label.x2<=row.x2&&label.y1>=row.y1&&label.y2<=row.y2))fprintf(stderr,"%s row %d bounds %d,%d..%d,%d label %d,%d..%d,%d\n",name,i,row.x1,row.y1,row.x2,row.y2,label.x1,label.y1,label.x2,label.y2);
    assert(label.x1>=row.x1&&label.x2<=row.x2&&label.y1>=row.y1&&label.y2<=row.y2);
    for(auto* obj:{jbc_detail_minus[i],jbc_detail_spin[i],jbc_detail_plus[i],jbc_detail_switch[i],jbc_detail_select[i]})if(obj){
      lv_area_t a;lv_obj_get_coords(obj,&a);assert(a.x1>label.x2&&a.x2<=row.x2&&a.y1>=row.y1&&a.y2<=row.y2);
    }
    for(uint8_t j=0;j<i;++j)if(!lv_obj_has_flag(jbc_detail_row[j],LV_OBJ_FLAG_HIDDEN)){
      lv_area_t other;lv_obj_get_coords(jbc_detail_row[j],&other);
      assert(row.x1>other.x2||other.x1>row.x2||row.y1>other.y2||other.y1>row.y2);
    }
  }
}
static lv_obj_t* check_keypad() {
  auto* dialog=lv_obj_get_child(jbc_detail_editor,0);
  auto* keyboard=lv_obj_get_child(dialog,-1);
  lv_obj_update_layout(ui_module_detail_screen);
  lv_area_t box,keys,input,title,error;
  lv_obj_get_coords(dialog,&box);lv_obj_get_coords(keyboard,&keys);
  lv_obj_get_coords(jbc_detail_input,&input);lv_obj_get_coords(lv_obj_get_child(dialog,0),&title);
  lv_obj_get_coords(jbc_detail_error,&error);
  assert(lv_obj_get_width(dialog)==(screen_width>=640?400:360));
  assert(box.x1>=0&&box.x2<screen_width&&box.y1>=44&&box.y2<screen_height);
  if(!(keys.x1>=box.x1&&keys.x2<=box.x2&&keys.y1>error.y2&&keys.y2<=box.y2))fprintf(stderr,"keypad box %d,%d..%d,%d keys %d,%d..%d,%d error %d,%d..%d,%d\n",box.x1,box.y1,box.x2,box.y2,keys.x1,keys.y1,keys.x2,keys.y2,error.x1,error.y1,error.x2,error.y2);
  assert(keys.x1>=box.x1&&keys.x2<=box.x2&&keys.y1>error.y2&&keys.y2<=box.y2);
  if(!(input.y1>title.y2&&error.y1>input.y2))fprintf(stderr,"title %d..%d input %d..%d error %d..%d\n",title.y1,title.y2,input.y1,input.y2,error.y1,error.y2);
  assert(input.y1>title.y2&&error.y1>input.y2);
  assert(lv_obj_get_height(keyboard)==(screen_width>=640?176:152));
  assert(lv_obj_has_flag(jbc_detail_editor,LV_OBJ_FLAG_CLICKABLE));
  return keyboard;
}
static void fill(uint8_t section) {
  using namespace ofe_jbc_detail;
  state.pending=false;state.select(0x12,section,0);state.received=true;state.total=24;state.ports=4;state.flags=3;state.revision=123;state.loaded=state.refreshed=0xFFF;
  const char* labels[]={"Station","Modell","Stationsklasse","JBC Verbindung","Work","Stand","Verbindungsmodus","Stationsfehler","Software","Hardware","Device ID","Protokoll"};
  const char* values[]={"Werkbank L\303\266tstation","DDE_CAP26_06","L\303\266tstation","Verbunden","Aus","An","MONITOR","Kein Fehler","8887002","0022058","849158E467A340159646170D6B1595EF","P02"};
  for(int i=0;i<WINDOW;++i){auto& r=state.rows[i];r.id=i;copy(r.label,sizeof(r.label),labels[i]);copy(r.text,sizeof(r.text),values[i]);
    if(section==COUNTERS){copy(r.label,sizeof(r.label),i%2?"Teilz\303\244hler eingesteckt":"Reinigung kontinuierlich");copy(r.text,sizeof(r.text),"123d 16h 50m");}
    if(section==SETTINGS){r.writable=true;r.kind=i%4==1?SWITCH:i%4==2?SELECT:NUMBER;r.value=3150;r.minimum=0;r.maximum=4500;
      copy(r.label,sizeof(r.label),i%4==1?"Port 1 Temperaturlevel aktiv":i%4==2?"Port 1 Temperaturlevel":"Port 1 Standby-Temperatur");copy(r.text,sizeof(r.text),"315 \302\260C");if(r.kind!=NUMBER)r.value=10;
    }
  }
  lv_update_module_detail();
}
static void settings() {
  display_language=german?1:0;display_theme=dark?0:1;
  auto* body=ofe_display_ui::content(ui_module_detail_screen);
  lv_create_display_settings(body,screen_width-24);
  const char* labels[]={tr("Home","Start"),tr("Modules","Module"),tr("Alarms","Alarme"),"System"};
  const lv_event_cb_t callbacks[]={lv_show_home_event,lv_show_modules_event,lv_show_alarms_event,lv_show_system_event};
  ofe_display_ui::navigation(ui_module_detail_screen,3,labels,callbacks);
  lv_obj_update_layout(ui_module_detail_screen);
  const uint32_t children=lv_obj_get_child_count(ui_module_detail_screen);
  for(uint8_t i=0;i<4;++i){
    auto* button=lv_obj_get_child(ui_module_detail_screen,children-4+i);
    assert(lv_obj_get_y(button)==screen_height-48&&lv_obj_get_height(button)==44);
    lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);assert(settings_event==9+i);
  }
  assert(lv_obj_get_x(body)==12&&lv_obj_get_y(body)==48);
  assert(lv_obj_get_y(body)+lv_obj_get_height(body)==screen_height-56);
  auto* appearance=lv_obj_get_parent(ui_system_brightness);
  auto* rest=lv_obj_get_parent(ui_system_screensaver);
  assert(lv_obj_get_x(rest)==(screen_width>=640?lv_obj_get_width(appearance)+16:0));
  assert(lv_obj_get_y(rest)==(screen_width>=640?0:lv_obj_get_height(appearance)+16));
  for(auto* field:{ui_system_language,ui_system_theme,ui_system_screensaver,ui_system_backlight_mode,ui_system_backlight_delay}){
    assert(lv_obj_get_height(field)==44);
    lv_area_t a,b;lv_obj_get_coords(field,&a);lv_obj_get_coords(lv_obj_get_parent(field),&b);
    assert(a.x1>=b.x1&&a.x2<=b.x2&&a.y1>=b.y1&&a.y2<=b.y2);
    assert(lv_color_to_u32(lv_obj_get_style_bg_color(field,LV_PART_MAIN))==lv_color_to_u32(ofe_display_ui::field()));
  }
  assert(lv_obj_get_y(ui_system_language)==lv_obj_get_y(ui_system_theme));
  assert(lv_dropdown_get_option_count(ui_system_backlight_delay)==60);
  char name[80];snprintf(name,sizeof(name),"system-%d-%s",screen_width,dark?"de-dark":"en-light");render(name);
  lv_obj_scroll_to_y(body,lv_obj_get_scroll_bottom(body),LV_ANIM_OFF);render((std::string(name)+"-bottom").c_str());
  for(auto pair:{std::pair<lv_obj_t*,int>{ui_system_language,2},{ui_system_theme,3},{ui_system_screensaver,5},{ui_system_backlight_mode,6},{ui_system_backlight_delay,6}}){
    settings_event=-1;lv_obj_send_event(pair.first,LV_EVENT_VALUE_CHANGED,nullptr);assert(settings_event==pair.second);
  }
#ifdef ST7796_PERCEPTUAL_BACKLIGHT
  assert(ui_system_color_profile&&lv_obj_get_y(ui_system_color_profile)+44<lv_obj_get_height(appearance));
  lv_obj_send_event(ui_system_color_profile,LV_EVENT_VALUE_CHANGED,nullptr);assert(settings_event==4);
#endif
  lv_obj_send_event(ui_system_benchmark_button,LV_EVENT_CLICKED,nullptr);assert(settings_event==7);
  for(uint8_t i=0;i<4;++i)lv_obj_delete(lv_obj_get_child(ui_module_detail_screen,-1));
  lv_obj_delete(body);
}
static void pairing() {
  auto& ui=OfeDisplayWifiUi::instance();
  strcpy(display_wifi.data.config.ssid,"Homeassistant Devices");strcpy(display_wifi.data.config.host,"192.168.20.12");
  display_ble.data.active=false;ui.open();lv_obj_update_layout(lv_layer_top());
  assert(lv_obj_get_y(ui.root_)==42&&lv_obj_get_y(ui.form_)==50);
  for(auto* field:{ui.mode_,ui.ssid_,ui.password_,ui.host_,ui.key_,ui.networks_})assert(lv_obj_get_height(field)==44);
  char name[80];snprintf(name,sizeof(name),"pairing-%d-%s",screen_width,dark?"de-dark":"en-light");render(name);
  lv_obj_send_event(ui.key_,LV_EVENT_FOCUSED,nullptr);assert(!lv_obj_has_flag(ui.keyboard_,LV_OBJ_FLAG_HIDDEN));
  render((std::string(name)+"-keyboard").c_str());
  lv_obj_send_event(ui.keyboard_,LV_EVENT_CANCEL,nullptr);assert(lv_obj_has_flag(ui.keyboard_,LV_OBJ_FLAG_HIDDEN));
  lv_textarea_set_text(ui.ssid_,"Unsaved SSID");ui.dirty_=true;++display_wifi.data.revision;ui.refresh();
  assert(!strcmp(lv_textarea_get_text(ui.ssid_),"Unsaved SSID"));
  display_ble.data.active=true;display_ble.data.phase=ofe_ble::CONFIRM;ui.refresh();assert(ui.dialog_);
  render((std::string(name)+"-confirm").c_str());
  auto* panel=lv_obj_get_child(ui.dialog_,0);lv_area_t a;lv_obj_get_coords(panel,&a);
  assert(a.x1>=0&&a.x2<screen_width&&a.y1>=42&&a.y2<screen_height);
  ui.close();assert(!ui.root_&&!display_ble.active());
}
#include "SolderIronIcon.h"
#include "OpenFumeBootLogo.h"
static const int HEX=16;
static lv_obj_t *ui_module_list_screen,*ui_module_list,*ui_module_update_notice;
static lv_obj_t *ui_module_rows[17],*ui_module_row_status_stripes[17],*ui_module_row_names[17],*ui_module_row_meta[17];
static lv_obj_t *ui_alarm_screen,*ui_alarm_list,*ui_alarm_rows[8],*ui_alarm_titles[8],*ui_alarm_details[8];
static lv_obj_t *ui_detail_meta,*ui_detail_status,*ui_detail_work_icon;
static lv_obj_t *ui_benchmark_screen,*ui_benchmark_list,*ui_benchmark_status,*ui_benchmark_result;
static lv_obj_t *ui_boot_screen,*ui_boot_fw,*ui_boot_addr,*ui_status_screen,*ui_status_msg;
static lv_obj_t *ui_update_screen,*ui_update_target,*ui_update_status,*ui_update_percent,*ui_update_phase,*ui_update_bar;
static lv_obj_t *ui_update_written,*ui_update_size,*ui_update_speed,*ui_update_detail,*ui_update_hint;
static lv_obj_t *ui_screensaver_screen,*ui_screensaver_brand,*ui_screensaver_clock,*ui_screensaver_state;
static lv_obj_t *ui_screensaver_power,*ui_screensaver_modules,*ui_screensaver_alarm,*ui_screensaver_power_bar;
static lv_obj_t *ui_screensaver_info,*ui_screensaver_hint,*ui_screensaver_eco_icon;
static lv_obj_t *ui_dashboard_screen,*ui_home_content,*ui_home_output_card,*ui_home_settings_card;
static lv_obj_t *ui_home_output,*ui_home_power,*ui_home_rpm,*ui_home_afterrun,*ui_home_power_bar;
static lv_obj_t *ui_home_continuous_button,*ui_home_fan_button,*ui_home_light_button,*ui_home_io1_button,*ui_home_io2_button;
static lv_obj_t *ui_home_mode_dropdown,*ui_home_power_input,*ui_home_delay_input,*ui_home_afterrun_power_button,*ui_home_afterrun_power_input;
static lv_obj_t *ui_home_input_dropdown,*ui_home_output_dropdown,*ui_home_work_card,*ui_home_filter_card;
static lv_obj_t *ui_home_jbc_card,*ui_home_jbc,*ui_home_work,*ui_home_jbc_stripe,*ui_home_work_icon;
static lv_obj_t *ui_home_suction_card,*ui_home_suction,*ui_home_fan_detail,*ui_home_suction_title,*ui_home_suction_stripe;
static lv_obj_t *ui_home_weller_card,*ui_home_weller,*ui_home_filter,*ui_home_weller_stripe;
static lv_obj_t *ui_home_fault_card,*ui_home_fault,*ui_home_modules,*ui_home_fault_stripe;
enum {NUMERIC_FIELD_NONE,NUMERIC_FIELD_JBC_POWER,NUMERIC_FIELD_JBC_DELAY_WORK,NUMERIC_FIELD_JBC_DELAY_STAND,NUMERIC_FIELD_AFTER_POWER};
enum {MODULE_UNKNOWN,MODULE_JBC_BUS,MODULE_FAN_IO,MODULE_WELLER_ZERO_SMOG,MODULE_DISPLAY,MODULE_MODBUS_RTU,MODULE_UNIVERSAL_RS232,MODULE_JBC_USB,MODULE_FAN_IO_PRO};
enum {DISPLAY_EVENT_NONE,DISPLAY_EVENT_JBC_POWER_SET,DISPLAY_EVENT_JBC_DELAY_WORK_SET,DISPLAY_EVENT_JBC_DELAY_STAND_SET,DISPLAY_EVENT_AFTER_POWER_SET};
SETTINGS_EVENT(lv_home_continuous_event,24)
SETTINGS_EVENT(lv_home_fan_event,25)
SETTINGS_EVENT(lv_home_light_event,26)
SETTINGS_EVENT(lv_home_io1_event,27)
SETTINGS_EVENT(lv_home_io2_event,28)
SETTINGS_EVENT(lv_detail_jbc_mode_event,29)
SETTINGS_EVENT(lv_home_afterrun_power_event,30)
SETTINGS_EVENT(lv_home_input_select_event,31)
SETTINGS_EVENT(lv_home_output_select_event,32)
static lv_obj_t *ui_numeric_keyboard=nullptr,*ui_numeric_editor=nullptr,*ui_numeric_overlay=nullptr,*ui_numeric_source=nullptr;
static uint8_t ui_numeric_field=0,pending_display_event=0;
static int pending_display_event_value=0;
static void queue_display_event(uint8_t event,int value=0){pending_display_event=event;pending_display_event_value=value;}
static int extractor_power_min_percent(){return 10;}
#define constrain(value,low,high) ((value)<(low)?(low):((value)>(high)?(high):(value)))
#include "generic_numeric.inc.h"
static void lv_apply_dropdown_theme(lv_obj_t*,bool=false){}
static void lv_enable_home_module_card(lv_obj_t* obj,int type){lv_obj_set_user_data(obj,reinterpret_cast<void*>(uintptr_t(type)));}
static void lv_set_home_work_icon(bool,bool){}
SETTINGS_EVENT(lv_module_row_event,20)
SETTINGS_EVENT(lv_open_boot_connection_setup,21)
SETTINGS_EVENT(display_benchmark_back_event,22)
SETTINGS_EVENT(display_benchmark_start_event,23)
static void lv_stabilize_button(lv_obj_t*){}
static void lv_style_screen(lv_obj_t* screen){ofe_display_ui::flat(screen,ofe_display_ui::background(),0);lv_obj_set_style_pad_all(screen,0,0);}
#include "OfeDisplayHeaderUi.h"
#include "OfeSerialPortFont.h"
#include "OfeBluetoothFont.h"
#include "SproutIcon.h"
static bool module_eco_mode=true;
static unsigned active_alarm_count=255,ui_header_link_count=0;
static lv_obj_t *ui_header_link_cards[10],*ui_header_link_labels[10],*ui_header_ble_icons[10];
static lv_obj_t *ui_header_clock_labels[10],*ui_header_alarm_labels[10],*ui_header_eco_icons[10];
static void lv_set_visible(lv_obj_t*,bool);
static unsigned master_link_state_now(){return 1;}
static const char* master_link_text(unsigned){return display_wifi.wireless()?LV_SYMBOL_WIFI:OFE_SYMBOL_SERIAL_PORT;}
static lv_color_t master_link_text_color(unsigned){return ofe_display_ui::text();}
static void lv_update_master_link_ui(bool){}
static void lv_update_ble_indicator_ui(bool){}
static void lv_update_eco_indicator_ui(bool){}
static void lv_set_text_color_if_changed(lv_obj_t* obj,lv_color_t color,int part){lv_obj_set_style_text_color(obj,color,part);}
static void lv_update_alarm_header_ui();
#include "header_builders.inc.h"
static void lv_add_header(lv_obj_t* screen,const char* title){
  ui_header_link_count=0;
  if(screen_width==800)build_header_800(screen,title);else build_header_480(screen,title);
}
static lv_obj_t* lv_card(lv_obj_t* parent,int x,int y,int w,int h,lv_color_t color){
  auto* obj=lv_obj_create(parent);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);lv_obj_set_style_pad_all(obj,0,0);
  lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);ofe_display_ui::flat(obj,color,16);return obj;
}
static void lv_add_main_nav(lv_obj_t* screen,uint8_t active){
  const char* labels[]={tr("Home","Start"),tr("Modules","Module"),tr("Alarms","Alarme"),"System"};
  const lv_event_cb_t callbacks[]={lv_show_home_event,lv_show_modules_event,lv_show_alarms_event,lv_show_system_event};
  ofe_display_ui::navigation(screen,active,labels,callbacks);
}
#include "home_helpers.inc.h"
static unsigned module_total=0;
static constexpr unsigned FAST_FLAG_CONNECTED=1,CAP_RELAY_OUTPUT=32,CAP_PWM_OUTPUT=4,CAP_JBC_BUS=1;
static struct Summary {
  bool valid=false,live_io_valid=false;
  unsigned type=0,flags=0,caps=0,addr=0,live_jbc_flags=0,live_jbc_work=0,live_jbc_stand=0,live_io_ms=0,live_io_flags=0;
  char name[32]={};
} module_summaries[17];
using DisplayModuleSummary=Summary;
static bool module_summary_provides_extractor_output(const Summary& module){
  return module.valid&&(module.flags&1)&&((module.flags&0x10)?(module.flags&8):bool(module.caps&(CAP_RELAY_OUTPUT|CAP_PWM_OUTPUT)));
}
static bool module_summary_matches_group(const Summary& module,unsigned group){
  return module.valid&&(module.type==group||(group==MODULE_JBC_BUS&&module.type==MODULE_JBC_USB));
}
static struct HomeProfileCache {
  bool valid=false;
  uint8_t addr=0,type=0;
  uint8_t values_language=1;
  uint32_t last_ms=0;
  unsigned universal_entity_count=0,universal_entity_total=0;
  DisplayUniversalEntity universal_entities[32];
} bridge_caches[17];
using DisplayUniversalModuleCache=HomeProfileCache;
static HomeProfileCache* universal_cache_find(uint8_t addr,bool){
  for(auto& cache:bridge_caches)if(cache.valid&&cache.addr==addr)return &cache;
  return nullptr;
}
static bool detail_fields_is_universal(uint8_t type,uint32_t){return type==MODULE_MODBUS_RTU||type==MODULE_UNIVERSAL_RS232;}
constexpr uint32_t DISPLAY_UNIVERSAL_CACHE_REFRESH_MS=1200;
static bool display_values_supported=false;
#include "home_profile_refresh.inc.h"
static unsigned universal_output_power_percent_for_ui(uint8_t,unsigned){return 60;}
static String universal_entity_value_text(const DisplayUniversalEntity&);
#include "DisplayHomeMetrics.h"
static ofe_home_metrics::Cache home_metrics_fixture;
static bool home_metrics_get(uint8_t addr,uint8_t type,ofe_home_metrics::State& s,uint32_t& received){return home_metrics_fixture.get(addr,type,s,received);}
static int opened_home_module=-1;
static void lv_open_module_summary(uint8_t index){opened_home_module=index;}
static const char* on_off(bool value);
static String fault_name(uint16_t value,uint8_t){return value?"Output fault":"OK";}
static const char* display_module_type_name(uint8_t type){
  switch(type){case MODULE_FAN_IO:return "Fan/IO";case MODULE_FAN_IO_PRO:return "Fan/IO Pro";
  case MODULE_WELLER_ZERO_SMOG:return "Weller Zero Smog";case MODULE_UNIVERSAL_RS232:return "Universal RS232";
  case MODULE_MODBUS_RTU:return "Modbus RTU";default:return "JBC";}
}
static String fmt_dhm(uint32_t minutes){char s[64];snprintf(s,sizeof(s),"%ud %uh %um",minutes/1440,(minutes%1440)/60,minutes%60);return s;}
#include "OfeDisplayHomeUi.inc.h"
static void lv_set_detail_work_icon(bool visible,bool){lv_obj_set_flag(ui_detail_work_icon,LV_OBJ_FLAG_HIDDEN,!visible);}
static void lv_set_visible(lv_obj_t* obj,bool visible){lv_obj_set_flag(obj,LV_OBJ_FLAG_HIDDEN,!visible);}
static void lv_set_bg_color_if_changed(lv_obj_t* obj,lv_color_t color,int){lv_obj_set_style_bg_color(obj,color,0);}
static void lv_set_border_color_if_changed(lv_obj_t* obj,lv_color_t color,int){lv_obj_set_style_border_color(obj,color,0);}
static unsigned module_count_by_group(unsigned,bool){return 1;}
#include "other_pages.inc.h"
#include "controls_fixture.h"
static void remaining_pages(){
  using Build=void(*)();
  const Build small[]={build_modules_480,build_alarms_480,build_details_480,build_benchmark_480,build_boot_480,build_update_480,build_status_480};
  const Build large[]={build_modules_800,build_alarms_800,build_details_800,build_benchmark_800,build_boot_800,build_update_800,build_status_800};
  const char* names[]={"modules","alarms","details","benchmark","boot","update","status"};
  for(int page=0;page<7;++page){
    (screen_width==800?large:small)[page]();
    lv_obj_t* screen=page==0?ui_module_list_screen:page==1?ui_alarm_screen:page==2?ui_module_detail_screen:page==3?ui_benchmark_screen:page==4?ui_boot_screen:page==5?ui_update_screen:ui_status_screen;
    lv_screen_load(screen);ui_module_detail_screen=screen;
    if(page==0)for(int i=0;i<3;++i){lv_obj_remove_flag(ui_module_rows[i],LV_OBJ_FLAG_HIDDEN);lv_label_set_text(ui_module_row_names[i],i==0?"JBC FAE Bus":i==1?"Weller Zero Smog Bus":"Modbus RTU Bridge");lv_label_set_text(ui_module_row_meta[i],"0x10 | RS485 | online");}
    if(page==1){
      uint8_t index=0;
      lv_alarm_add(index,"Weller: Filter bald wechseln","0x30 | Hinweis",false);
      lv_alarm_add(index,"JBC: Station getrennt","0x10 | Kritisch",true);
      assert(lv_obj_get_style_border_width(ui_alarm_rows[0],LV_PART_MAIN)==1);
      assert(lv_obj_get_style_shadow_width(ui_alarm_rows[0],LV_PART_MAIN)==2);
      assert(lv_color_eq(lv_obj_get_style_border_color(ui_alarm_rows[1],LV_PART_MAIN),lv_color_hex(0xFF4D5E)));
    }
    if(page==2){
      lv_label_set_text(ui_detail_title,"Fan/IO Pro");
      lv_label_set_text(ui_detail_meta,"0x20 | RS485 | Fan/IO Pro Testbank");
      lv_label_set_text(ui_detail_status,"Online");
      lv_label_set_text(ui_detail_values,"Leistung: 55 %\nFilter-Restlaufzeit: 9d 23h 45m\nEingang 1: aus\nEingang 2: an");
      lv_detail_values_view("Work: An    Stand: Aus\nLeistung: 55 %\nFilter-Restlaufzeit: 9d 23h 45m\nEingang 1: aus\nEingang 2: an",true);
      lv_obj_update_layout(screen);
      lv_area_t back,body;
      lv_obj_get_coords(lv_obj_get_child(screen,1),&back);
      lv_obj_get_coords(lv_obj_get_parent(ui_detail_values),&body);
      assert(back.y2+8<=body.y1);
    }
    if(page==4){lv_label_set_text(ui_boot_fw,"Firmware 1.4.28RC");lv_label_set_text(ui_boot_addr,"0x40");}
    if(page==5){
      lv_label_set_text(ui_update_target,"0x40 Display ST7796 320x480 v1.0.29RC-Developer");
      lv_label_set_text(ui_update_status,"Firmware wird geschrieben");lv_label_set_text(ui_update_percent,"65%");
      lv_label_set_text(ui_update_phase,"Schreiben");lv_bar_set_value(ui_update_bar,65,LV_ANIM_OFF);
      lv_obj_update_layout(screen);lv_area_t target,status,bar;
      lv_obj_get_coords(ui_update_target,&target);lv_obj_get_coords(ui_update_status,&status);lv_obj_get_coords(ui_update_bar,&bar);
      assert(target.y2<status.y1&&status.y2<bar.y1);
    }
    if(page==6)lv_label_set_text(ui_status_msg,"Keine Verbindung zum Master");
    char name[80];snprintf(name,sizeof(name),"%s-%d-%s",names[page],screen_width,dark?"de-dark":"en-light");render(name);
    for(uint32_t i=0;i<lv_obj_get_child_count(screen);++i){auto* child=lv_obj_get_child(screen,i);if(!lv_obj_check_type(child,&lv_button_class))continue;lv_area_t a;lv_obj_get_coords(child,&a);assert(a.x1>=0&&a.x2<screen_width&&a.y1>=38&&a.y2<screen_height);}
    lv_obj_delete(screen);
  }
}
static void navigation_stress();
static void home_page(){
  using namespace ofe_home_metrics;
  module_total=11;home_metrics_fixture.clear();now_ms=100;
  for(auto& cache:bridge_caches)cache={};
  status.output_addr=0; // Every card must work without any selected main output.
  active_alarm_count=0;status.master_alarm_critical_mask=0;
  const unsigned types[]={MODULE_FAN_IO,MODULE_FAN_IO,MODULE_FAN_IO_PRO,MODULE_FAN_IO_PRO,
    MODULE_WELLER_ZERO_SMOG,MODULE_WELLER_ZERO_SMOG,MODULE_UNIVERSAL_RS232,MODULE_UNIVERSAL_RS232,
    MODULE_MODBUS_RTU,MODULE_MODBUS_RTU,MODULE_JBC_USB};
  const uint8_t addresses[]={0x20,0x21,0x22,0x23,0x30,0x31,0x50,0x51,0x60,0x61,0x12};
  for(unsigned i=0;i<module_total;++i){
    module_summaries[i]={};auto& m=module_summaries[i];m.valid=true;m.flags=0x19;m.type=types[i];
    m.addr=addresses[i];m.caps=CAP_PWM_OUTPUT;
    snprintf(m.name,sizeof(m.name),"%s %u",i<4?"Werkbank Absaugung":i<6?"Weller Loetplatz":"Community Werkstatt",i+1);
    if(i==10)continue;
    State s;s.addr=addresses[i];s.type=types[i];s.flags=ONLINE|HAS_OUTPUT|OUTPUT_VALID|OUTPUT_ON|HAS_POWER|HAS_RPM;
    s.power=100+i*70;s.rpm=1250+i*27;
    if(i==2||i==4||i==5){s.flags|=HAS_FILTER|HAS_TIME;s.filter=1;s.runtime=75;s.lifetime=14400;}
    if(i==2){s.flags|=HAS_PRESSURE;s.pressure=123;s.saturation=120;}
    if(i==5){s.flags|=LINK_KNOWN;s.filter=10;}
    if(i==7||i==9){s.flags=ONLINE|HAS_INPUT|INPUT_ON;m.flags=0x11;}
    home_metrics_fixture.put(s,now_ms);
  }
  for(unsigned round=0;round<3;++round)for(unsigned i=6;i<10;++i)
    assert(first_universal_module_addr_needing_cache()==addresses[i]);
  for(unsigned i=6;i<10;++i){
    auto& cache=bridge_caches[i];cache.valid=true;cache.addr=addresses[i];cache.type=types[i];cache.last_ms=now_ms;cache.values_language=display_language;
    cache.universal_entity_total=cache.universal_entity_count=32;
    for(unsigned e=0;e<32;++e){
      auto& entity=cache.universal_entities[e];entity={};entity.id=20+e;entity.flags=1;entity.type=DISPLAY_UNI_SENSOR;
      entity.value=i*100+e;snprintf(entity.label,sizeof(entity.label),"Profil %u Wert %u",i,e);
    }
    auto& duration=cache.universal_entities[0];duration.type=DISPLAY_UNI_TEXT;strcpy(duration.text,"6d 16h 0m");
    auto& boolean=cache.universal_entities[1];boolean.type=DISPLAY_UNI_BINARY_SENSOR;boolean.value=1;
    auto& action=cache.universal_entities[2];action.type=DISPLAY_UNI_BUTTON;action.flags=2;
  }
  assert(!first_universal_module_addr_needing_cache());
  // An empty profile is complete, not a permanent priority cache request.
  bridge_caches[6].universal_entity_total=bridge_caches[6].universal_entity_count=0;
  assert(!first_universal_module_addr_needing_cache());
  bridge_caches[6].universal_entity_total=bridge_caches[6].universal_entity_count=32;
  (screen_width==800?build_home_800:build_home_480)();
  lv_screen_load(ui_dashboard_screen);ui_module_detail_screen=ui_dashboard_screen;
  status.output_enabled=true;
  lv_label_set_text(ui_home_output,"Bereit");lv_label_set_text(ui_home_power,"55%");
  lv_label_set_text(ui_home_rpm,"1250 rpm");lv_label_set_text(ui_home_afterrun,"Nachlauf 12s");
  lv_label_set_text(ui_home_jbc,"Verbunden");lv_label_set_text(ui_home_work,"Station CLMU, DDE, JTSE");
  ofe_home_activity::set_counts({2,3,true});ofe_home_activity::set_fan(0,true,true,55);
  lv_label_set_text(ui_home_fault,"Keine Alarme");lv_label_set_text(ui_home_modules,"11 Module online");
  lv_update_bridge_home_cards(true);
  (screen_width==800?layout_home_800:layout_home_480)(true);lv_obj_update_layout(ui_dashboard_screen);
  assert(ofe_module_home::count==10);
  assert(lv_obj_get_width(ui_home_jbc_stripe)==3&&lv_obj_get_x(ui_home_jbc_stripe)==4&&lv_obj_get_y(ui_home_jbc_stripe)==12);
  assert(lv_obj_get_height(ui_home_jbc_stripe)==lv_obj_get_height(ui_home_jbc_card)-24);
  char name[80];snprintf(name,sizeof(name),"home-%d-%s",screen_width,dark?"de-dark":"en-light");render(name);
  lv_area_t button,bar,output,settings;lv_obj_get_coords(ui_home_continuous_button,&button);
  lv_obj_get_coords(ui_home_power_bar,&bar);lv_obj_get_coords(ui_home_output_card,&output);lv_obj_get_coords(ui_home_settings_card,&settings);
  assert(button.y2<bar.y1&&bar.y2<output.y2&&output.y2<settings.y1);
  assert(lv_obj_get_height(ui_home_continuous_button)==44);
  for(auto* field:{ui_home_power_input,ui_home_delay_input,ui_home_afterrun_power_input}){
    assert(lv_obj_get_style_text_align(field,LV_PART_MAIN)==LV_TEXT_ALIGN_CENTER);
    assert(lv_obj_get_style_pad_top(field,LV_PART_MAIN)==lv_obj_get_style_pad_bottom(field,LV_PART_MAIN));
  }
  assert(lv_obj_has_flag(ui_home_suction_card,LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(ui_home_weller_card,LV_OBJ_FLAG_HIDDEN));
  std::vector<lv_obj_t*> cards={ui_home_jbc_card};
  int fan_slot=-1,filtered_slot=-1,disabled_slot=-1;
  for(unsigned i=0;i<ofe_module_home::count;++i){
    auto& card=ofe_module_home::cards[i];cards.push_back(card.root);
    assert(lv_obj_get_width(card.stripe)==3&&lv_obj_get_x(card.stripe)==4&&lv_obj_get_y(card.stripe)==12);
    assert(lv_obj_get_height(card.stripe)==lv_obj_get_height(card.root)-24);
    assert(lv_obj_get_style_radius(card.stripe,LV_PART_MAIN)==0);
    auto& fan=ofe_home_activity::fans[5+i];
    if(card.addr==0x20)fan_slot=i;
    if(card.addr==0x22)filtered_slot=i;
    if(card.addr==0x23)disabled_slot=i;
    assert(!strstr(lv_label_get_text(card.detail),"IN1")&&!strstr(lv_label_get_text(card.detail),"OUT1"));
    assert(lv_obj_get_style_y(card.identity,LV_PART_MAIN)>=lv_obj_get_style_y(card.title,LV_PART_MAIN)+lv_obj_get_height(card.title));
    for(auto* label:{card.title,card.identity,card.power_label,card.power,card.rpm_label,card.rpm,card.detail}){
      if(lv_obj_has_flag(label,LV_OBJ_FLAG_HIDDEN))continue;
      lv_area_t child,parent;lv_obj_get_coords(label,&child);lv_obj_get_coords(card.root,&parent);
      assert(child.x1>=parent.x1&&child.x2<=parent.x2&&child.y1>=parent.y1&&child.y2<parent.y2);
    }
    unsigned index=0;while(index<module_total&&module_summaries[index].addr!=card.addr)++index;assert(index<module_total);
    opened_home_module=-1;lv_obj_send_event(card.root,LV_EVENT_CLICKED,nullptr);assert(opened_home_module==int(index));
    if(index==7||index==9){assert(lv_obj_has_flag(card.power,LV_OBJ_FLAG_HIDDEN));assert(!fan.running);}
    else if(index==5){assert(!strcmp(lv_label_get_text(card.power),"--"));assert(strstr(lv_label_get_text(card.detail),german?"Filterwerte":"filter values"));}
    else {char expected[24];snprintf(expected,sizeof(expected),"%u %%",unsigned((100+index*70)/10));assert(!strcmp(lv_label_get_text(card.power),expected));}
    if(index>=6&&index<10){
      const char* detail=lv_label_get_text(card.detail);
      for(unsigned e=0;e<32;++e){char label[64];snprintf(label,sizeof(label),"Profil %u Wert %u:",index,e);assert(strstr(detail,label));}
      char last[80];snprintf(last,sizeof(last),"Profil %u Wert 31: %u",index,index*100+31);assert(strstr(detail,last));
      assert(strstr(detail,"6d 16h 0m")&&strstr(detail,german?"Steuerung":"Control"));
      assert(!strstr(detail,german?"Keine Hauptrolle":"No main role"));
    }
  }
  assert(fan_slot>=0&&filtered_slot>=0&&disabled_slot>=0);
  assert(strstr(lv_label_get_text(ofe_module_home::cards[filtered_slot].detail),"ADC"));
  assert(!strstr(lv_label_get_text(ofe_module_home::cards[disabled_slot].detail),"Filter"));
  const std::string other_profile=lv_label_get_text(ofe_module_home::cards[7].detail);
  bridge_caches[6].universal_entities[31].value=987;lv_update_bridge_home_cards(true);
  assert(strstr(lv_label_get_text(ofe_module_home::cards[6].detail),"Wert 31: 987"));
  assert(other_profile==lv_label_get_text(ofe_module_home::cards[7].detail));
  bridge_caches[6].universal_entities[31].valid=false;lv_update_bridge_home_cards(true);
  assert(!strstr(lv_label_get_text(ofe_module_home::cards[6].detail),"Wert 31:"));
  bridge_caches[6].universal_entities[31].valid=true;
  cards.push_back(ui_home_fault_card);
  for(unsigned i=0;i<cards.size();++i)for(unsigned j=0;j<i;++j){
    lv_area_t a,b;lv_obj_get_coords(cards[i],&a);lv_obj_get_coords(cards[j],&b);
    assert(a.x1>b.x2||b.x1>a.x2||a.y1>b.y2||b.y1>a.y2);
  }
  if(screen_width==800){
    lv_area_t before,after;lv_obj_get_coords(ui_home_settings_card,&before);
    lv_obj_scroll_to_y(ui_home_module_content,260,LV_ANIM_OFF);lv_obj_update_layout(ui_dashboard_screen);
    lv_obj_get_coords(ui_home_settings_card,&after);assert(!memcmp(&before,&after,sizeof(before)));
    assert(lv_obj_get_scroll_y(ui_home_module_content)>200&&lv_obj_get_scroll_y(ui_home_content)==0);
  } else lv_obj_scroll_to_y(ui_home_content,lv_obj_get_y(ofe_module_home::cards[0].root),LV_ANIM_OFF);
  render((std::string(name)+"-modules-scrolled").c_str());
  lv_obj_scroll_to_y(ui_home_module_content,lv_obj_get_y(ofe_module_home::cards[filtered_slot].root),LV_ANIM_OFF);
  render((std::string(name)+"-filter").c_str());
  lv_obj_scroll_to_y(ui_home_module_content,lv_obj_get_y(ofe_module_home::cards[fan_slot].root),LV_ANIM_OFF);
  lv_obj_update_layout(ui_dashboard_screen);lv_update_bridge_home_cards(true);
  auto& fan=ofe_home_activity::fans[5+fan_slot];
  lv_tick_inc(500);lv_timer_handler();const int phase=lv_image_get_rotation(fan.image);
  assert(phase>0);ofe_home_activity::set_fan(5+fan_slot,true,true,80,ofe_home_activity::accent(1));
  assert(lv_image_get_rotation(fan.image)==phase);
  lv_point_t pivot;lv_image_get_pivot(fan.image,&pivot);assert(pivot.x==10&&pivot.y==10);
  assert(lv_color_eq(lv_obj_get_style_image_recolor(fan.image,LV_PART_MAIN),lv_color_hex(ofe_home_activity::accent(1))));
  now_ms+=11000;lv_update_bridge_home_cards(true);
  assert(!strcmp(lv_label_get_text(ofe_module_home::cards[fan_slot].power),"--"));
  assert(strstr(lv_label_get_text(ofe_module_home::cards[filtered_slot].detail),german?"Filterwerte":"filter values"));
  module_summaries[0].valid=false;lv_update_bridge_home_cards(true);
  assert(ofe_module_home::count==9&&lv_obj_has_flag(ofe_module_home::cards[9].root,LV_OBJ_FLAG_HIDDEN));
  for(unsigned i=0;i<ofe_module_home::count;++i)assert(ofe_module_home::cards[i].addr!=0x20);
  navigation_stress();
  auto* other=lv_obj_create(nullptr);lv_screen_load(other);for(auto& icon:ofe_home_activity::fans)assert(!icon.running);
  lv_screen_load(ui_dashboard_screen);
  auto* saver=lv_obj_create(nullptr);lv_style_screen(saver);
  auto* power=lv_label(saver,"Leistung 55%",42,196,ofe_display_ui::text(),UI_FONT_DEFAULT,160);
  ofe_home_activity::create_saver(saver,power);ofe_home_activity::update_saver(true,true);
  lv_screen_load(saver);assert(ofe_home_activity::saver_fan.running);
  ofe_home_activity::update_saver(true,false);assert(!ofe_home_activity::saver_fan.running);
  ofe_home_activity::update_saver(true,true);assert(ofe_home_activity::saver_fan.running);
  lv_obj_set_pos(power,50,200);ofe_home_activity::move_saver(power);lv_obj_update_layout(saver);
  assert(lv_obj_get_x(ofe_home_activity::saver_fan.image)==22&&lv_obj_get_y(ofe_home_activity::saver_fan.image)==197);
  lv_screen_load(ui_dashboard_screen);assert(!ofe_home_activity::saver_fan.running);
  ofe_home_activity::saver_fan={};lv_obj_delete(saver);lv_obj_delete(other);
  for(unsigned i=0;i<22;++i)ofe_home_activity::set_fan(i,false,false);
  for(auto& icon:ofe_home_activity::fans)assert(!icon.running);
  lv_obj_delete(ui_dashboard_screen);module_total=0;for(auto& cache:bridge_caches)cache={};
}
static void detail_examples(){
  (screen_width==800?build_details_800:build_details_480)();
  lv_screen_load(ui_module_detail_screen);
  const char* names[]={"JBC FAE Bus","Fan/IO Pro","Weller Zero Smog","Display 800x480","Master","Universal / Modbus"};
  const char* values[]={
    "Station: DDE    Adresse: 0x12\nWork: An    Stand: Aus\nLeistung: 55 %\nDevice ID: 849158E467A340159646170D6B1595EF",
    "Hauptausgang: Luefter Absaugung\nLeistung: 55 %\nEingaenge:\n Taster Absaugstufe: Aus\n Taster Continuous: An\nAusgaenge:\n LED Absaugstufe: Ein\nFilter-Restlaufzeit: 9d 23h 45m",
    "Verbindung: Aus   Luefter: Aus   Licht: An\nDrehzahl: 33 %   RPM: 0\nFilter: Sehr gut\nFilterlaufzeit: 1d 16h 30m / 6d 16h 0m",
    "Ansicht: System\nHelligkeit: 55 %\nFirmware: 1.4.29RC\nVerbindung: WLAN",
    "IP: 192.168.20.12\nLaufzeit: 10d 14h 20m\nCPU: 30 %   Heap: 120 KB",
    "Programmierte Filterlaufzeit: 6d 16h 0m\nFilterzustand: Filter bald wechseln\nDrehzahl: 2300 rpm\nArbeitslicht: An"
  };
  for(unsigned i=0;i<6;++i){
    lv_label_set_text(ui_detail_title,names[i]);
    lv_detail_values_view(values[i],i==0);
    char name[80];snprintf(name,sizeof(name),"details-%u-%d-%s",i,screen_width,dark?"de-dark":"en-light");render(name);
    auto* first=lv_obj_get_child(ui_detail_value_grid,0);
    lv_detail_values_view(values[i],i==0);assert(first==lv_obj_get_child(ui_detail_value_grid,0));
    assert(lv_detail_values_bottom()>72);
  }
  String many;
  for(unsigned i=0;i<32;++i)many+=std::string("Entity ")+std::to_string(i)+": 6d 16h 0m\n";
  lv_detail_values_view(many,false);assert(lv_obj_get_child_count(ui_detail_value_grid)==32);
  lv_detail_values_view("Einziger Wert: 123",false);
  for(unsigned i=1;i<32;++i)assert(lv_obj_has_flag(lv_obj_get_child(ui_detail_value_grid,i),LV_OBJ_FLAG_HIDDEN));
  lv_obj_delete(ui_module_detail_screen);lv_detail_values_reset();
}
static void assert_control_bounds(lv_obj_t* parent){
  lv_obj_update_layout(parent);
  lv_area_t box;lv_obj_get_coords(parent,&box);
  for(unsigned i=0;i<lv_obj_get_child_count(parent);++i){
    auto* obj=lv_obj_get_child(parent,i);if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))continue;
    lv_area_t a;lv_obj_get_coords(obj,&a);
    if(!(a.x1>=box.x1&&a.x2<=box.x2&&a.y1>=box.y1&&a.y2<=box.y2))
      fprintf(stderr,"control outside parent: %d,%d..%d,%d vs %d,%d..%d,%d\n",a.x1,a.y1,a.x2,a.y2,box.x1,box.y1,box.x2,box.y2);
    assert(a.x1>=box.x1&&a.x2<=box.x2&&a.y1>=box.y1&&a.y2<=box.y2);
    if(lv_obj_check_type(obj,&lv_textarea_class)){
      assert(lv_obj_get_style_text_align(obj,LV_PART_MAIN)==LV_TEXT_ALIGN_LEFT);
      assert(lv_obj_get_style_pad_top(obj,LV_PART_MAIN)==lv_obj_get_style_pad_bottom(obj,LV_PART_MAIN));
    }
    else if(lv_obj_check_type(obj,&lv_obj_class))assert_control_bounds(obj);
  }
}
static void control_examples(){
  const unsigned types[]={MODULE_JBC_BUS,MODULE_FAN_IO,MODULE_WELLER_ZERO_SMOG,MODULE_DISPLAY,MODULE_MODBUS_RTU};
  for(unsigned type:types){
    (screen_width==800?build_details_800:build_details_480)();lv_screen_load(ui_module_detail_screen);
    selected_module.type=uint8_t(type);selected_module.addr=type==MODULE_DISPLAY?module_addr:0x20;
    selected_module.valid=true;selected_module.universal_entity_count=type==MODULE_FAN_IO?8:type==MODULE_MODBUS_RTU?32:0;
    for(unsigned i=0;i<selected_module.universal_entity_count;++i){
      const uint8_t controls[]={DISPLAY_UNI_NUMBER,DISPLAY_UNI_SWITCH,DISPLAY_UNI_SELECT,DISPLAY_UNI_BUTTON};
      auto& e=selected_module.universal_entities[i];e={};e.id=uint8_t(type==MODULE_FAN_IO?40+i:20+i);e.type=controls[i%4];e.value=55;
      snprintf(e.label,sizeof(e.label),"%s %u",i%4==2?"Programmierte Filterlaufzeit Werkbank":"Absaugung Werkbank",i+1);
      snprintf(e.options,sizeof(e.options),"Aus|1 Tag|6 Tage 16 Stunden");
    }
    detail_controls_addr=0;detail_controls_type=MODULE_UNKNOWN;detail_controls_caps=0;detail_controls_signature=0;
    lv_detail_values_view("Status: Online\nLeistung: 55 %",false);
    lv_build_detail_controls();
    assert_control_bounds(ui_detail_controls);
    if(type==MODULE_MODBUS_RTU){
      for(unsigned i=0;i<32;++i){
        auto* obj=ui_detail_universal_sliders[i]?ui_detail_universal_sliders[i]:ui_detail_universal_switches[i]?ui_detail_universal_switches[i]:
          ui_detail_universal_selects[i]?ui_detail_universal_selects[i]:ui_detail_universal_buttons[i];
        assert(obj&&uintptr_t(lv_obj_get_user_data(obj))==20+i);
      }
    }
    if(type==MODULE_JBC_BUS){
      pending_display_event=DISPLAY_EVENT_NONE;
      lv_obj_send_event(ui_detail_jbc_power_input,LV_EVENT_CLICKED,nullptr);assert(ui_numeric_overlay);
      assert(lv_obj_get_style_text_align(ui_numeric_editor,LV_PART_MAIN)==LV_TEXT_ALIGN_CENTER);
      char image[80];snprintf(image,sizeof(image),"generic-keypad-%d-%s",screen_width,dark?"de-dark":"en-light");render(image);
      lv_textarea_set_text(ui_numeric_editor,"9999");lv_obj_send_event(ui_numeric_keyboard,LV_EVENT_READY,nullptr);
      assert(!ui_numeric_overlay&&pending_display_event==DISPLAY_EVENT_JBC_POWER_SET&&pending_display_event_value==100);
      lv_obj_send_event(ui_detail_jbc_power_input,LV_EVENT_CLICKED,nullptr);
      lv_obj_send_event(ui_numeric_keyboard,LV_EVENT_CANCEL,nullptr);assert(!ui_numeric_overlay);
    }
    lv_obj_scroll_to_y(lv_obj_get_parent(ui_detail_controls),lv_obj_get_y(ui_detail_controls)-8,LV_ANIM_OFF);
    char image[80];snprintf(image,sizeof(image),"controls-%u-%d-%s",type,screen_width,dark?"de-dark":"en-light");render(image);
    lv_obj_delete(ui_module_detail_screen);lv_detail_values_reset();
  }
}
static void navigation_stress(){
  // The device keeps its screens alive. Test that combined footprint, not each
  // page in isolation with the former unconstrained 4 MiB test-only heap.
  std::vector<lv_obj_t*> screens;
  (screen_width==800?build_modules_800:build_modules_480)();screens.push_back(ui_module_list_screen);
  (screen_width==800?build_alarms_800:build_alarms_480)();screens.push_back(ui_alarm_screen);
  (screen_width==800?build_benchmark_800:build_benchmark_480)();screens.push_back(ui_benchmark_screen);
  (screen_width==800?build_boot_800:build_boot_480)();screens.push_back(ui_boot_screen);
  (screen_width==800?build_update_800:build_update_480)();screens.push_back(ui_update_screen);
  (screen_width==800?build_status_800:build_status_480)();screens.push_back(ui_status_screen);
  (screen_width==800?build_details_800:build_details_480)();screens.push_back(ui_module_detail_screen);
  auto* detail=ui_module_detail_screen;
  size_t baseline=0,peak=0;
  for(unsigned cycle=0;cycle<40;++cycle){
    selected_module.valid=true;selected_module.type=MODULE_MODBUS_RTU;selected_module.addr=0x60+(cycle%2);
    selected_module.universal_entity_count=32;
    for(unsigned i=0;i<32;++i){
      const uint8_t kinds[]={DISPLAY_UNI_NUMBER,DISPLAY_UNI_SWITCH,DISPLAY_UNI_SELECT,DISPLAY_UNI_BUTTON};
      auto& e=selected_module.universal_entities[i];e={};e.id=20+i;e.type=kinds[i%4];e.value=i;
      snprintf(e.label,sizeof(e.label),"Werkstatt Eingang Ausgang %u",i);strcpy(e.options,"Aus|1 Tag|6 Tage 16 Stunden");
    }
    lv_screen_load(detail);ui_module_detail_screen=detail;detail_controls_addr=0;
    String values;
    for(unsigned i=0;i<32;++i)values+=String("Profilwert ")+String(i)+": 6d 16h 0m\n";
    lv_detail_values_view(values,false);lv_build_detail_controls();lv_obj_update_layout(detail);
    lv_tick_inc(100);lv_timer_handler();
    status.output_addr=cycle%2?0x22:0x30;
    lv_screen_load(ui_dashboard_screen);lv_update_bridge_home_cards(true);
    (screen_width==800?layout_home_800:layout_home_480)(true);
    lv_tick_inc(100);lv_timer_handler();
    lv_mem_monitor_t mem;lv_mem_monitor(&mem);assert(lv_mem_test()==LV_RESULT_OK);
    const size_t used=mem.total_size-mem.free_size;if(used>peak)peak=used;
    if(cycle==8)baseline=used;
    if(cycle>8)assert(used<=baseline+2048);
  }
  printf("PASS: 40 output/detail navigation cycles, retained screens, 32 entities; %d px UI peak %zu/%zu bytes\n",screen_width,peak,size_t(12*32*1024));
  for(auto* screen:screens)lv_obj_delete(screen);
  lv_detail_values_reset();ui_module_detail_screen=ui_dashboard_screen;status.output_addr=0;
}
static void screensaver_examples(){
  (screen_width==800?build_screensaver_800:build_screensaver_480)();
  lv_screen_load(ui_screensaver_screen);ui_module_detail_screen=ui_screensaver_screen;
  lv_label_set_text(ui_screensaver_clock,"23:59\n31.12.2026");
  lv_label_set_text(ui_screensaver_state,"Absaugung aktiv");
  lv_label_set_text(ui_screensaver_power,"Leistung 55%");
  lv_label_set_text(ui_screensaver_modules,"7 Module");
  ofe_home_activity::update_saver(true,true);
  char name[80];snprintf(name,sizeof(name),"screensaver-%d-%s",screen_width,dark?"de-dark":"en-light");
  lv_tick_inc(100);lv_timer_handler();render(name);
  auto before=pixels;const int angle=lv_image_get_rotation(ofe_home_activity::saver_fan.image);
  lv_tick_inc(400);lv_timer_handler();render((std::string(name)+"-rotated").c_str());
  assert(angle!=lv_image_get_rotation(ofe_home_activity::saver_fan.image));
  unsigned changed=0;for(unsigned i=0;i<pixels.size();++i)changed+=pixels[i]!=before[i];assert(changed>10);
  lv_area_t icon,power;lv_obj_get_coords(ofe_home_activity::saver_fan.image,&icon);lv_obj_get_coords(ui_screensaver_power,&power);
  assert(icon.x1>=0&&icon.x2<power.x1&&icon.y1>=0&&icon.y2<screen_height);
  ofe_home_activity::update_saver(true,false);assert(!ofe_home_activity::saver_fan.running);
  ofe_home_activity::update_saver(false,true);assert(!ofe_home_activity::saver_fan.running);
  ofe_home_activity::saver_fan={};lv_obj_delete(ui_screensaver_screen);
}
static void fan_center_tests(){
  auto* screen=lv_obj_create(nullptr);lv_style_screen(screen);lv_obj_set_style_bg_color(screen,lv_color_hex(0),0);
  lv_screen_load(screen);
  auto* image=lv_image_create(screen);ofe_home_activity::prepare_fan(image);lv_obj_set_pos(image,40,40);
  lv_obj_set_style_image_recolor(image,lv_color_hex(0xFFFFFF),0);
  double min_x=999,max_x=0,min_y=999,max_y=0;
  for(int angle=0;angle<3600;angle+=450){
    lv_image_set_rotation(image,angle);lv_refr_now(nullptr);lv_obj_update_layout(screen);
    double sum=0,sx=0,sy=0;
    for(int y=32;y<70;++y)for(int x=32;x<70;++x){
      const double weight=pixels[y*screen_width+x]&31;sum+=weight;sx+=weight*x;sy+=weight*y;
    }
    assert(sum>0);const double x=sx/sum,y=sy/sum;
    min_x=std::min(min_x,x);max_x=std::max(max_x,x);min_y=std::min(min_y,y);max_y=std::max(max_y,y);
    // The supplied SVG has a cutout hub; measure its ink centroid instead.
  }
  if(max_x-min_x>0.5||max_y-min_y>0.5)fprintf(stderr,"fan center span %.3f, %.3f\n",max_x-min_x,max_y-min_y);
  assert(max_x-min_x<=0.5&&max_y-min_y<=0.5);
  lv_obj_delete(screen);
}
static void activity_state_tests(){
  module_total=2;
  for(unsigned i=0;i<2;++i){
    auto& module=module_summaries[i];module={};module.valid=true;module.flags=1;module.type=MODULE_JBC_USB;
    module.live_io_valid=true;module.live_io_ms=now_ms;module.live_jbc_flags=FAST_FLAG_CONNECTED;
    module.live_jbc_work=1;module.live_jbc_stand=6;
  }
  auto counts=lv_home_jbc_counts(true);assert(counts.known&&counts.work==2&&counts.stand==4);
  module_summaries[1].live_jbc_flags=0;counts=lv_home_jbc_counts(true);assert(counts.known&&counts.work==1&&counts.stand==2);
  module_summaries[1].live_io_valid=false;assert(!lv_home_jbc_counts(true).known);
  assert(!lv_home_jbc_counts(false).known);
  now_ms=5;module_summaries[0].live_io_ms=0xFFFFFFF0u;module_summaries[1].flags=0;
  counts=lv_home_jbc_counts(true);assert(counts.known&&counts.work==1);
  module_summaries[0].type=MODULE_FAN_IO;module_summaries[0].caps=CAP_PWM_OUTPUT;
  module_summaries[0].addr=0x21;module_summaries[0].live_io_flags=6;
  assert(lv_home_fan_state(true).capable&&lv_home_fan_state(true).active);
  assert(!lv_home_fan_state(false).active);
  now_ms=20000;assert(!lv_home_fan_state(true).active);
  now_ms=100;
  for(unsigned i=0;i<2;++i){
    auto& module=module_summaries[i];module={};module.valid=true;module.flags=1;module.type=MODULE_JBC_BUS;
    module.live_io_valid=true;module.live_io_ms=now_ms;module.live_jbc_flags=FAST_FLAG_CONNECTED;
  }
  counts=lv_home_jbc_counts(true);assert(counts.known&&counts.work==0&&counts.stand==2);
  module_summaries[0].live_jbc_work=1;
  counts=lv_home_jbc_counts(true);assert(counts.known&&counts.work==1&&counts.stand==1);
  module_summaries[0].live_jbc_flags=0;module_summaries[1].live_jbc_flags=0;
  counts=lv_home_jbc_counts(true);assert(counts.known&&counts.work==0&&counts.stand==0);
  module_total=0;now_ms=100;
  auto* icon=lv_image_create(lv_screen_active());
  for(const char* text:{"on","an","On","An","ON","AN"}){
    ofe_jbc_activity_ui::update(icon,ofe_jbc_activity_ui::STAND,text);
    assert(lv_color_eq(lv_obj_get_style_image_recolor(icon,LV_PART_MAIN),lv_color_hex(0xFFB020)));
  }
  bool aa=false;for(auto value:parking_icon_map)aa|=value>0&&value<255;assert(aa);
  ofe_jbc_activity_ui::update(icon,ofe_jbc_activity_ui::STAND,"aus");
  assert(lv_color_eq(lv_obj_get_style_image_recolor(icon,LV_PART_MAIN),ofe_display_ui::muted()));
  lv_obj_delete(icon);
}
static void header_examples(){
  active_alarm_count=255;
  for(bool wireless:{true,false}){
    display_wifi.data.active=wireless;
    ui_module_detail_screen=lv_obj_create(nullptr);lv_style_screen(ui_module_detail_screen);lv_screen_load(ui_module_detail_screen);
    lv_add_header(ui_module_detail_screen,"module details");lv_obj_update_layout(ui_module_detail_screen);
    lv_area_t eco;lv_obj_get_coords(ui_header_eco_icons[0],&eco);
    const double center=(eco.y1+eco.y2)/2.0;
    for(auto item:{std::make_pair(ui_header_ble_icons[0],0xE001u),
                   std::make_pair(ui_header_alarm_labels[0],0xF0F3u),
                   std::make_pair(ui_header_link_labels[0],wireless?0xF1EBu:0xE000u)}){
      const lv_font_t* font=lv_obj_get_style_text_font(item.first,LV_PART_MAIN);
      lv_font_glyph_dsc_t glyph={};assert(lv_font_get_glyph_dsc(font,&glyph,item.second,0));
      lv_area_t bounds;lv_obj_get_coords(item.first,&bounds);
      const double ink=bounds.y1+lv_obj_get_style_pad_top(item.first,LV_PART_MAIN)+
        font->line_height-font->base_line-glyph.box_h-glyph.ofs_y+(glyph.box_h-1)/2.0;
      assert(std::abs(ink-center)<=1.0);
      assert(bounds.y1>=0&&bounds.y2<42&&bounds.x2<screen_width);
    }
    auto* clock=ui_header_clock_labels[0];
    for(const char* text:{"--:--","23:59","23:59\n31.12.2026"}){
      lv_label_set_text(clock,text);ofe_display_header::center_clock(clock);lv_obj_update_layout(clock);
      const int lines=strchr(text,'\n')?2:1;
      const int height=UI_FONT_DEFAULT->line_height*lines+(lines-1)*lv_obj_get_style_text_line_space(clock,LV_PART_MAIN);
      assert(std::abs(2*lv_obj_get_style_pad_top(clock,LV_PART_MAIN)+height-34)<=1);
    }
    render((std::string("header-")+std::to_string(screen_width)+(wireless?"-wifi":"-serial")+(dark?"-dark":"-light")).c_str());
    active_alarm_count=0;status.master_alarm_critical_mask=0;lv_update_alarm_header_ui();lv_obj_update_layout(ui_module_detail_screen);
    auto* alarm=ui_header_alarm_labels[0];
    assert(!strcmp(lv_label_get_text(alarm),LV_SYMBOL_OK)&&!lv_obj_has_flag(alarm,LV_OBJ_FLAG_HIDDEN));
    assert(lv_color_eq(lv_obj_get_style_text_color(alarm,LV_PART_MAIN),lv_color_hex(0x55D98A)));
    render((std::string("header-ok-")+std::to_string(screen_width)+(wireless?"-wifi":"-serial")+(dark?"-dark":"-light")).c_str());
    active_alarm_count=2;lv_update_alarm_header_ui();
    assert(strstr(lv_label_get_text(alarm),"2")&&strstr(lv_label_get_text(alarm),LV_SYMBOL_BELL));
    assert(lv_color_eq(lv_obj_get_style_text_color(alarm,LV_PART_MAIN),lv_color_hex(0xFFB020)));
    status.master_alarm_critical_mask=1;lv_update_alarm_header_ui();
    assert(lv_color_eq(lv_obj_get_style_text_color(alarm,LV_PART_MAIN),lv_color_hex(0xFF4D5E)));
    active_alarm_count=255;status.master_alarm_critical_mask=0;
    lv_obj_delete(ui_module_detail_screen);
  }
  display_wifi.data.active=true;
}
int main(){
  lv_init();
  alignas(64) static uint8_t widget_pools[11][LV_MEM_SIZE];
  for(auto& pool:widget_pools)assert(lv_mem_add_pool(pool,sizeof(pool)));
  for(int width:{480,800}){
    screen_width=width;screen_height=width==480?320:480;pixels.resize(width*screen_height);draw.resize(pixels.size());
    auto* d=lv_display_create(width,screen_height);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,draw.data(),nullptr,draw.size()*2,LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
    activity_state_tests();
    fan_center_tests();
    for(bool theme:{true,false}){dark=theme;german=theme;selected_module.addr=0x12;jbc_detail_reset_ui();lv_detail_values_reset();ui_module_detail_screen=lv_obj_create(nullptr);lv_screen_load(ui_module_detail_screen);
      lv_obj_set_style_bg_color(ui_module_detail_screen,ui_theme_color(0x0E1620,0xF7FAFC),0);
      lv_label(ui_module_detail_screen,"JBC USB",8,10,ui_theme_color(0xFFFFFF,0x17212B),UI_FONT_18,200);
      auto* panel=lv_obj_create(ui_module_detail_screen);ui_detail_values=lv_label_create(panel);ui_detail_controls=lv_obj_create(panel);ui_detail_title=lv_label_create(ui_module_detail_screen);
      // Prepare the loading view while a different screen is active.
      auto* previous=lv_obj_create(nullptr);lv_screen_load(previous);
      state=ofe_jbc_detail::State();lv_update_jbc_detail(true);
      assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(ui_detail_title,LV_OBJ_FLAG_HIDDEN));
      assert(jbc_detail_root&&!lv_obj_has_flag(jbc_detail_root,LV_OBJ_FLAG_HIDDEN));
      lv_screen_load(ui_module_detail_screen);lv_obj_delete(previous);
      for(auto section:{ofe_jbc_detail::STATION,ofe_jbc_detail::COUNTERS,ofe_jbc_detail::SETTINGS}){
        fill(section);char name[80];snprintf(name,sizeof(name),"jbc-%d-%s-%d",width,dark?"de-dark":"en-light",section);render(name);
        assert(strstr(lv_label_get_text(jbc_detail_identity),"0x12"));
        assert(strstr(lv_label_get_text(jbc_detail_identity),"1.1.83RC"));
        lv_area_t identity,list;lv_obj_get_coords(jbc_detail_identity,&identity);lv_obj_get_coords(jbc_detail_list,&list);
        assert(identity.y2<list.y1&&lv_obj_get_y(jbc_detail_list)==76);
        int footer_y=lv_obj_get_y(jbc_detail_next);lv_obj_scroll_to_y(jbc_detail_list,160,LV_ANIM_OFF);lv_obj_update_layout(ui_module_detail_screen);assert(lv_obj_get_y(jbc_detail_next)==footer_y);lv_obj_scroll_to_y(jbc_detail_list,0,LV_ANIM_OFF);
        if(section==ofe_jbc_detail::SETTINGS){
          lv_obj_send_event(jbc_detail_plus[0],LV_EVENT_CLICKED,nullptr);assert(state.pending);state.pending=false;lv_update_module_detail();
          assert(lv_obj_get_style_text_align(jbc_detail_spin[0],LV_PART_MAIN)==LV_TEXT_ALIGN_LEFT);
          lv_obj_send_event(jbc_detail_spin[0],LV_EVENT_CLICKED,nullptr);assert(jbc_detail_editor);render((std::string(name)+"-keypad").c_str());
          assert(lv_obj_get_style_pad_top(jbc_detail_spin[0],LV_PART_MAIN)==lv_obj_get_style_pad_bottom(jbc_detail_spin[0],LV_PART_MAIN));
          assert(lv_obj_get_style_text_align(jbc_detail_input,LV_PART_MAIN)==LV_TEXT_ALIGN_CENTER);
          assert(lv_obj_get_style_pad_top(jbc_detail_input,LV_PART_MAIN)==lv_obj_get_style_pad_bottom(jbc_detail_input,LV_PART_MAIN));
          auto* keyboard=check_keypad();lv_textarea_set_text(jbc_detail_input,"9999");
          lv_obj_send_event(keyboard,LV_EVENT_READY,nullptr);assert(jbc_detail_editor&&!state.pending&&lv_label_get_text(jbc_detail_error)[0]);
          render((std::string(name)+"-keypad-error").c_str());check_keypad();
          lv_obj_send_event(keyboard,LV_EVENT_CANCEL,nullptr);assert(!jbc_detail_editor&&!state.pending);
          ofe_jbc_detail::copy(state.rows[0].label,sizeof(state.rows[0].label),"Port 1 Temperaturlevel Mindesttemperatur einstellen");
          lv_update_module_detail();lv_obj_send_event(jbc_detail_spin[0],LV_EVENT_CLICKED,nullptr);assert(jbc_detail_editor);
          keyboard=check_keypad();render((std::string(name)+"-keypad-title").c_str());
          lv_textarea_set_text(jbc_detail_input,"320");lv_obj_send_event(keyboard,LV_EVENT_READY,nullptr);
          assert(!jbc_detail_editor&&state.pending&&state.control_value==3200);state.pending=false;
        }
        // Datagram retries may deliver later rows before earlier ones.
        state.rows[1].label[0]=0;lv_update_module_detail();render((std::string(name)+"-partial").c_str());
      }
      lv_update_jbc_detail(false);
      // The prototype System page must not render the previous detail widgets.
      lv_obj_add_flag(lv_obj_get_parent(ui_detail_values),LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ui_detail_title,LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(lv_obj_get_child(ui_module_detail_screen,0),"System");
      settings();
      pairing();
      lv_obj_delete(ui_module_detail_screen);
      check_jbc_bounds=false;remaining_pages();home_page();detail_examples();control_examples();header_examples();screensaver_examples();check_jbc_bounds=true;
    }
    lv_display_delete(d);
  }
  puts("PASS: shared System/JBC UI, both resolutions/themes, fixed navigation, compact keypad, validation/cancel/confirmation, settings bounds and callbacks");
}
