#pragma once
// Included after the sketch's theme, fonts and translation helpers.
class OfeDisplayWifiUi {
public:
  static void openEvent(lv_event_t*) { instance().open(); }
  static bool isOpen() { return instance().root_!=nullptr; }
  static void closePanel() { instance().close(); }
private:
  lv_obj_t* root_=nullptr;
  lv_obj_t* form_=nullptr;
  lv_obj_t* keyboard_=nullptr;
  lv_obj_t* state_=nullptr;
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
  bool dirty_=false, filling_=false, reload_on_save_=false;
  const char* text(const char* en,const char* de) const { return de_ ? de : en; }
  static OfeDisplayWifiUi& instance() { static OfeDisplayWifiUi ui; return ui; }
  lv_obj_t* label(lv_obj_t* parent,const char* value,int x,int y,int width) {
    auto* obj=lv_label_create(parent); lv_label_set_text(obj,value); lv_obj_set_pos(obj,x,y);
    lv_obj_set_width(obj,width); lv_label_set_long_mode(obj,LV_LABEL_LONG_WRAP); return obj;
  }
  lv_obj_t* button(lv_obj_t* parent,const char* value,int x,int y,int width,lv_event_cb_t callback) {
    auto* obj=lv_button_create(parent); lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,width,40);
    lv_obj_set_style_radius(obj,6,0); auto* l=lv_label_create(obj); lv_label_set_text(l,value);
    lv_obj_center(l); lv_obj_add_event_cb(obj,callback,LV_EVENT_CLICKED,this); return obj;
  }
  lv_obj_t* field(const char* title,int y,int max,bool secret=false) {
    int width=lv_display_get_horizontal_resolution(nullptr);
    label(form_,title,0,y+9,122);
    auto* input=lv_textarea_create(form_); lv_obj_set_pos(input,126,y); lv_obj_set_size(input,width-158,40);
    lv_textarea_set_one_line(input,true); lv_textarea_set_max_length(input,max);
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
    int w=lv_display_get_horizontal_resolution(nullptr),h=lv_display_get_vertical_resolution(nullptr);
    root_=lv_obj_create(lv_layer_top()); lv_obj_set_size(root_,w,h); lv_obj_set_pos(root_,0,0);
    lv_obj_set_style_pad_all(root_,0,0); lv_obj_set_style_radius(root_,0,0); lv_obj_set_style_border_width(root_,0,0);
    lv_obj_set_style_bg_color(root_,ui_theme_color(0x121820,0xF5F7FA),0);
    lv_obj_set_style_text_color(root_,ui_theme_color(0xFFFFFF,0x17212B),0);
    lv_obj_set_style_text_font(root_,UI_FONT_DEFAULT,0); lv_obj_remove_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
    state_=label(root_,text("Connection","Verbindung"),12,10,w-68);
    lv_obj_set_height(state_,36);
    lv_label_set_long_mode(state_,LV_LABEL_LONG_WRAP);
    button(root_,LV_SYMBOL_CLOSE,w-48,2,44,closeEvent);
    form_=lv_obj_create(root_); lv_obj_set_pos(form_,12,50); lv_obj_set_size(form_,w-24,h-54);
    lv_obj_set_style_pad_all(form_,0,0); lv_obj_set_style_border_width(form_,0,0);
    lv_obj_set_style_bg_opa(form_,LV_OPA_TRANSP,0); lv_obj_set_scroll_dir(form_,LV_DIR_VER);
    label(form_,text("Transport","Verbindung"),0,9,122);
    mode_=lv_dropdown_create(form_); lv_obj_set_pos(mode_,126,0); lv_obj_set_size(mode_,w-158,40);
    lv_dropdown_set_options(mode_,text("Automatic\nRS485\nWiFi","Automatisch\nRS485\nWLAN"));
    lv_obj_add_event_cb(mode_,changedEvent,LV_EVENT_VALUE_CHANGED,this);
    ssid_=field("SSID",50,32);
    button(form_,LV_SYMBOL_REFRESH,0,100,112,scanEvent);
    networks_=lv_dropdown_create(form_); lv_obj_set_pos(networks_,126,100); lv_obj_set_size(networks_,w-158,40);
    lv_dropdown_set_options(networks_,text("Networks","Netzwerke"));
    lv_obj_add_event_cb(networks_,networkEvent,LV_EVENT_VALUE_CHANGED,this);
    password_=field(text("Password","Passwort"),150,64,true);
    host_=field(text("Master IP / host","Master IP / Host"),200,63);
    key_=field(text("Pairing code","Kopplungscode"),250,32,true);
    lv_textarea_set_accepted_chars(key_,"0123456789ABCDEFabcdef");
    char serial[48]; snprintf(serial,sizeof(serial),"%s: %016llX",text("Serial number","Seriennummer"),(unsigned long long)display_wifi.uid());
    label(form_,serial,0,300,w-48);
    button(form_,text("Apply","Übernehmen"),0,340,(w-40)/2,saveEvent);
    button(form_,text("From master","Vom Master"),(w-40)/2+8,340,(w-40)/2,importEvent);
    keyboard_=lv_keyboard_create(root_); lv_obj_set_size(keyboard_,w,h/2); lv_obj_align(keyboard_,LV_ALIGN_BOTTOM_MID,0,0);
    lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard_,keyboardEvent,LV_EVENT_ALL,this);
    fill(); timer_=lv_timer_create(timerEvent,500,this); refresh();
  }
  void close() {
    if (timer_) { lv_timer_delete(timer_); timer_=nullptr; }
    if (root_) { lv_obj_delete(root_); root_=nullptr; }
    networks_text_[0]=0;
    scan_revision_=UINT32_MAX;
  }
  void refresh() {
    if (!root_) return;
    auto v=display_wifi.view();
    const char* result=v.result==1 ? text("Saving...","Speichert...") :
      v.result==2 ? text("Saved","Gespeichert") :
      v.result==3 ? text("Save failed","Speichern fehlgeschlagen") :
      v.result==4 ? text("WiFi task unavailable","WLAN-Task nicht verfügbar") :
      v.result==5 ? text("WiFi initialization failed","WLAN-Initialisierung fehlgeschlagen") :
      v.result==6 ? text("Scan failed","Netzwerksuche fehlgeschlagen") : "";
    if (v.scanning && v.result!=5) result=text("Scanning...","Suche läuft...");
    char line[128];
    const char* transport=v.active ? text("WiFi: master connected","WLAN: Master verbunden") :
      v.config.mode==ofe_wifi::WIRELESS ? text("WiFi: waiting for master","WLAN: wartet auf Master") :
      master_link_online() ? "RS485" : text("No master connection","Keine Master-Verbindung");
    snprintf(line,sizeof(line),"%s%s%s  %s",transport,
      v.wifi ? " | " : "",v.wifi ? v.ip : "",result);
    if (v.wifi) {
      char signal[20]; snprintf(signal,sizeof(signal),"  %d dBm",v.rssi);
      strlcat(line,signal,sizeof(line));
    }
    if (v.result==5) snprintf(line,sizeof(line),"%s | RAM %lu KB / Block %lu KB",
      result,(unsigned long)(v.free_internal/1024),(unsigned long)(v.largest_internal/1024));
    lv_label_set_text(state_,line);
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
  static void closeEvent(lv_event_t* e) { self(e)->close(); }
  static void scanEvent(lv_event_t*) { display_wifi.scan(); }
  static void importEvent(lv_event_t* e) {
    auto* ui=self(e); auto v=display_wifi.view();
    v.config.from_master=1; v.config.mode=lv_dropdown_get_selected(ui->mode_);
    if (!display_wifi.save(v.config)) lv_label_set_text(ui->state_,ui->text("Save failed","Speichern fehlgeschlagen"));
    else { ui->dirty_=false; ui->reload_on_save_=true; ui->hideKeyboard(); }
  }
  static void saveEvent(lv_event_t* e) {
    auto* ui=self(e); auto v=display_wifi.view(); auto& c=v.config;
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
    lv_obj_set_height(ui->form_,lv_display_get_vertical_resolution(nullptr)/2-54);
    lv_obj_scroll_to_view(static_cast<lv_obj_t*>(lv_event_get_target(e)),LV_ANIM_OFF);
  }
  static void changedEvent(lv_event_t* e) {
    auto* ui=self(e);
    if (!ui->filling_) { ui->dirty_=true; ui->reload_on_save_=false; }
  }
  void hideKeyboard() {
    lv_keyboard_set_textarea(keyboard_,nullptr); lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(form_,lv_display_get_vertical_resolution(nullptr)-54);
  }
  static void keyboardEvent(lv_event_t* e) {
    if (lv_event_get_code(e)==LV_EVENT_READY || lv_event_get_code(e)==LV_EVENT_CANCEL) self(e)->hideKeyboard();
  }
  static void timerEvent(lv_timer_t* timer) { static_cast<OfeDisplayWifiUi*>(lv_timer_get_user_data(timer))->refresh(); }
};
