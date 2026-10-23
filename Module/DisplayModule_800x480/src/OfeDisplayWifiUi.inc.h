#pragma once
#include "OfeDisplayUi.h"
// Included after the sketch's theme, fonts and translation helpers.
class OfeDisplayWifiUi {
public:
  static void openEvent(lv_event_t*) { instance().open(); }
  static void openPanel() { instance().open(); }
  static bool isOpen() { return instance().root_!=nullptr; }
  static void closePanel() { instance().close(); }
private:
  static constexpr int HEADER_HEIGHT=42;
  static int contentHeight() { return lv_display_get_vertical_resolution(nullptr)-HEADER_HEIGHT; }
  static int keyboardHeight() { return lv_display_get_vertical_resolution(nullptr)/2; }
  lv_obj_t* root_=nullptr;
  lv_obj_t* form_=nullptr;
  lv_obj_t* keyboard_=nullptr;
  lv_obj_t* dialog_=nullptr;
  uint8_t dialog_kind_=0;
  uint32_t dialog_token_=0,dialog_pin_=0;
  lv_obj_t* state_=nullptr;
  lv_obj_t* connection_=nullptr;
  lv_obj_t* ble_button_=nullptr;
  lv_obj_t* mode_=nullptr;
  lv_obj_t* ssid_=nullptr;
  lv_obj_t* password_=nullptr;
  lv_obj_t* host_=nullptr;
  lv_obj_t* key_=nullptr;
  lv_obj_t* networks_=nullptr;
  lv_timer_t* timer_=nullptr;
  uint32_t revision_=0;
  uint32_t scan_revision_=UINT32_MAX;
  char networks_text_[544]={};
  bool de_=false;
  bool dirty_=false, filling_=false, reload_on_save_=false,ble_feedback_=false;
  const char* text(const char* en,const char* de) const { return de_ ? de : en; }
  static OfeDisplayWifiUi& instance() { static OfeDisplayWifiUi ui; return ui; }
  static void setLabel(lv_obj_t* obj,const char* value) {
    if (strcmp(lv_label_get_text(obj),value)) lv_label_set_text(obj,value);
  }
  lv_obj_t* label(lv_obj_t* parent,const char* value,int x,int y,int width) {
    auto* obj=lv_label_create(parent); lv_label_set_text(obj,value); lv_obj_set_pos(obj,x,y);
    lv_obj_set_style_text_font(obj,UI_FONT_DEFAULT,0); lv_obj_set_style_text_color(obj,ofe_display_ui::text(),0);
    lv_obj_set_width(obj,width); lv_label_set_long_mode(obj,LV_LABEL_LONG_WRAP); return obj;
  }
  lv_obj_t* button(lv_obj_t* parent,const char* value,int x,int y,int width,lv_event_cb_t callback) {
    auto* obj=lv_button_create(parent); lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,width,44);
    controlTheme(obj);
    auto* l=lv_label_create(obj); lv_label_set_text(l,value);
    lv_obj_set_width(l,width-16); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);
    lv_obj_center(l); ofe_display_ui::skin_button(obj);
    lv_obj_add_event_cb(obj,callback,LV_EVENT_CLICKED,this); return obj;
  }
  void controlTheme(lv_obj_t* obj) {
    ofe_display_ui::flat(obj,ofe_display_ui::field());
    lv_obj_set_style_border_width(obj,1,0);
    lv_obj_set_style_border_color(obj,ofe_display_ui::outline(),0);
    lv_obj_set_style_text_color(obj,ofe_display_ui::text(),0);
    lv_obj_set_style_shadow_width(obj,0,0);
  }
  void section(const char* title,int x,int y,int width) {
    auto* heading=label(form_,title,x,y,width);
    lv_obj_set_style_text_color(heading,ofe_display_ui::accent(),0);
    lv_obj_set_style_text_font(heading,UI_FONT_18,0);
  }
  lv_obj_t* dropdown(int x,int y,int width,const char* options,lv_event_cb_t callback) {
    auto* obj=lv_dropdown_create(form_); lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,width,44);
    lv_dropdown_set_options(obj,options); ofe_display_ui::dropdown(obj);
    lv_obj_add_event_cb(obj,callback,LV_EVENT_VALUE_CHANGED,this); return obj;
  }
  lv_obj_t* field(const char* title,int x,int y,int width,int max,bool secret=false) {
    label(form_,title,x,y+12,122);
    auto* input=lv_textarea_create(form_); lv_obj_set_pos(input,x+126,y); lv_obj_set_size(input,width-126,44);
    controlTheme(input);
    lv_textarea_set_one_line(input,true); lv_textarea_set_max_length(input,max);
    lv_obj_set_height(input,44); lv_obj_set_style_pad_top(input,6,0); lv_obj_set_style_pad_bottom(input,6,0);
    lv_textarea_set_password_mode(input,secret);
    lv_obj_add_event_cb(input,focusEvent,LV_EVENT_FOCUSED,this);
    lv_obj_add_event_cb(input,changedEvent,LV_EVENT_VALUE_CHANGED,this); return input;
  }
  void fill() {
    filling_=true;
    auto v=display_wifi.view(); revision_=v.revision;
    lv_dropdown_set_selected(mode_,v.config.mode);
    lv_textarea_set_text(ssid_,v.config.ssid); lv_textarea_set_text(password_,v.config.password);
    lv_textarea_set_text(host_,v.config.host); char key[33]={};
    if (ofe_wifi::nonzero(v.config.key,16)) ofe_wifi::hex(v.config.key,16,key);
    lv_textarea_set_text(key_,key);
    filling_=false; dirty_=false; reload_on_save_=false;
  }
  void open() {
    if (root_) return;
    de_=display_language==1;
    int w=lv_display_get_horizontal_resolution(nullptr),h=contentHeight();
    // Keep the live screen header visible, including alarm and transport status.
    root_=lv_obj_create(lv_layer_top()); lv_obj_set_size(root_,w,h); lv_obj_set_pos(root_,0,HEADER_HEIGHT);
    lv_obj_set_style_pad_all(root_,0,0); lv_obj_set_style_radius(root_,0,0); lv_obj_set_style_border_width(root_,0,0);
    ofe_display_ui::flat(root_,ofe_display_ui::background(),0);
    lv_obj_set_style_text_color(root_,ofe_display_ui::text(),0);
    lv_obj_set_style_text_font(root_,UI_FONT_DEFAULT,0); lv_obj_remove_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
    auto* title=label(root_,text("Display connection","Display-Verbindung"),12,3,w-68);
    lv_obj_set_style_text_font(title,UI_FONT_18,0);
    connection_=label(root_,"",12,28,w-68); lv_obj_set_height(connection_,18);
    lv_label_set_long_mode(connection_,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(connection_,ofe_display_ui::muted(),0);
    button(root_,LV_SYMBOL_CLOSE,w-48,2,44,closeEvent);
    form_=lv_obj_create(root_); lv_obj_set_pos(form_,12,50); lv_obj_set_size(form_,w-24,h-54);
    lv_obj_set_style_pad_all(form_,0,0); lv_obj_set_style_border_width(form_,0,0);
    lv_obj_set_style_bg_opa(form_,LV_OPA_TRANSP,0); lv_obj_set_scroll_dir(form_,LV_DIR_VER);
    const bool wide=ofe_display_ui::wide(w);
    const int width=w-24,column=wide ? (width-16)/2 : width;
    const int right=wide ? column+16 : 0;
    ble_button_=button(form_,text("Pair via Bluetooth","Per Bluetooth koppeln"),0,0,column,bleStartEvent);
    state_=label(form_,"",0,54,width); lv_obj_set_height(state_,38);
    const int mode_y=wide ? 0 : 104;
    label(form_,text("Transport","Verbindung"),right,mode_y+12,122);
    mode_=dropdown(right+126,mode_y,column-126,
      text("Automatic\nRS485\nWiFi","Automatisch\nRS485\nWLAN"),changedEvent);
    const int wifi_y=wide ? 108 : 166,master_y=wide ? 108 : 372;
    section(text("WiFi","WLAN"),0,wifi_y,column);
    networks_=dropdown(0,wifi_y+28,column-54,text("Networks","Netzwerke"),networkEvent);
    button(form_,LV_SYMBOL_REFRESH,column-44,wifi_y+28,44,scanEvent);
    ssid_=field("SSID",0,wifi_y+82,column,32);
    password_=field(text("Password","Passwort"),0,wifi_y+136,column,64,true);
    section(text("Master","Master"),right,master_y,column);
    host_=field(text("IP / host","IP / Host"),right,master_y+28,column,63);
    key_=field(text("Pairing code","Kopplungscode"),right,master_y+82,column,32,true);
    lv_textarea_set_accepted_chars(key_,"0123456789ABCDEFabcdef");
    char serial[48]; snprintf(serial,sizeof(serial),"%s: %016llX",text("Serial number","Seriennummer"),(unsigned long long)display_wifi.uid());
    auto* identity=label(form_,serial,right,master_y+136,column);
    lv_obj_set_height(identity,22); lv_label_set_long_mode(identity,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(identity,ofe_display_ui::muted(),0);
    const int actions_y=wide ? 338 : 544,action_width=(width-12)/2;
    auto* apply=button(form_,text("Apply","Übernehmen"),0,actions_y,action_width,saveEvent);
    lv_obj_set_style_bg_color(apply,ui_theme_color(0x176792,0x14628F),0);
    lv_obj_set_style_text_color(apply,lv_color_white(),0);
    lv_obj_set_style_text_color(lv_obj_get_child(apply,0),lv_color_white(),0);
    button(form_,text("From master","Vom Master"),action_width+12,actions_y,action_width,importEvent);
    auto* forget=button(form_,text("Forget master pairing","Master-Kopplung löschen"),
      right,wide ? 278 : 604,column,forgetEvent);
    lv_obj_set_style_text_color(forget,ui_theme_color(0xFFAAA4,0xAF3434),0);
    lv_obj_set_style_text_color(lv_obj_get_child(forget,0),ui_theme_color(0xFFAAA4,0xAF3434),0);
    keyboard_=lv_keyboard_create(root_); lv_obj_set_size(keyboard_,w,keyboardHeight()); lv_obj_align(keyboard_,LV_ALIGN_BOTTOM_MID,0,0);
    ofe_display_ui::keyboard(keyboard_);
    lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard_,keyboardEvent,LV_EVENT_ALL,this);
    fill(); timer_=lv_timer_create(timerEvent,500,this); refresh();
  }
  void close() {
    if (display_ble.active()) display_ble.cancel();
    dialog_=nullptr; dialog_kind_=0;
    if (timer_) { lv_timer_delete(timer_); timer_=nullptr; }
    if (root_) { lv_obj_delete(root_); root_=nullptr; }
    networks_text_[0]=0;
    scan_revision_=UINT32_MAX;
  }
  void refresh() {
    if (!root_) return;
    auto v=display_wifi.view();
    const auto ble=display_ble.view();
    if (ble.active && ble.phase==ofe_ble::CONFIRM && (!dialog_ || dialog_kind_!=2 || dialog_token_!=ble.token))
      showDialog(2,ble.token,ble.pin);
    else if (dialog_kind_==2 && (!ble.active || ble.phase!=ofe_ble::CONFIRM)) closeDialog();
    const char* result=v.result==1 ? text("Saving...","Speichert...") :
      v.result==2 ? text("Saved","Gespeichert") :
      v.result==3 ? text("Save failed","Speichern fehlgeschlagen") :
      v.result==4 ? text("WiFi task unavailable","WLAN-Task nicht verfügbar") :
      v.result==5 ? text("WiFi initialization failed","WLAN-Initialisierung fehlgeschlagen") :
      v.result==6 ? text("Scan failed","Netzwerksuche fehlgeschlagen") : "";
    if (v.scanning && v.result!=5) result=text("Scanning...","Suche läuft...");
    if (ble.active) result=ble.phase==ofe_ble::CONFIRM ? text("Compare code","Code vergleichen") :
      ble.phase==ofe_ble::AUTHENTICATING ? text("Confirm on the master too","Auch am Master bestätigen") :
      ble.phase==ofe_ble::SAVING ? text("Bluetooth: saving","Bluetooth: speichert") :
      text("Bluetooth pairing active","Bluetooth-Kopplung aktiv");
    else if (ble_feedback_ && ble.phase==ofe_ble::DONE) result=text("Bluetooth: paired","Bluetooth: gekoppelt");
    else if (ble_feedback_ && ble.phase==ofe_ble::LOW_MEMORY) result=text("Bluetooth: insufficient RAM","Bluetooth: zu wenig RAM");
    else if (ble_feedback_ && ble.phase==ofe_ble::FAILED) result=text("Bluetooth pairing failed","Bluetooth-Kopplung fehlgeschlagen");
    else if (ble_feedback_ && ble.phase==ofe_ble::CANCELLED) result=text("Bluetooth: cancelled","Bluetooth: abgebrochen");
    else if (ble_feedback_ && ble.phase==ofe_ble::EXPIRED) result=text("Bluetooth: timed out","Bluetooth: Zeit abgelaufen");
    char line[128];
    const char* transport=v.active ? text("WiFi: master connected","WLAN: Master verbunden") :
      v.config.mode==ofe_wifi::WIRELESS ? text("WiFi: waiting for master","WLAN: wartet auf Master") :
      master_link_online() ? "RS485" : text("No master connection","Keine Master-Verbindung");
    snprintf(line,sizeof(line),"%s%s%s",transport,
      v.wifi ? " | " : "",v.wifi ? v.ip : "");
    if (v.wifi) {
      char signal[20]; snprintf(signal,sizeof(signal),"  %d dBm",v.rssi);
      strlcat(line,signal,sizeof(line));
    }
    setLabel(connection_,line);
    snprintf(line,sizeof(line),"%s",result[0] ? result : text("Ready to pair","Bereit zum Koppeln"));
    if (ble_feedback_ && !ble.active && ble.phase==ofe_ble::FAILED && ble.error_phase)
      snprintf(line,sizeof(line),"%s (Phase %u, BLE %d)",result,(unsigned)ble.error_phase,ble.error_code);
    if (v.result==5) snprintf(line,sizeof(line),"%s | RAM %lu KB / Block %lu KB",
      result,(unsigned long)(v.free_internal/1024),(unsigned long)(v.largest_internal/1024));
    setLabel(state_,line);
    setLabel(lv_obj_get_child(ble_button_,0),ble.active ? text("Cancel Bluetooth","Bluetooth abbrechen") :
      text("Pair via Bluetooth","Per Bluetooth koppeln"));
    if (scan_revision_!=v.scan_revision || strcmp(networks_text_,v.networks)) {
      scan_revision_=v.scan_revision;
      strlcpy(networks_text_,v.networks,sizeof(networks_text_));
      lv_dropdown_set_options(networks_,v.networks[0] ? v.networks :
        !v.scan_revision ? text("Networks","Netzwerke") :
        v.result==6 ? text("Scan failed","Suche fehlgeschlagen") : text("No networks","Keine Netzwerke"));
    }
    if (v.revision!=revision_) {
      if (!dirty_ || reload_on_save_) fill();
      else revision_=v.revision;
    }
  }
  static OfeDisplayWifiUi* self(lv_event_t* e) { return static_cast<OfeDisplayWifiUi*>(lv_event_get_user_data(e)); }
  void closeDialog() {
    if (dialog_) { lv_obj_delete(dialog_); dialog_=nullptr; }
    dialog_kind_=0;
  }
  void showDialog(uint8_t kind,uint32_t token=0,uint32_t pin=0) {
    closeDialog(); hideKeyboard();
    dialog_kind_=kind; dialog_token_=token; dialog_pin_=pin;
    const int screen_width=lv_display_get_horizontal_resolution(nullptr);
    const int width=screen_width>=700 ? 480 : screen_width-24;
    dialog_=lv_obj_create(root_);
    lv_obj_set_size(dialog_,lv_display_get_horizontal_resolution(nullptr),contentHeight());
    lv_obj_set_pos(dialog_,0,0); lv_obj_set_style_pad_all(dialog_,0,0);
    lv_obj_set_style_border_width(dialog_,0,0); lv_obj_set_style_radius(dialog_,0,0);
    lv_obj_set_style_bg_color(dialog_,lv_color_black(),0); lv_obj_set_style_bg_opa(dialog_,LV_OPA_50,0);
    lv_obj_remove_flag(dialog_,LV_OBJ_FLAG_SCROLLABLE);
    auto* panel=lv_obj_create(dialog_); lv_obj_set_size(panel,width,182); lv_obj_center(panel);
    controlTheme(panel);
    lv_obj_set_style_pad_all(panel,0,0); lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    ofe_display_ui::flat(panel,ofe_display_ui::surface(),8);
    label(panel,kind==1 ? text("Forget master pairing?","Master-Kopplung löschen?") :
      text("Same code on the master?","Gleicher Code im Master?"),12,12,width-24);
    if (kind==2) {
      char code[8]; snprintf(code,sizeof(code),"%06lu",(unsigned long)pin);
      auto* value=label(panel,code,12,48,width-24);
      lv_obj_set_style_text_font(value,UI_FONT_28,0);
      lv_obj_set_style_text_align(value,LV_TEXT_ALIGN_CENTER,0);
    } else label(panel,text("WiFi credentials remain saved.","WLAN-Zugangsdaten bleiben gespeichert."),12,50,width-24);
    button(panel,text("Cancel","Abbrechen"),12,122,(width-36)/2,dialogCancelEvent);
    button(panel,kind==1 ? text("Forget","Löschen") : text("Code matches","Code stimmt"),
      24+(width-36)/2,122,(width-36)/2,dialogAcceptEvent);
  }
  static void bleStartEvent(lv_event_t* e) {
    auto* ui=self(e); ui->hideKeyboard();
    if (display_ble.active()) { display_ble.cancel(); return; }
    if (fw_update_active || status.update_active || !display_ble.start())
      lv_label_set_text(ui->state_,ui->text("Bluetooth busy or unavailable","Bluetooth belegt oder nicht verfügbar"));
    else ui->ble_feedback_=true;
  }
  static void forgetEvent(lv_event_t* e) {
    auto* ui=self(e);
    if (display_ble.active()) { display_ble.cancel(); return; }
    ui->ble_feedback_=false;
    ui->showDialog(1);
  }
  static void dialogCancelEvent(lv_event_t* e) {
    auto* ui=self(e);
    if (ui->dialog_kind_==2) display_ble.approve(ui->dialog_token_,ui->dialog_pin_,false);
    ui->closeDialog();
  }
  static void dialogAcceptEvent(lv_event_t* e) {
    auto* ui=self(e);
    const bool ok=ui->dialog_kind_==1 ? display_wifi.forgetPairing() :
      display_ble.approve(ui->dialog_token_,ui->dialog_pin_,true);
    if (ok) { ui->dirty_=false; ui->reload_on_save_=true; ui->closeDialog(); }
    else lv_label_set_text(ui->state_,ui->text("Operation unavailable","Aktion nicht verfügbar"));
  }
  static void closeEvent(lv_event_t* e) { self(e)->close(); }
  static void scanEvent(lv_event_t*) { display_wifi.scan(); }
  static void importEvent(lv_event_t* e) {
    if (display_ble.active()) return;
    auto* ui=self(e); ui->ble_feedback_=false; auto v=display_wifi.view();
    v.config.from_master=1; v.config.mode=lv_dropdown_get_selected(ui->mode_);
    if (!display_wifi.save(v.config)) lv_label_set_text(ui->state_,ui->text("Save failed","Speichern fehlgeschlagen"));
    else { ui->dirty_=false; ui->reload_on_save_=true; ui->hideKeyboard(); }
  }
  static void saveEvent(lv_event_t* e) {
    if (display_ble.active()) return;
    auto* ui=self(e); ui->ble_feedback_=false; auto v=display_wifi.view(); auto& c=v.config;
    c.from_master=0; c.mode=lv_dropdown_get_selected(ui->mode_);
    strlcpy(c.ssid,lv_textarea_get_text(ui->ssid_),sizeof(c.ssid));
    strlcpy(c.password,lv_textarea_get_text(ui->password_),sizeof(c.password));
    strlcpy(c.host,lv_textarea_get_text(ui->host_),sizeof(c.host));
    const char* key=lv_textarea_get_text(ui->key_);
    bool key_ok=ofe_wifi::unhex(key,c.key,16);
    if ((!key_ok && c.mode!=ofe_wifi::WIRED) || !display_wifi.save(c))
      lv_label_set_text(ui->state_,ui->text("Check SSID, master and 32-digit code","SSID, Master und 32-stelligen Code prüfen"));
    else { ui->dirty_=false; ui->reload_on_save_=true; ui->hideKeyboard(); }
  }
  static void networkEvent(lv_event_t* e) {
    auto* ui=self(e); if (!ui->networks_text_[0]) return;
    char ssid[33]; lv_dropdown_get_selected_str(ui->networks_,ssid,sizeof(ssid));
    lv_textarea_set_text(ui->ssid_,ssid);
  }
  static void focusEvent(lv_event_t* e) {
    auto* ui=self(e);
    lv_keyboard_set_textarea(ui->keyboard_,static_cast<lv_obj_t*>(lv_event_get_target(e)));
    lv_obj_remove_flag(ui->keyboard_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(ui->form_,contentHeight()-keyboardHeight()-54);
    lv_obj_scroll_to_view(static_cast<lv_obj_t*>(lv_event_get_target(e)),LV_ANIM_OFF);
  }
  static void changedEvent(lv_event_t* e) {
    auto* ui=self(e);
    if (!ui->filling_) { ui->dirty_=true; ui->reload_on_save_=false; }
  }
  void hideKeyboard() {
    lv_keyboard_set_textarea(keyboard_,nullptr); lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(form_,contentHeight()-54);
  }
  static void keyboardEvent(lv_event_t* e) {
    if (lv_event_get_code(e)==LV_EVENT_READY || lv_event_get_code(e)==LV_EVENT_CANCEL) self(e)->hideKeyboard();
  }
  static void timerEvent(lv_timer_t* timer) { static_cast<OfeDisplayWifiUi*>(lv_timer_get_user_data(timer))->refresh(); }
};
