#pragma once
#include <errno.h>
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "HostedC6WebUi.h"
static bool web_c6_chunk_ok=false;
static uint32_t web_c6_chunk_bytes=0;
static bool web_c6_number(const char* name,uint32_t maximum,uint32_t& value) {
  const String text=web.arg(name);if(!text.length() || text[0]<'0' || text[0]>'9')return false;
  char* end=nullptr;errno=0;const unsigned long parsed=strtoul(text.c_str(),&end,10);
  if(errno || !end || *end || parsed>maximum)return false;value=uint32_t(parsed);return true;
}
static bool web_c6_lan() {return master_ethernet_ready() && master_lan_wifi_policy.suspended();}
static bool web_c6_other_update() {return master_update_active || module_update_addr || scheduler.firmwareUpdateActive() || master_update_restart_due_ms.load() || master_display_ble.active() || hostedIsBLEActive() || WiFi.scanComplete()==WIFI_SCAN_RUNNING;}
static void web_c6_reply(ofe_c6::Error error) {
  String json="{\"ok\":";json+=error==ofe_c6::Ok?"true":"false";
  json+=",\"error\":\"";json+=ofe_c6::errorName(error);json+="\",\"received\":";json+=hosted_c6().received();json+='}';
  web.send(error==ofe_c6::Ok?200:409,"application/json",json);
}
static void web_handle_c6_status() {
  auto& c=hosted_c6();String json;json.reserve(400);
  json="{\"phase\":";json+=uint8_t(c.phase());json+=",\"error\":\"";json+=ofe_c6::errorName(c.error());
  json+="\",\"current\":";json+=c.slaveVersion();json+=",\"host\":";json+=c.hostVersion();
  json+=",\"expected\":";json+=ofe_c6::VERSION;json+=",\"received\":";json+=c.received();
  json+=",\"written\":";json+=c.written();json+=",\"size\":";json+=ofe_c6::IMAGE_SIZE;
  json+=",\"lan\":";json+=web_c6_lan()?"true":"false";
  json+=",\"other_busy\":";json+=web_c6_other_update()?"true":"false";
  uint32_t token=0;
  json+=",\"owned\":";json+=web_c6_number("token",UINT32_MAX,token) && c.owns(token)?"true":"false";
  json+=",\"verification\":";json+=c.verification();json+='}';web.send(200,"application/json",json);
}
static void web_handle_c6_begin() {
  if (!web_c6_lan()) {web_c6_reply(ofe_c6::NoLan);return;}
  if (web_c6_other_update()) {web_c6_reply(ofe_c6::Busy);return;}
  uint32_t size=0;if(!web_c6_number("size",ofe_c6::IMAGE_SIZE,size)){web_c6_reply(ofe_c6::Size);return;}
  uint32_t token=esp_random();if(!token)token=1;
  const auto result=hosted_c6().begin(size,token,millis());
  if(result!=ofe_c6::Ok){web_c6_reply(result);return;}
  web.send(200,"application/json",String("{\"ok\":true,\"token\":")+String(token)+"}");
}
static void web_handle_c6_chunk_upload() {
  auto& u=web.upload();uint32_t token=0,offset=0;
  if(u.status==UPLOAD_FILE_START) {
    web_c6_chunk_bytes=0;
    web_c6_chunk_ok=web_c6_lan() && web_c6_number("token",UINT32_MAX,token) && hosted_c6().owns(token) &&
      hosted_c6().phase()==HostedC6Updater::Receiving && web_c6_number("offset",ofe_c6::IMAGE_SIZE,offset) && offset==hosted_c6().received();
  } else if(u.status==UPLOAD_FILE_WRITE && web_c6_chunk_ok) {
    if(!web_c6_number("token",UINT32_MAX,token) || u.currentSize>16384U-web_c6_chunk_bytes) {web_c6_chunk_ok=false;return;}
    web_c6_chunk_bytes+=u.currentSize;
    web_c6_chunk_ok=hosted_c6().append(token,u.buf,u.currentSize,millis())==ofe_c6::Ok;
  } else if(u.status==UPLOAD_FILE_ABORTED) {
    web_c6_chunk_ok=false;if(web_c6_number("token",UINT32_MAX,token))hosted_c6().cancel(token);
  }
}
static void web_handle_c6_chunk_done() {
  const bool ok=web_c6_chunk_ok && web_c6_chunk_bytes>0;
  web_c6_chunk_ok=false;web_c6_chunk_bytes=0;
  web_c6_reply(ok ? ofe_c6::Ok : ofe_c6::Offset);
}
static void web_handle_c6_seal() {
  uint32_t token=0;
  if(!web_c6_lan()){web_c6_reply(ofe_c6::NoLan);return;}
  if(!web_c6_number("token",UINT32_MAX,token)){web_c6_reply(ofe_c6::Busy);return;}
  web_c6_reply(hosted_c6().seal(token,millis()));
}
static void web_handle_c6_cancel() {
  uint32_t token=0;web_c6_reply(web_c6_number("token",UINT32_MAX,token) && hosted_c6().cancel(token) ? ofe_c6::Ok : ofe_c6::Busy);
}
static void web_handle_c6_install() {
  if(!web_c6_lan()){web_c6_reply(ofe_c6::NoLan);return;}
  if(web_c6_other_update() || web.arg("recovery")!="1"){web_c6_reply(ofe_c6::Busy);return;}
  uint32_t token=0;if(!web_c6_number("token",UINT32_MAX,token)){web_c6_reply(ofe_c6::Busy);return;}
  const bool ok=hosted_c6().start(token,[](){const uint32_t due=millis()+5000U;master_update_restart_due_ms.store(due?due:1U);});
  web_c6_reply(ok?ofe_c6::Ok:ofe_c6::TaskFailed);
}
static void append_hosted_c6_ui(String& html) {
  html+=F("<section class='panel' id='c6_panel'><h2>ESP32-C6 · WLAN / Bluetooth</h2><div class='grid'><div><label>");
  html+=web_text("Installierte Version","Installed version");html+=F("</label><strong id='c6_current'>-</strong></div><div><label>");
  html+=web_text("Passend zur MCU-SDK","Compatible with MCU SDK");html+=F("</label><strong>");html+=ofe_c6::VERSION_TEXT;
  html+=F("</strong></div></div><div class='actions' style='margin-top:12px'><button type='button' class='secondary' id='c6_catalog_check'>");
  html+=web_text("Freigaben prüfen","Check approved releases");html+=F("</button></div><p class='muted' id='c6_catalog_state' role='status'></p><p id='c6_state' role='status'>-</p><div class='actions'><button type='button' id='c6_download'>");
  html+=web_text("Firmware laden und prüfen","Load and verify firmware");html+=F("</button><button type='button' class='secondary' id='c6_cancel' disabled>");
  html+=web_text("Verwerfen","Discard");html+=F("</button></div><label for='c6_file'>");html+=web_text("Firmwaredatei (.bin)","Firmware file (.bin)");
  html+=F("</label><input id='c6_file' type='file' accept='.bin'><label style='display:flex;gap:8px;align-items:center;margin-top:12px'><input id='c6_recovery' type='checkbox' style='width:auto;min-height:0'>");
  html+=web_text("C6-UART-Wiederherstellung ist verfügbar","C6 UART recovery is available");
  html+=F("</label><p class='muted'>");
  html+=web_text("Nur über LAN. WLAN/Bluetooth werden unterbrochen; danach startet die MCU neu.","Ethernet only. WiFi/Bluetooth will be interrupted; the MCU restarts afterwards.");
  html+=F("</p><div class='actions'><button type='button' id='c6_install' disabled>");html+=web_text("C6 aktualisieren","Update C6");
  html+=F("</button><a class='btn secondary' href='https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT/FAQ' target='_blank' rel='noopener noreferrer'>");
  html+=web_text("Wiederherstellung","Recovery");html+=F("</a></div><div class='progress'><div id='c6_bar' class='bar'></div></div></section><script>window.OFE_C6={de:");
  html+=web_is_german()?"true":"false";html+=F(",size:");html+=ofe_c6::IMAGE_SIZE;html+=F(",url:'");html+=ofe_c6::URL;
  html+=F("',expected:");html+=ofe_c6::VERSION;html+=F(",board:'");html+=ofe_c6::BOARD;
  html+=F("',arduino:'");html+=ofe_c6::ARDUINO_VERSION_TEXT;html+=F("',idf:'");html+=ofe_c6::IDF_VERSION_TEXT;
  html+=F("',sha256:'");html+=ofe_c6::SHA256_TEXT;html+=F("',catalog:'");html+=ofe_c6::CATALOG_URL;
  html+=F("',csrf:'");web_csrf_init();html+=web_csrf_token;html+=F("'};</script>");html+=OFE_C6_CATALOG_SCRIPT;html+=OFE_C6_UI_SCRIPT;
}
#else
static void append_hosted_c6_ui(String&) {}
#endif
