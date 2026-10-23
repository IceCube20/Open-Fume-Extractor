#pragma once
constexpr unsigned DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX=32,CAP_FILTER_SENSOR=256;
static uint8_t module_addr=0x40,detail_controls_addr=0,detail_controls_type=MODULE_UNKNOWN;
static uint32_t detail_controls_caps=0,detail_controls_signature=0;
static bool ui_detail_universal_select_refreshing=false;
static lv_obj_t *ui_detail_weller_filter_dropdown,*ui_detail_jbc_mode_dropdown,*ui_detail_jbc_power_input;
static lv_obj_t *ui_detail_jbc_delay_work_input,*ui_detail_jbc_delay_stand_input,*ui_detail_weller_speed_slider;
static lv_obj_t *ui_detail_display_brightness_slider,*ui_detail_display_brightness_value,*ui_detail_display_language,*ui_detail_display_theme;
static lv_obj_t *ui_detail_stand_button,*ui_detail_continuous_button,*ui_detail_fan_button,*ui_detail_out1_button,*ui_detail_out2_button;
static lv_obj_t *ui_detail_output_power_slider,*ui_detail_output_power_value,*ui_detail_weller_fan_button,*ui_detail_weller_light_button;
static lv_obj_t *ui_detail_universal_sliders[32],*ui_detail_universal_switches[32],*ui_detail_universal_values[32];
static lv_obj_t *ui_detail_universal_buttons[32],*ui_detail_universal_buttons_b[32],*ui_detail_universal_selects[32];
static bool selected_detail_is_jbc(){return selected_module.type==MODULE_JBC_BUS;}
static bool selected_detail_is_fan_io(){return selected_module.type==MODULE_FAN_IO;}
static bool selected_detail_is_weller(){return selected_module.type==MODULE_WELLER_ZERO_SMOG;}
static bool selected_detail_is_universal(){return selected_module.type==MODULE_MODBUS_RTU;}
static bool selected_fan_io_main_output_configured(){return selected_module.main_configured;}
static bool fan_io_gpio_output_entity(const DisplayUniversalEntity& e){return e.id>=40&&e.id<48;}
static uint8_t selected_fan_io_gpio_output_count(){uint8_t n=0;for(unsigned i=0;i<selected_module.universal_entity_count;++i)if(fan_io_gpio_output_entity(selected_module.universal_entities[i]))++n;return n;}
static uint8_t selected_universal_control_count(){return selected_module.universal_entity_count;}
static uint32_t selected_universal_controls_signature(){return selected_module.universal_entity_count;}
static String detail_alias_or(const char* label,const char* fallback){return *label?label:fallback;}
static String fan_io_entity_label(const DisplayUniversalEntity& e,bool){return e.label;}
static bool universal_pending_value(uint8_t,uint8_t,uint8_t,int16_t&){return false;}
static bool universal_entity_writable(const DisplayUniversalEntity&){return true;}
static bool universal_entity_readable(const DisplayUniversalEntity&){return true;}
#include "universal_value.inc.h"
static String universal_select_option_text(const DisplayUniversalEntity&,int16_t){return "6d 16h 0m";}
static const char* on_off(bool v){return tr(v?"On":"Off",v?"An":"Aus");}
static const char* weller_filter_options(){return tr("Off\n6 days 16 hours","Aus\n6 Tage 16 Stunden");}
static unsigned weller_filter_preset_index(unsigned){return 1;}
SETTINGS_EVENT(lv_stand_intakes_toggle_event,40)
SETTINGS_EVENT(lv_continuous_toggle_event,41)
SETTINGS_EVENT(lv_detail_fan_event,42)
SETTINGS_EVENT(lv_detail_output_power_slider_event,43)
SETTINGS_EVENT(lv_detail_universal_switch_event,44)
SETTINGS_EVENT(lv_detail_universal_slider_event,45)
SETTINGS_EVENT(lv_detail_universal_select_event,46)
SETTINGS_EVENT(lv_detail_universal_button_event,47)
SETTINGS_EVENT(lv_detail_weller_fan_event,48)
SETTINGS_EVENT(lv_detail_weller_light_event,49)
SETTINGS_EVENT(lv_detail_weller_speed_slider_event,50)
SETTINGS_EVENT(lv_detail_weller_filter_event,51)
SETTINGS_EVENT(lv_detail_weller_reset_event,52)
SETTINGS_EVENT(lv_detail_display_brightness_event,53)
#include "control_sizing.inc.h"
#include "OfeDisplayControlsUi.inc.h"
