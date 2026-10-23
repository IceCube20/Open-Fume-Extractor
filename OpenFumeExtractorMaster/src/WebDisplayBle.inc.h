#pragma once
#include <errno.h>
#if WEB_ENABLE
static bool web_ble_number(const char* name,uint32_t maximum,uint32_t& out) {
  const String value=web.arg(name);
  errno=0;
  char* end=nullptr; const unsigned long parsed=strtoul(value.c_str(),&end,10);
  if (errno==ERANGE || !value.length() || value[0]<'0' || value[0]>'9' || !end || *end || parsed>maximum) return false;
  out=(uint32_t)parsed; return true;
}
static void web_handle_ble_pairing() {
  if (!web_require_auth()) return;
  if (web.method()==HTTP_POST) {
    const String action=web.arg("action");
    bool ok=false;
    if (action=="start") {
      ok=!master_update_active && !scheduler.firmwareUpdateActive() && WiFi.status()==WL_CONNECTED && master_display_ble.start();
    } else if (action=="cancel") {
      master_display_ble.cancel(); ok=true;
    } else {
      uint32_t token=0,index=0,pin=0;
      if (web_ble_number("token",UINT32_MAX,token)) {
        if (action=="select" && web_ble_number("index",ofe_ble::MAX_DEVICES-1,index))
          ok=!master_update_active && !scheduler.firmwareUpdateActive() && WiFi.status()==WL_CONNECTED &&
             master_display_ble.select((uint8_t)index,token);
        else if (action=="confirm" && web_ble_number("pin",999999,pin) &&
                 (web.arg("accept")=="0" || web.arg("accept")=="1"))
          ok=master_display_ble.approve(token,pin,web.arg("accept")=="1");
      }
    }
    web.send(ok ? 200 : 409,"application/json",ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return;
  }
  const auto state=master_display_ble.view();
  String json; json.reserve(1000);
  json="{\"active\":"; json+=state.active ? "true" : "false";
  json+=",\"phase\":"; json+=(uint8_t)state.phase;
  json+=",\"wifi\":"; json+=WiFi.status()==WL_CONNECTED ? "true" : "false";
  if (web.arg("brief")!="1") {
    json+=",\"free_internal\":"; json+=state.free_internal;
    json+=",\"largest_internal\":"; json+=state.largest_internal;
    json+=",\"memory_stage\":"; json+=state.memory_stage;
    json+=",\"error_phase\":"; json+=state.error_phase;
    json+=",\"error_code\":"; json+=state.error_code;
    json+=",\"searching\":"; json+=ofe_ble::searching(state) ? "true" : "false";
    json+=",\"search_elapsed_ms\":"; json+=ofe_ble::searchElapsedMs(state,millis());
    json+=",\"search_timeout_ms\":"; json+=ofe_ble::SEARCH_TIMEOUT_MS;
    json+=",\"token\":"; json+=state.token;
    json+=",\"pin\":"; json+=state.phase==ofe_ble::CONFIRM ? state.pin : 0;
    json+=",\"devices\":[";
    for (uint8_t i=0;i<state.count;++i) {
      if (i) json+=',';
      char serial[17]; snprintf(serial,sizeof(serial),"%016llX",(unsigned long long)state.devices[i].uid);
      json+="{\"serial\":\""; json+=serial; json+="\",\"kind\":"; json+=state.devices[i].kind;
      json+=",\"rssi\":"; json+=state.devices[i].rssi;
      json+=",\"paired\":"; json+=state.devices[i].paired ? "true" : "false"; json+='}';
    }
    json+=']';
  }
  json+='}'; web.send(200,"application/json",json);
}
static void append_ble_pairing_ui(String& html) {
  html+=F("<section class='panel' id='ble'><h2>Bluetooth</h2><div class='actions'><button type='button' onclick=\"bleAction('start')\">");
  html+=web_text("Displays suchen","Find displays");
  html+=F("</button><button type='button' class='secondary' onclick=\"bleAction('cancel')\">");
  html+=web_text("Abbrechen","Cancel");
  html+=F("</button></div><p id='ble_state' role='status'></p><p id='ble_search_timer' role='timer' style='font-variant-numeric:tabular-nums' hidden></p><div id='ble_devices'></div><div id='ble_confirm' hidden><strong id='ble_pin' style='font-size:28px'></strong><div class='actions' style='margin-top:12px'><button type='button' onclick=\"bleConfirm(true)\">");
  html+=web_text("Code stimmt","Code matches");
  html+=F("</button><button type='button' class='danger' onclick=\"bleConfirm(false)\">");
  html+=web_text("Ablehnen","Reject");
  html+=F("</button></div></div></section><script>");
  html+=web_is_german() ? F("const BLE_DE=true;") : F("const BLE_DE=false;");
  html+=R"JS(
let bleCurrent=null;
const bleMessages=BLE_DE?['Bereit','Bluetooth startet','Displays werden gesucht','Display auswählen','Kopplungsbereit','Verbindung wird aufgebaut','Zahlencode an beiden Geräten vergleichen','Bestätigung am Display abwarten','WLAN und Kopplung werden gespeichert','Display gekoppelt','Kopplung fehlgeschlagen','Abgebrochen','Kopplungszeit abgelaufen','Nicht genügend freier RAM']:['Ready','Starting Bluetooth','Searching for displays','Select a display','Ready to pair','Connecting','Compare the code on both devices','Waiting for display confirmation','Saving WiFi and pairing','Display paired','Pairing failed','Cancelled','Pairing timed out','Insufficient free RAM'];
async function bleAction(action,extra={}){
  try{
    const r=await fetch('/display-link/ble',{method:'POST',body:new URLSearchParams({action,...extra}),cache:'no-store'});
    if(!r.ok)throw Error(BLE_DE?'Aktion nicht möglich. WLAN, Update und Kopplungszustand prüfen.':'Action unavailable. Check WiFi, updates and pairing state.');
    await bleRefresh();
  }catch(e){document.getElementById('ble_state').textContent=e.message}
}
function bleConfirm(accept){
  if(bleCurrent&&bleCurrent.phase===6)bleAction('confirm',{token:String(bleCurrent.token),pin:String(bleCurrent.pin),accept:accept?'1':'0'});
}
async function bleRefresh(){
  if(document.hidden)return;
  try{
    const r=await fetch('/display-link/ble',{cache:'no-store'});if(!r.ok)return;
    const d=await r.json();bleCurrent=d;
    let message=bleMessages[d.phase]||'';
    if(d.phase===10&&d.error_phase){
      const step=bleMessages[d.error_phase]||String(d.error_phase);
      message+=' ('+step+'; BLE '+String(d.error_code||0)+')';
    }
    if(d.phase===13&&Number.isFinite(d.free_internal)&&Number.isFinite(d.largest_internal)){
      const step=BLE_DE?['','Task-Start','vor Bluetooth-Start','nach Bluetooth-Start','nach Bluetooth-Stopp']:['','task startup','before Bluetooth startup','after Bluetooth startup','after Bluetooth shutdown'];
      message+=' ('+(step[d.memory_stage]||'')+': '+Math.floor(d.free_internal/1024)+' KiB '+(BLE_DE?'frei, größter Block ':'free, largest block ')+Math.floor(d.largest_internal/1024)+' KiB)';
    }
    document.getElementById('ble_state').textContent=message;
    const timer=document.getElementById('ble_search_timer');timer.hidden=!d.searching;
    const clock=ms=>{const s=Math.max(0,Math.floor((Number(ms)||0)/1000));return String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0')};
    timer.textContent=(BLE_DE?'Suche läuft: ':'Search running: ')+clock(d.search_elapsed_ms)+' / '+clock(d.search_timeout_ms);
    const box=document.getElementById('ble_devices');box.replaceChildren();
    (d.devices||[]).forEach((item,index)=>{
      const row=document.createElement('div');row.className='actions';row.style.marginTop='12px';
      const text=document.createElement('span');text.textContent=['Display 320x480','Display 800x480','Display ST7796 320x480','Display P4 800x480'][item.kind]+' · '+item.serial+' · '+item.rssi+' dBm';
      const button=document.createElement('button');button.type='button';button.textContent=item.paired?(BLE_DE?'Gekoppelt':'Paired'):(BLE_DE?'Koppeln':'Pair');
      const selectable=d.active?(d.phase===2||d.phase===3):(d.phase===9||d.phase===10);
      button.disabled=!selectable||!!item.paired;button.onclick=()=>bleAction('select',{index:String(index),token:String(d.token)});
      row.append(text,button);box.append(row);
    });
    document.getElementById('ble_confirm').hidden=d.phase!==6;
    document.getElementById('ble_pin').textContent=String(d.pin||0).padStart(6,'0');
  }catch(e){}
}
bleRefresh();setInterval(bleRefresh,1000);
)JS";
  html+=F("</script>");
}
#endif
