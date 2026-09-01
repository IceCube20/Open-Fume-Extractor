#pragma once

// Firmware update pages and handlers. Included from the master sketch so it can
// use the existing static WebServer, Update state and scheduler objects.
static bool firmware_signature_allowed(const String& sig, const char* expected);
static bool module_firmware_signature_allowed(const ModuleRecord& rec, const String& sig);
static bool firmware_image_header_allowed(const uint8_t* data, size_t len);
static void master_update_abort_active();
static bool module_update_abort_active();

static String update_page(const String& msg = String()) {
  String html;
  html.reserve(7600);
  web_shell_begin(html, web_text("Firmware-Updates", "Firmware Updates"), web_text("Wartung", "Maintenance"), "update");
  html += F("<p class='muted'>");
  html += web_text("Master und Module sicher aktualisieren.", "Update the master and modules safely.");
  html += F("</p>");
  if (msg.length()) { html += F("<p class='msg'>"); html += html_escape(msg); html += F("</p>"); }
  html += F("<div class='grid'><section class='panel'><h2>Master OTA</h2><p>"); html += web_text("Firmware des Masters aktualisieren. Der Master startet danach automatisch neu.", "Update the master firmware. The master restarts automatically afterwards."); html += F("</p><form id='masterForm' method='post' action='/update/master' enctype='multipart/form-data'><label>"); html += web_text("Master-Firmware (.bin)", "Master firmware (.bin)"); html += F("</label><input id='master_fw_file' type='file' name='firmware' accept='.bin'><div id='master_fw_info' class='muted' style='margin-top:8px'></div><label class='dev-fw-override' style='display:none;margin-top:10px;padding:10px;border:1px solid #5b4a22;border-radius:8px;background:#211b10;color:#f4c25b'><input type='checkbox' name='unsafe' value='1' style='width:auto;min-height:0;margin-right:8px'><span>"); html += web_text("Entwicklermodus: Signaturprüfung überspringen", "Developer mode: skip signature verification"); html += F("</span></label><button id='master_update_btn' type='submit'>"); html += web_text("Master aktualisieren", "Update Master"); html += F("</button><div class='progress'><div id='masterBar' class='bar'></div></div><div id='masterStat' class='upload-stat'></div></form></section>");
  html += F("<section class='panel'><h2>Module OTA</h2><p>"); html += web_text("Firmware über RS485 oder die WLAN-Verbindung des Displays senden.", "Send firmware over RS485 or the display's WiFi connection."); html += F("</p><form id='moduleForm' method='post' action='/update/module' enctype='multipart/form-data'><label>"); html += web_text("Zielmodul", "Target module"); html += F("</label><select name='addr'>");
  bool update_module_found = false;
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    if (!m.online || !(m.caps & CAP_FW_UPDATE)) continue;
    update_module_found = true;
    String display_name = module_display_name(m);
    html += F("<option value='");
    html += m.addr;
    html += F("' data-type='");
    html += m.type;
    html += F("' data-caps='");
    html += m.caps;
    html += F("' data-transport='");
    html += master_display_wifi.active(m.addr) ? F("wifi") : F("rs485");
    html += F("' data-name='");
    html += html_escape(display_name);
    html += F("'>0x");
    if (m.addr < 0x10) html += '0';
    html += String(m.addr, HEX);
    html += F(" ");
    html += html_escape(display_name);
    html += F(" - FW ");
    html += m.fw_major;
    html += F(".");
    html += m.fw_minor;
    html += F(".");
    html += m.fw_patch;
    if (m.fw_suffix[0]) html += html_escape(String(m.fw_suffix));
    if (m.type == MODULE_DISPLAY) {
      html += F(" - ");
      html += master_display_wifi.active(m.addr) ? web_text("WLAN", "WiFi") : String("RS485");
    }
    html += F("</option>");
  }
  if (!update_module_found) { html += F("<option value=''>"); html += web_text("Kein updatefähiges Modul online", "No update-capable module online"); html += F("</option>"); }
  html += F("</select><label>"); html += web_text("Modul-Firmware (.bin)", "Module firmware (.bin)"); html += F("</label><input id='module_fw_file' type='file' name='firmware' accept='.bin'><div id='module_fw_info' class='muted' style='margin-top:8px'></div><label class='dev-fw-override' style='display:none;margin-top:10px;padding:10px;border:1px solid #5b4a22;border-radius:8px;background:#211b10;color:#f4c25b'><input type='checkbox' name='unsafe' value='1' style='width:auto;min-height:0;margin-right:8px'><span>"); html += web_text("Entwicklermodus: Modultyp-Prüfung überspringen", "Developer mode: skip module type check"); html += F("</span></label><button id='module_update_btn' type='submit' style='margin-top:16px'>"); html += web_text("Modul aktualisieren", "Update Module"); html += F("</button><div class='progress'><div id='moduleBar' class='bar'></div></div><div id='moduleStat' class='upload-stat'></div></form></section></div>");
  html += F("<script>");
  html += web_is_german() ? F("const UI_DE=true;") : F("const UI_DE=false;");
  html += developer_mode_enabled ? F("let DEV_MODE=true;") : F("let DEV_MODE=false;");
  html += F("localStorage.setItem('ofe_dev_mode',DEV_MODE?'1':'0');function applyUpdateDevMode(){document.querySelectorAll('.dev-fw-override').forEach(function(e){e.style.display=DEV_MODE?'block':'none';});}applyUpdateDevMode();fetch('/state',{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){if(typeof d.developer_mode==='boolean'){DEV_MODE=!!d.developer_mode;localStorage.setItem('ofe_dev_mode',DEV_MODE?'1':'0');applyUpdateDevMode();}}).catch(function(){});const RS485_FW_CHUNK=" OFE_STR(MODULE_FW_CHUNK_SIZE) ";const RS485_FW_CHUNK_DISPLAY=" OFE_STR(MODULE_FW_DISPLAY_CHUNK_SIZE) ";const MODULE_FW_HTTP_CHUNK=" OFE_STR(MODULE_FW_HTTP_CHUNK_SIZE) ";const MASTER_FW_CHUNK=" OFE_STR(MASTER_FW_WEB_CHUNK_SIZE) ";");
  html += F("function u(de,en){return UI_DE?de:en;}function fmt(n){if(!n)return'0 B';var uu=['B','KB','MB'];var i=0;while(n>=1024&&i<uu.length-1){n/=1024;i++;}return n.toFixed(i?1:0)+' '+uu[i];}");
  html += F("function fmtSpeed(bps){if(!bps||bps<1)return'-';return (bps/1024).toFixed(1)+' kB/s';}function fmtEta(sec){if(!isFinite(sec)||sec<0)return'-';sec=Math.ceil(sec);var m=Math.floor(sec/60),s=sec%60;return m?m+'m '+String(s).padStart(2,'0')+'s':s+'s';}");
  html += F("function send(form,bar,stat,url,doneText){form.addEventListener('submit',function(e){e.preventDefault();var file=form.querySelector('input[type=file]').files[0];if(!file){stat.textContent=u('Keine Firmwaredatei ausgewählt','No firmware file selected');return;}var xhr=new XMLHttpRequest();bar.style.width='0%';stat.textContent=u('Update wird gestartet...','Starting...');xhr.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.round(ev.loaded*100/ev.total);bar.style.width=p+'%';stat.textContent=p+'% ('+fmt(ev.loaded)+' / '+fmt(ev.total)+')';}else{stat.textContent=u('Upload läuft...','Uploading...');}};xhr.onload=function(){if(xhr.status>=200&&xhr.status<300){bar.style.width='100%';stat.textContent=(xhr.responseText||doneText)+' '+u('Weiterleitung in wenigen Sekunden...','Redirecting in a few seconds...');setTimeout(function(){location.href='/';},9000);setTimeout(function(){location.reload();},15000);}else{stat.textContent=xhr.responseText||('Failed: HTTP '+xhr.status);}};xhr.onerror=function(){stat.textContent=u('Upload-Verbindung fehlgeschlagen','Upload connection failed');};xhr.open('POST',url(file));xhr.send(new FormData(form));});}");
  html += F("function postBlob(url,blob){return new Promise(function(ok,fail){var x=new XMLHttpRequest();var fd=new FormData();fd.append('firmware',blob,'chunk.bin');x.onload=function(){if(x.status>=200&&x.status<300)ok(x.responseText);else fail(x.responseText||('HTTP '+x.status));};x.onerror=function(){fail(u('Verbindung fehlgeschlagen','Connection failed'));};x.open('POST',url);x.send(fd);});}");
  html += F("function sleep(ms){return new Promise(function(ok){setTimeout(ok,ms);});}");
  html += F("async function waitMasterBack(stat){await sleep(2500);for(var i=0;i<35;i++){try{var r=await fetch('/state',{cache:'no-store'});if(r.ok){location.href='/';return;}}catch(e){}stat.textContent=u('Master startet neu...','Master rebooting...')+' '+(i+1);await sleep(2000);}location.href='/';}");
  html += F("function fwNameOk(type,file){var n=file.name.toLowerCase();var fan=n.indexOf('fanio')>=0||n.indexOf('fan_io')>=0||n.indexOf('fan-io')>=0;var pro=n.indexOf('pro')>=0;if(type==1)return n.indexOf('jbc')>=0&&n.indexOf('usb')<0;if(type==9)return n.indexOf('jbc')>=0&&n.indexOf('usb')>=0;if(type==2)return fan&&!pro;if(type==3)return (fan&&pro)||n.indexOf('faniopro')>=0||n.indexOf('relay')>=0;if(type==5)return n.indexOf('weller')>=0||(n.indexOf('zero')>=0&&n.indexOf('smog')>=0);if(type==6)return n.indexOf('display')>=0;if(type==7)return n.indexOf('universal')>=0||n.indexOf('rs232')>=0||n.indexOf('uart')>=0||n.indexOf('bridge')>=0;if(type==8)return n.indexOf('modbus')>=0||n.indexOf('rtu')>=0||n.indexOf('bridge')>=0;if(type==4)return n.indexOf('sensor')>=0;return false;}function fwChunkForType(type){return Number(type)==6?RS485_FW_CHUNK_DISPLAY:RS485_FW_CHUNK;}");
  html += F("function fwExpectedSig(type,caps){caps=Number(caps||0);if(type==1)return 'OFE_FW_SIG:v1;target=JBC_BUS;';if(type==9)return 'OFE_FW_SIG:v1;target=JBC_USB;';if(type==2)return 'OFE_FW_SIG:v1;target=FAN_IO;';if(type==3)return 'OFE_FW_SIG:v1;target=FAN_IO_PRO;';if(type==5)return 'OFE_FW_SIG:v1;target=WELLER_ZERO_SMOG;';if(type==6){if(caps&8388608)return 'OFE_FW_SIG:v1;target=DISPLAY_800X480;';if(caps&4194304)return 'OFE_FW_SIG:v1;target=DISPLAY_320X480;';return 'OFE_FW_SIG:v1;target=DISPLAY;';}if(type==7)return 'OFE_FW_SIG:v1;target=UNIVERSAL_RS232;';if(type==8)return 'OFE_FW_SIG:v1;target=MODBUS_RTU;';if(type==4)return 'OFE_FW_SIG:v1;target=SENSOR;';return '';}");
  html += F("async function findFwSig(file,want){var b=new Uint8Array(await file.arrayBuffer());var p='OFE_FW_SIG:v1;',fallback='';for(var i=0;i<=b.length-p.length;i++){var ok=true;for(var j=0;j<p.length;j++){if(b[i+j]!==p.charCodeAt(j)){ok=false;break;}}if(ok){var s='';for(var k=i;k<b.length&&k<i+180;k++){var c=b[k];if(c<32||c>126)break;s+=String.fromCharCode(c);}var m=s.match(/^OFE_FW_SIG:v1;target=[A-Z0-9_]+;(version=[^;]+;)?/);var sig=m?m[0]:s;if(!want||sig.indexOf(want)>=0){if(sig.indexOf(';version=')>=0)return sig;if(!fallback)fallback=sig;}}}return fallback;}");
  html += F("async function findFwAuth(file){var b=new Uint8Array(await file.arrayBuffer()),p='OFE_FW_AUTH:v1;';for(var i=b.length-p.length;i>=0;i--){var ok=true;for(var j=0;j<p.length;j++){if(b[i+j]!==p.charCodeAt(j)){ok=false;break;}}if(!ok)continue;var s='';for(var k=i;k<b.length&&k<i+420;k++){var c=b[k];if(c<32||c>126)break;s+=String.fromCharCode(c);}var m=s.match(/^OFE_FW_AUTH:v1;target=([A-Z0-9_]+);version=([^;]+);size=([0-9]+);sha256=([0-9a-f]{64});keyid=([0-9a-f]{16});sig=([0-9a-f]{128});$/);if(m&&Number(m[3])===i&&i+s.length===b.length)return{text:s,target:m[1],version:m[2],size:Number(m[3]),sha256:m[4],keyid:m[5]};}return null;}");
  html += F("function fwTargetOk(type,target,caps,isMaster){if(isMaster)return target==='MASTER';if(Number(type)==6){caps=Number(caps||0);if(caps&8388608)return target==='DISPLAY_800X480';if(caps&4194304)return target==='DISPLAY_320X480';return target==='DISPLAY'||target==='DISPLAY_320X480'||target==='DISPLAY_800X480';}var expected=fwExpectedSig(type,caps),m=expected.match(/target=([A-Z0-9_]+)/);return !!m&&target===m[1];}");
  html += F("function fwSigVersion(sig){var m=String(sig||'').match(/(?:^|;)version=([^;]+)/);return m?m[1]:'-';}function fwSigTarget(sig){var m=String(sig||'').match(/(?:^|;)target=([^;]+)/);return m?m[1]:'-';}function fwSigTargetLabel(sig){var t=fwSigTarget(sig);var map={MASTER:'Master',JBC_BUS:'JBC Bus',FAN_IO:'Fan/IO',FAN_IO_PRO:'Fan/IO Pro',WELLER_ZERO_SMOG:'Weller Zero Smog',DISPLAY:'Display',DISPLAY_320X480:'Display 320x480',DISPLAY_800X480:'Display 800x480',UNIVERSAL_RS232:'Universal RS232',MODBUS_RTU:'Modbus RTU',SENSOR:'Sensor'};return map[t]||t.replace(/_/g,' ');}");
  html += F("function fwSigComplete(sig){return !!sig&&sig.indexOf(';version=')>=0;}function fwSigOk(type,sig,caps){var e=fwExpectedSig(type,caps);if(!e||!sig||!fwSigComplete(sig))return false;if(Number(type)==6){var c=Number(caps||0),is800=!!(c&8388608),is320=!!(c&4194304);if(is800)return sig.indexOf('OFE_FW_SIG:v1;target=DISPLAY_800X480;')>=0;if(is320)return sig.indexOf('OFE_FW_SIG:v1;target=DISPLAY_320X480;')>=0;return sig.indexOf('OFE_FW_SIG:v1;target=DISPLAY;')>=0||sig.indexOf('OFE_FW_SIG:v1;target=DISPLAY_320X480;')>=0||sig.indexOf('OFE_FW_SIG:v1;target=DISPLAY_800X480;')>=0;}return sig.indexOf(e)>=0;}");
  html += F("async function getState(){var r=await fetch('/state',{cache:'no-store'});return await r.json();}");
  html += F("function moduleStatsFmt(s){if(!DEV_MODE||!s)return '';return '  -  Dev Queue '+s.queue_count+'/'+s.queue_size+' low '+s.queue_low+' empty '+s.empty_polls+' starve '+s.starve_count+'/'+s.starve_max_ms+' ms http '+s.http_age_ms+'/'+s.http_max_gap_ms+' ms ack '+s.ack_last_ms+'/'+s.ack_max_ms+' ms pump '+s.pump_gap_last_ms+'/'+s.pump_gap_max_ms+' ms retry '+s.retry_total+'/'+s.retry_last+' frames '+s.frames;}function moduleChunkStatsText(t){if(!DEV_MODE||!t)return '';try{return moduleStatsFmt(JSON.parse(t));}catch(e){return '';}}async function moduleStatsText(){if(!DEV_MODE)return '';try{var r=await fetch('/update/module/stats',{cache:'no-store'});if(!r.ok)return '';return moduleStatsFmt(await r.json());}catch(e){return '';}}");
  html += F("function updateChunkText(name,size,extra){return DEV_MODE?'  -  '+name+': '+size+' B'+(extra||''):'';}function updateStartText(base,name,size){return DEV_MODE?base+' '+name+': '+size+' B':base;}function updateProgressText(p,label,offset,total,bps,eta,name,size,extra){return p+'% '+label+' ('+fmt(offset)+' / '+fmt(total)+')  -  '+fmtSpeed(bps)+'  -  '+u('Restzeit','ETA')+': '+fmtEta(eta)+updateChunkText(name,size,extra);}function updateAverageText(base,avg,name,size,extra){return base+': '+fmtSpeed(avg)+updateChunkText(name,size,extra);}function updateFailText(base,e,name,size,extra){return base+e+updateChunkText(name,size,extra);}");
  html += F("async function waitOnline(addr,type,minSeen,stat){for(var i=0;i<20;i++){stat.textContent=u('Update geschrieben. Warte auf Modul 0x','Update written. Waiting for module 0x')+Number(addr).toString(16).toUpperCase()+'... '+(i+1)+'s';try{await fetch('/scan?addr='+addr,{cache:'no-store'});var d=await getState();for(var j=0;j<d.modules.length;j++){var m=d.modules[j];if(Number(m.addr)==Number(addr)&&m.online&&(Number(type)<0||Number(m.type)==Number(type))&&Number(m.last_seen_ms)>=minSeen){return m;}}}catch(e){}await sleep(1000);}return null;}");
  html += F("async function showFwInfo(file,el,type,isMaster,caps){if(!el)return;if(!file){el.textContent='';return;}el.textContent=u('Firmware wird geprüft...','Checking firmware...');var want=isMaster?'OFE_FW_SIG:v1;target=MASTER;':fwExpectedSig(type,caps),sig=await findFwSig(file,(Number(type)==6&&!isMaster)?'OFE_FW_SIG:v1;':want),auth=await findFwAuth(file),legacyOk=isMaster?(sig.indexOf(want)>=0&&fwSigComplete(sig)):fwSigOk(type,sig,caps),authOk=!!auth&&fwTargetOk(type,auth.target,caps,isMaster)&&auth.version===fwSigVersion(sig);if(legacyOk&&authOk){el.textContent=u('Firmware erkannt: ','Firmware detected: ')+auth.version+' '+fwSigTargetLabel(sig)+' · Ed25519 V1';el.style.color='#9fb6cc';}else if(legacyOk){el.textContent=u('Firmware erkannt, aber nicht kryptografisch signiert','Firmware detected but not cryptographically signed')+': '+fwSigVersion(sig)+' '+fwSigTargetLabel(sig);el.style.color='#ffb86b';}else{el.textContent=u('Keine passende OFE-Firmware-Signatur gefunden','No matching OFE firmware signature found');el.style.color='#ffb86b';}}");
  html += F("function selectedModuleOption(){var f=document.getElementById('moduleForm');if(!f||!f.addr||f.addr.selectedIndex<0)return null;return f.addr.options[f.addr.selectedIndex];}function selectedModuleType(){var sel=selectedModuleOption();return sel?Number(sel.dataset.type||0):0;}function selectedModuleCaps(){var sel=selectedModuleOption();return sel?Number(sel.dataset.caps||0):0;}");
  html += F("var moduleUpdateRun=null;function setModuleUpdateButton(run){var b=document.getElementById('module_update_btn');if(!b)return;b.disabled=!!(run&&run.cancelling)||!!(run&&run.finished);b.textContent=!run?u('Modul aktualisieren','Update Module'):run.cancelling?u('Abbruch läuft...','Cancelling...'):run.finished?u('Modul startet neu...','Module rebooting...'):u('Update abbrechen','Cancel update');b.style.background=run&&!run.finished?'#9b2c2c':'';}");
  html += F("function cancelModuleUpdate(stat){var run=moduleUpdateRun;if(!run||run.cancelling||run.finished)return run&&run.cancelPromise;run.cancelled=true;run.cancelling=true;setModuleUpdateButton(run);stat.textContent=u('Modul-Update wird abgebrochen...','Cancelling module update...');run.cancelPromise=(async function(){try{var r=await fetch('/update/module/abort?addr='+encodeURIComponent(run.addr),{method:'POST'}),t=await r.text();if(!r.ok)throw(t||('HTTP '+r.status));stat.textContent=u('Modul-Update abgebrochen. Das Modul ist wieder bereit.','Module update cancelled. The module is ready again.');}catch(e){stat.textContent=u('Abbruch konnte vom Modul nicht bestätigt werden: ','Module did not confirm cancellation: ')+e;}finally{if(moduleUpdateRun===run){moduleUpdateRun=null;setModuleUpdateButton(null);}}})();return run.cancelPromise;}");
  html += F("async function sendModule(form,bar,stat){var file=form.querySelector('input[type=file]').files[0];if(!file){stat.textContent=u('Keine Firmwaredatei ausgewählt','No firmware file selected');return;}var sel=form.addr.options[form.addr.selectedIndex],addr=form.addr.value;if(!addr){stat.textContent=u('Kein updatefähiges Modul online','No update-capable module online');return;}var transport=sel.dataset.transport==='wifi'?u('WLAN','WiFi'):'RS485',chunkLabel=transport+u('-Chunk',' chunk');var type=Number(sel.dataset.type||0),caps=Number(sel.dataset.caps||0),unsafe=!!(form.querySelector('input[name=unsafe]')&&form.querySelector('input[name=unsafe]').checked),auth=await findFwAuth(file),payloadSize=auth?auth.size:file.size,want=fwExpectedSig(type,caps),sig=await findFwSig(file,Number(type)==6?'OFE_FW_SIG:v1;':want);if(!unsafe&&!fwNameOk(type,file)){stat.textContent=u('Blockiert: Der Dateiname passt nicht zum gewählten Modultyp.','Blocked: file name does not match the selected module type.');return;}if(!unsafe&&(!fwSigOk(type,sig,caps)||!auth||!fwTargetOk(type,auth.target,caps,false)||auth.version!==fwSigVersion(sig))){stat.textContent=u('Blockiert: Kryptografische Firmware-Signatur fehlt oder passt nicht zum Zielmodul.','Blocked: cryptographic firmware signature is missing or does not match the target module.');return;}if(moduleUpdateRun)return;var run={addr:addr,cancelled:false,cancelling:false,finished:false,cancelPromise:null};moduleUpdateRun=run;setModuleUpdateButton(run);var fwChunk=fwChunkForType(type),chunk=MODULE_FW_HTTP_CHUNK,offset=0,lastOffset=0,t0=performance.now(),lastT=t0,ema=0;bar.style.width='0%';stat.textContent=updateStartText(transport+u('-Update wird gestartet...',' update starting...'),chunkLabel,fwChunk);try{var s=await getState();if(run.cancelled)throw u('Update abgebrochen','Update cancelled');var live=(s.modules||[]).find(function(m){return Number(m.addr)===Number(addr);});if(live){transport=live.transport==='wifi'?u('WLAN','WiFi'):'RS485';chunkLabel=transport+u('-Chunk',' chunk');}var minSeen=s.uptime_ms,r=await fetch('/update/module/begin?addr='+addr+'&size='+payloadSize+'&name='+encodeURIComponent(file.name)+'&unsafe='+(unsafe?'1':'0')+'&sig='+encodeURIComponent(sig)+'&auth='+encodeURIComponent(auth?auth.text:''),{method:'POST'});if(!r.ok)throw await r.text();if(run.cancelled)throw u('Update abgebrochen','Update cancelled');while(offset<payloadSize){var end=Math.min(offset+chunk,payloadSize),chunkReply=await postBlob('/update/module/chunk?offset='+offset,file.slice(offset,end));if(run.cancelled)throw u('Update abgebrochen','Update cancelled');offset=end;var now=performance.now(),dt=(now-lastT)/1000;if(dt>0){var inst=(offset-lastOffset)/dt;ema=ema?(ema*0.75+inst*0.25):inst;}lastT=now;lastOffset=offset;var p=Math.round(offset*100/payloadSize),eta=ema?((payloadSize-offset)/ema):Infinity;bar.style.width=p+'%';stat.textContent=updateProgressText(p,transport,offset,payloadSize,ema,eta,chunkLabel,fwChunk,moduleChunkStatsText(chunkReply));}r=await fetch('/update/module/end',{method:'POST'});var t=await r.text();if(!r.ok)throw t;if(run.cancelled)throw u('Update abgebrochen','Update cancelled');run.finished=true;setModuleUpdateButton(run);bar.style.width='100%';var avg=payloadSize/((performance.now()-t0)/1000);stat.textContent=updateAverageText(u('Update geschrieben. Durchschnitt','Update written. Average'),avg,chunkLabel,fwChunk,await moduleStatsText());var m=await waitOnline(addr,unsafe?-1:type,minSeen,stat);stat.textContent=m?(u('Modul online: 0x','Module online: 0x')+Number(addr).toString(16).toUpperCase()+' '+(m.name||'')+' FW '+m.fw+'  -  '+fmtSpeed(avg)+updateChunkText(chunkLabel,fwChunk,await moduleStatsText())):u('Update geschrieben, aber das Modul kam nicht mit gleicher Adresse und gleichem Typ zurück.','Update written, but the module did not return with the same address and type.');}catch(e){if(run.cancelled){if(run.cancelPromise)await run.cancelPromise;return;}try{await fetch('/update/module/abort?addr='+addr,{method:'POST'});}catch(_e){}stat.textContent=updateFailText(u('Modul-Update fehlgeschlagen: ','Module update failed: '),e,chunkLabel,fwChunk,await moduleStatsText());}finally{if(!run.cancelling&&moduleUpdateRun===run){moduleUpdateRun=null;setModuleUpdateButton(null);}}}");
  html += F("var masterUpdateRun=null;function setMasterUpdateButton(run){var b=document.getElementById('master_update_btn');if(!b)return;b.disabled=!!(run&&run.cancelling)||!!(run&&run.finished);b.textContent=!run?u('Master aktualisieren','Update Master'):run.cancelling?u('Abbruch läuft...','Cancelling...'):run.finished?u('Master startet neu...','Master rebooting...'):u('Update abbrechen','Cancel update');b.style.background=run&&!run.finished?'#9b2c2c':'';}");
  html += F("function cancelMasterUpdate(stat){var run=masterUpdateRun;if(!run||run.cancelling||run.finished)return run&&run.cancelPromise;run.cancelled=true;run.cancelling=true;setMasterUpdateButton(run);stat.textContent=u('Master-Update wird abgebrochen...','Cancelling master update...');run.cancelPromise=(async function(){try{var r=await fetch('/update/master/abort',{method:'POST'}),t=await r.text();if(!r.ok)throw(t||('HTTP '+r.status));stat.textContent=u('Master-Update abgebrochen. Der Master läuft normal weiter.','Master update cancelled. The master continues running normally.');}catch(e){stat.textContent=u('Master-Update konnte nicht sauber abgebrochen werden: ','Master update could not be cancelled cleanly: ')+e;}finally{if(masterUpdateRun===run){masterUpdateRun=null;setMasterUpdateButton(null);}}})();return run.cancelPromise;}");
  html += F("async function sendMaster(form,bar,stat){var file=form.querySelector('input[type=file]').files[0];if(!file){stat.textContent=u('Keine Firmwaredatei ausgewählt','No firmware file selected');return;}var unsafe=!!(form.querySelector('input[name=unsafe]')&&form.querySelector('input[name=unsafe]').checked),want='OFE_FW_SIG:v1;target=MASTER;',sig=await findFwSig(file,want),auth=await findFwAuth(file),payloadSize=auth?auth.size:file.size;if(!unsafe&&(sig.indexOf(want)<0||!fwSigComplete(sig)||!auth||auth.target!=='MASTER'||auth.version!==fwSigVersion(sig))){stat.textContent=u('Blockiert: Kryptografische Firmware-Signatur fehlt oder passt nicht zum Master.','Blocked: cryptographic firmware signature is missing or does not match the master.');return;}if(masterUpdateRun)return;var run={cancelled:false,cancelling:false,finished:false,cancelPromise:null};masterUpdateRun=run;setMasterUpdateButton(run);var chunk=MASTER_FW_CHUNK,offset=0,lastOffset=0,t0=performance.now(),lastT=t0,ema=0;bar.style.width='0%';stat.textContent=updateStartText(u('Master-Update wird gestartet...','Starting master update...'),u('Master-Chunk','Master chunk'),MASTER_FW_CHUNK);try{var r=await fetch('/update/master/begin?size='+payloadSize+'&unsafe='+(unsafe?'1':'0')+'&sig='+encodeURIComponent(sig)+'&auth='+encodeURIComponent(auth?auth.text:''),{method:'POST'});if(!r.ok)throw await r.text();if(run.cancelled)throw u('Update abgebrochen','Update cancelled');while(offset<payloadSize){var end=Math.min(offset+chunk,payloadSize);await postBlob('/update/master/chunk?offset='+offset,file.slice(offset,end));if(run.cancelled)throw u('Update abgebrochen','Update cancelled');offset=end;var now=performance.now(),dt=(now-lastT)/1000;if(dt>0){var inst=(offset-lastOffset)/dt;ema=ema?(ema*0.75+inst*0.25):inst;}lastT=now;lastOffset=offset;var p=Math.round(offset*100/payloadSize);if(p>99&&offset<payloadSize)p=99;var eta=ema?((payloadSize-offset)/ema):Infinity;bar.style.width=p+'%';stat.textContent=updateProgressText(p,'Master',offset,payloadSize,ema,eta,u('Master-Chunk','Master chunk'),MASTER_FW_CHUNK,'');}r=await fetch('/update/master/end',{method:'POST'});var t=await r.text();if(!r.ok)throw t;if(run.cancelled)throw u('Update abgebrochen','Update cancelled');run.finished=true;setMasterUpdateButton(run);bar.style.width='100%';var avg=payloadSize/((performance.now()-t0)/1000);stat.textContent=updateAverageText((t||u('Master-Update abgeschlossen. Neustart...','Master update complete. Rebooting...'))+'  -  '+u('Durchschnitt','Average'),avg,u('Master-Chunk','Master chunk'),MASTER_FW_CHUNK,'');waitMasterBack(stat);}catch(e){if(run.cancelled){if(run.cancelPromise)await run.cancelPromise;return;}try{await fetch('/update/master/abort',{method:'POST'});}catch(_e){}stat.textContent=updateFailText(u('Master-Update fehlgeschlagen: ','Master update failed: '),e,u('Master-Chunk','Master chunk'),MASTER_FW_CHUNK,'');}finally{if(!run.cancelling&&!run.finished&&masterUpdateRun===run){masterUpdateRun=null;setMasterUpdateButton(null);}}}");
  html += F("var mf=document.getElementById('master_fw_file'),mi=document.getElementById('master_fw_info'),uf=document.getElementById('module_fw_file'),ui=document.getElementById('module_fw_info'),ms=document.querySelector('#moduleForm select[name=addr]');if(mf)mf.addEventListener('change',function(){showFwInfo(this.files&&this.files[0],mi,0,true,0);});if(uf)uf.addEventListener('change',function(){showFwInfo(this.files&&this.files[0],ui,selectedModuleType(),false,selectedModuleCaps());});if(ms)ms.addEventListener('change',function(){if(uf)showFwInfo(uf.files&&uf.files[0],ui,selectedModuleType(),false,selectedModuleCaps());});");
  html += F("document.getElementById('masterForm').addEventListener('submit',function(e){e.preventDefault();var stat=document.getElementById('masterStat');if(masterUpdateRun){cancelMasterUpdate(stat);return;}sendMaster(this,document.getElementById('masterBar'),stat);});");
  html += F("document.getElementById('moduleForm').addEventListener('submit',function(e){e.preventDefault();var stat=document.getElementById('moduleStat');if(moduleUpdateRun){cancelModuleUpdate(stat);return;}sendModule(this,document.getElementById('moduleBar'),stat);});");
  html += F("</script>");
  web_shell_end(html);
  return html;
}

static void web_handle_update() {
  web.send(200, "text/html; charset=utf-8", update_page());
}

static String module_update_stats_json() {
  size_t queue_count = 0;
  size_t queue_low = 0;
  uint32_t empty_polls = 0;
  uint32_t frames_sent = 0;
  uint32_t http_chunks = 0;
  uint32_t last_http_ms = 0;
  uint32_t max_http_gap_ms = 0;
  uint32_t last_ack_ms = 0;
  uint32_t max_ack_ms = 0;
  uint32_t queued_offset = 0;
  uint32_t starve_count = 0;
  uint32_t starve_since_ms = 0;
  uint32_t starve_max_ms = 0;
  uint32_t pump_gap_last_ms = 0;
  uint32_t pump_gap_max_ms = 0;
  uint32_t retry_total = scheduler.moduleFwChunkRetryCount();
  uint8_t retry_last = scheduler.lastModuleFwChunkAttempts();

  portENTER_CRITICAL(&module_update_queue_mux);
  queue_count = module_update_queue_count;
  queue_low = module_update_queue_low_water;
  empty_polls = module_update_queue_empty_polls;
  frames_sent = module_update_frames_sent;
  http_chunks = module_update_http_chunks;
  last_http_ms = module_update_last_http_ms;
  max_http_gap_ms = module_update_max_http_gap_ms;
  last_ack_ms = module_update_last_ack_ms;
  max_ack_ms = module_update_max_ack_ms;
  queued_offset = module_update_queued_offset;
  starve_count = module_update_starve_count;
  starve_since_ms = module_update_starve_since_ms;
  starve_max_ms = module_update_starve_max_ms;
  pump_gap_last_ms = module_update_last_pump_gap_ms;
  pump_gap_max_ms = module_update_max_pump_gap_ms;
  portEXIT_CRITICAL(&module_update_queue_mux);

  const uint32_t now = millis();
  const uint32_t http_age_ms = last_http_ms ? (uint32_t)(now - last_http_ms) : 0;
  const uint32_t starve_age_ms = starve_since_ms ? (uint32_t)(now - starve_since_ms) : 0;
  if (starve_age_ms > starve_max_ms) starve_max_ms = starve_age_ms;

  String json;
  json.reserve(480);
  json += F("{\"active\":");
  json += module_update_addr ? F("true") : F("false");
  json += F(",\"addr\":");
  json += (uint32_t)module_update_addr;
  json += F(",\"sent\":");
  json += module_update_offset;
  json += F(",\"queued\":");
  json += queued_offset;
  json += F(",\"size\":");
  json += module_update_size;
  json += F(",\"progress\":");
  json += (uint32_t)module_update_progress;
  json += F(",\"queue_count\":");
  json += (uint32_t)queue_count;
  json += F(",\"queue_size\":");
  json += (uint32_t)MODULE_FW_QUEUE_SIZE;
  json += F(",\"queue_low\":");
  json += (uint32_t)queue_low;
  json += F(",\"empty_polls\":");
  json += empty_polls;
  json += F(",\"frames\":");
  json += frames_sent;
  json += F(",\"http_chunks\":");
  json += http_chunks;
  json += F(",\"http_age_ms\":");
  json += http_age_ms;
  json += F(",\"http_max_gap_ms\":");
  json += max_http_gap_ms;
  json += F(",\"ack_last_ms\":");
  json += last_ack_ms;
  json += F(",\"ack_max_ms\":");
  json += max_ack_ms;
  json += F(",\"starve_count\":");
  json += starve_count;
  json += F(",\"starve_age_ms\":");
  json += starve_age_ms;
  json += F(",\"starve_max_ms\":");
  json += starve_max_ms;
  json += F(",\"pump_gap_last_ms\":");
  json += pump_gap_last_ms;
  json += F(",\"pump_gap_max_ms\":");
  json += pump_gap_max_ms;
  json += F(",\"retry_total\":");
  json += retry_total;
  json += F(",\"retry_last\":");
  json += (uint32_t)retry_last;
  json += F("}");
  return json;
}

static void web_handle_module_update_stats() {
  // Update timing diagnostics are intentionally developer-only. Keep normal
  // mode responses small and do not expose internal timing/queue telemetry.
  if (!developer_mode_enabled) {
    web.send(404, "text/plain; charset=utf-8", "Not found");
    return;
  }
  web.send(200, "application/json; charset=utf-8", module_update_stats_json());
}

static bool firmware_bytes_contain(const uint8_t* data, size_t len, const char* needle) {
  if (!data || !needle) return false;
  const size_t nlen = strlen(needle);
  if (!nlen || len < nlen) return false;
  for (size_t i = 0; i + nlen <= len; ++i) {
    if (memcmp(data + i, needle, nlen) == 0) return true;
  }
  return false;
}

static void firmware_signature_probe_reset(
    bool& verified,
    uint32_t& probe_bytes,
    uint8_t* overlap,
    uint8_t& overlap_len) {
  verified = false;
  probe_bytes = 0;
  overlap_len = 0;
  if (overlap) memset(overlap, 0, 96);
}

// Feeds real BIN bytes into a small streaming substring probe. The overlap
// catches signatures split across HTTP upload boundaries.
static void firmware_signature_probe_feed(
    const uint8_t* data,
    size_t len,
    const char* expected,
    bool& verified,
    uint32_t& probe_bytes,
    uint8_t* overlap,
    uint8_t& overlap_len) {
  if (!data || !len || !expected || !expected[0]) return;

  const size_t expected_len = strlen(expected);
  if (!verified) {
    if (firmware_bytes_contain(data, len, expected)) {
      verified = true;
    } else if (overlap_len) {
      uint8_t bridge[192];
      const size_t head_len = len < sizeof(bridge) - overlap_len
        ? len : sizeof(bridge) - overlap_len;
      memcpy(bridge, overlap, overlap_len);
      memcpy(bridge + overlap_len, data, head_len);
      if (firmware_bytes_contain(bridge, overlap_len + head_len, expected)) {
        verified = true;
      }
    }
  }

  if (probe_bytes <= UINT32_MAX - len) probe_bytes += (uint32_t)len;
  else probe_bytes = UINT32_MAX;

  // Keep enough tail to bridge any expected signature across the next chunk.
  size_t keep = expected_len > 1 ? expected_len - 1 : 1;
  if (keep > 95) keep = 95;

  if (len >= keep) {
    memcpy(overlap, data + len - keep, keep);
    overlap_len = (uint8_t)keep;
  } else {
    uint8_t tmp[96];
    size_t combined = 0;
    const size_t old_keep = overlap_len < keep ? overlap_len : keep;
    if (old_keep) {
      memcpy(tmp, overlap + overlap_len - old_keep, old_keep);
      combined = old_keep;
    }
    const size_t copy_len = len < sizeof(tmp) - combined ? len : sizeof(tmp) - combined;
    memcpy(tmp + combined, data, copy_len);
    combined += copy_len;
    if (combined > keep) {
      memmove(tmp, tmp + combined - keep, keep);
      combined = keep;
    }
    memcpy(overlap, tmp, combined);
    overlap_len = (uint8_t)combined;
  }
}

static void master_update_signature_reset() {
  master_update_header_checked = false;
  master_update_auth.reset();
  firmware_signature_probe_reset(
    master_update_payload_signature_verified,
    master_update_signature_probe_bytes,
    master_update_signature_overlap,
    master_update_signature_overlap_len);
}

static void module_update_signature_reset() {
  module_update_auth.reset();
  firmware_signature_probe_reset(
    module_update_payload_signature_verified,
    module_update_signature_probe_bytes,
    module_update_signature_overlap,
    module_update_signature_overlap_len);
  module_update_expected_signature[0] = 0;
}

static void master_firmware_signature_marker(char* out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = 0;

  // Keep the complete marker out of the updater's own read-only string table.
  // Otherwise a firmware image could satisfy the scan with the validator/UI
  // text itself instead of the deliberately embedded build signature.
  static const char part_sig[] = "OFE_FW_SIG:v1;";
  static const char part_target[] = "target=";
  static const char part_master[] = "MASTER";
  static const char part_version[] = ";version=";
  strlcpy(out, part_sig, cap);
  strlcat(out, part_target, cap);
  strlcat(out, part_master, cap);
  strlcat(out, part_version, cap);
}

static bool master_update_check_real_bytes(const uint8_t* data, size_t len) {
  if (!master_update_header_checked) {
    master_update_header_checked = true;
    if (!firmware_image_header_allowed(data, len)) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Firmware image header invalid");
      master_update_abort_active();
      return false;
    }
  }

  char expected_marker[48];
  master_firmware_signature_marker(expected_marker, sizeof(expected_marker));
  firmware_signature_probe_feed(
    data, len, expected_marker,
    master_update_payload_signature_verified,
    master_update_signature_probe_bytes,
    master_update_signature_overlap,
    master_update_signature_overlap_len);

  if (!master_update_unsafe_fw_type && !master_update_auth.update(data, len)) {
    snprintf(update_status_msg, sizeof(update_status_msg), "%s", master_update_auth.error());
    master_update_abort_active();
    return false;
  }

  // Do not reject a valid Master image at an arbitrary prefix boundary. The
  // embedded signature may move when the linker layout changes. The staged OTA
  // partition is only committed in /update/master/end after the full streamed
  // image has been checked.
  return true;
}

static bool module_update_check_real_signature(const uint8_t* data, size_t len) {
  if (module_update_unsafe_fw_type) return true;
  if (!module_update_expected_signature[0]) {
    snprintf(update_status_msg, sizeof(update_status_msg),
      "No expected module firmware signature");
    module_update_abort_active();
    return false;
  }

  firmware_signature_probe_feed(
    data, len, module_update_expected_signature,
    module_update_payload_signature_verified,
    module_update_signature_probe_bytes,
    module_update_signature_overlap,
    module_update_signature_overlap_len);

  if (!module_update_auth.update(data, len)) {
    snprintf(update_status_msg, sizeof(update_status_msg), "%s", module_update_auth.error());
    module_update_abort_active();
    return false;
  }

  // Do not abort module OTA at an arbitrary prefix boundary. Linker layout can
  // move the embedded OFE signature far into a perfectly valid .ino.bin. The
  // browser already validates the complete file
  // before FW_BEGIN, and the module does not commit the OTA partition until
  // FW_END. We therefore keep scanning the real streamed bytes and perform the
  // authoritative embedded-signature check in /update/module/end. A mismatch
  // there triggers FW_ABORT, so the staged image is never activated.
  return true;
}

static void master_update_abort_active() {
  Update.abort();
  master_update_active = false;
  master_update_ok = false;
  master_chunk_ok = false;
  master_update_offset = 0;
  master_update_size = 0;
  master_update_progress = 0;
  master_update_speed_bps = 0;
  master_update_unsafe_fw_type = false;
  master_update_signature_reset();
  scheduler.notifyDisplayUpdate(false, ADDR_MASTER, 0);
}

static void web_handle_master_chunk_begin() {
  if (master_update_active) {
    web.send(409, "text/plain; charset=utf-8", "Master update already active");
    return;
  }
  master_update_ok = false;
  master_chunk_ok = false;
  master_update_offset = 0;
  master_update_size = (uint32_t)strtoul(web.arg("size").c_str(), nullptr, 0);
  master_update_progress = 0;
  master_update_speed_bps = 0;
  master_update_speed_sample_ms = millis();
  master_update_speed_sample_offset = 0;
  update_status_msg[0] = 0;
  master_update_signature_reset();
  master_update_unsafe_fw_type = developer_mode_enabled && web.hasArg("unsafe") && web.arg("unsafe") == "1";
  scheduler.notifyDisplayUpdate(true, ADDR_MASTER, 0, 0);
  if (!master_update_unsafe_fw_type && !firmware_signature_allowed(web.arg("sig"), "OFE_FW_SIG:v1;target=MASTER;")) {
    snprintf(update_status_msg, sizeof(update_status_msg), "Firmware signature mismatch. Expected MASTER");
    scheduler.notifyDisplayUpdate(false, ADDR_MASTER, 0);
    web.send(400, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  if (master_update_size == 0) {
    snprintf(update_status_msg, sizeof(update_status_msg), "No update size");
    scheduler.notifyDisplayUpdate(false, ADDR_MASTER, 0);
    web.send(400, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  if (!master_update_unsafe_fw_type && !master_update_auth.begin(web.arg("auth"), master_update_size, "MASTER")) {
    snprintf(update_status_msg, sizeof(update_status_msg), "%s", master_update_auth.error());
    scheduler.notifyDisplayUpdate(false, ADDR_MASTER, 0);
    web.send(400, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  if (!Update.begin(master_update_size)) {
    snprintf(update_status_msg, sizeof(update_status_msg), "Update.begin failed");
    scheduler.notifyDisplayUpdate(false, ADDR_MASTER, 0);
    web.send(500, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  master_update_active = true;
  web.send(200, "text/plain; charset=utf-8", "Master update started");
}

static void web_handle_master_chunk_done() {
  if (master_chunk_ok) {
    web.send(200, "text/plain; charset=utf-8", String(master_update_offset));
  } else {
    web.send(500, "text/plain; charset=utf-8", update_status_msg[0] ? update_status_msg : "Master chunk failed");
  }
}

static void web_handle_master_chunk_upload() {
  HTTPUpload& upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    master_chunk_ok = false;
    update_status_msg[0] = 0;
    const uint32_t expected = (uint32_t)strtoul(web.arg("offset").c_str(), nullptr, 0);
    if (expected != master_update_offset) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Bad offset %lu expected %lu", (unsigned long)expected, (unsigned long)master_update_offset);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (update_status_msg[0]) return;
    if (upload.currentSize == 0) return;
    if (!master_update_check_real_bytes(upload.buf, upload.currentSize)) return;
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Update.write failed");
      master_update_abort_active();
      return;
    }
    master_update_offset += upload.currentSize;
    const uint32_t speed_now = millis();
    const uint32_t speed_elapsed = speed_now - master_update_speed_sample_ms;
    bool speed_changed = false;
    if (speed_elapsed >= 250UL && master_update_offset >= master_update_speed_sample_offset) {
      const uint32_t bytes = master_update_offset - master_update_speed_sample_offset;
      const uint32_t sample_bps = speed_elapsed ? (bytes * 1000UL / speed_elapsed) : 0;
      master_update_speed_bps = master_update_speed_bps ?
        (master_update_speed_bps * 3UL + sample_bps) / 4UL : sample_bps;
      master_update_speed_sample_ms = speed_now;
      master_update_speed_sample_offset = master_update_offset;
      speed_changed = true;
    }
    uint8_t progress = master_update_size ? (uint8_t)((master_update_offset * 100UL) / master_update_size) : 0;
    if (progress > 99 && master_update_offset < master_update_size) progress = 99;
    if (progress != master_update_progress || speed_changed) {
      master_update_progress = progress;
      scheduler.notifyDisplayUpdate(true, ADDR_MASTER, master_update_progress, master_update_speed_bps);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    master_chunk_ok = !update_status_msg[0];
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    snprintf(update_status_msg, sizeof(update_status_msg), "Upload aborted");
    master_chunk_ok = false;
    master_update_abort_active();
  }
}

static void web_handle_master_chunk_end() {
  if (!master_update_unsafe_fw_type && !master_update_payload_signature_verified) {
    snprintf(update_status_msg, sizeof(update_status_msg),
      "Embedded firmware signature missing/mismatch (MASTER, scanned %lu B)",
      (unsigned long)master_update_signature_probe_bytes);
    master_update_abort_active();
    web.send(400, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  if (!master_update_unsafe_fw_type && !master_update_auth.finish()) {
    snprintf(update_status_msg, sizeof(update_status_msg), "%s", master_update_auth.error());
    master_update_abort_active();
    web.send(400, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  if (master_update_size && master_update_offset != master_update_size) {
    String msg = F("Size mismatch ");
    msg += master_update_offset;
    msg += F("/");
    msg += master_update_size;
    snprintf(update_status_msg, sizeof(update_status_msg), "%s", msg.c_str());
    master_update_abort_active();
    web.send(500, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  master_update_ok = !update_status_msg[0] && Update.end(true);
  if (!master_update_ok) {
    snprintf(update_status_msg, sizeof(update_status_msg), "Update.end failed");
    master_update_abort_active();
    web.send(500, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  master_update_active = false;
  scheduler.notifyDisplayUpdate(false, ADDR_MASTER, 100, master_update_speed_bps);
  prepare_controlled_restart();
  web.send(200, "text/plain; charset=utf-8", "Master update complete. Rebooting...");
  delay(700);
  ESP.restart();
}

static void web_handle_master_chunk_abort() {
  if (!update_status_msg[0]) snprintf(update_status_msg, sizeof(update_status_msg), "Master update aborted");
  master_update_abort_active();
  web.send(200, "text/plain; charset=utf-8", update_status_msg);
}

static void web_handle_master_update_done() {
  if (web.hasArg("xhr")) {
    web.send(master_update_ok ? 200 : 500, "text/plain; charset=utf-8", master_update_ok ? "Master update complete. Rebooting..." : String("Master update failed: ") + update_status_msg);
    if (master_update_ok) {
      prepare_controlled_restart();
      delay(700);
      ESP.restart();
    }
    return;
  }
  web.send(200, "text/html; charset=utf-8", update_page(master_update_ok ? "Master update complete. Rebooting..." : "Master update failed."));
  if (master_update_ok) {
    prepare_controlled_restart();
    delay(700);
    ESP.restart();
  }
}

static void web_handle_master_update_upload() {
  HTTPUpload& upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (master_update_active) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Master update already active");
      return;
    }
    master_update_ok = false;
    master_update_offset = 0;
    master_update_size = (uint32_t)strtoul(web.arg("size").c_str(), nullptr, 0);
    master_update_progress = 0;
    master_update_speed_bps = 0;
    master_update_speed_sample_ms = millis();
    master_update_speed_sample_offset = 0;
    update_status_msg[0] = 0;
    scheduler.notifyDisplayUpdate(true, ADDR_MASTER, 0, 0);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Update.begin failed");
    } else {
      master_update_active = true;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!master_update_active || update_status_msg[0]) return;
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Update.write failed");
      master_update_abort_active();
      return;
    }
    master_update_offset += upload.currentSize;
    const uint32_t speed_now = millis();
    const uint32_t speed_elapsed = speed_now - master_update_speed_sample_ms;
    bool speed_changed = false;
    if (speed_elapsed >= 250UL && master_update_offset >= master_update_speed_sample_offset) {
      const uint32_t bytes = master_update_offset - master_update_speed_sample_offset;
      const uint32_t sample_bps = bytes * 1000UL / speed_elapsed;
      master_update_speed_bps = master_update_speed_bps ?
        (master_update_speed_bps * 3UL + sample_bps) / 4UL : sample_bps;
      master_update_speed_sample_ms = speed_now;
      master_update_speed_sample_offset = master_update_offset;
      speed_changed = true;
    }
    uint8_t progress = master_update_size ? (uint8_t)((master_update_offset * 100UL) / master_update_size) : 0;
    if (progress > 99) progress = 99;
    if (progress != master_update_progress || speed_changed) {
      master_update_progress = progress;
      scheduler.notifyDisplayUpdate(true, ADDR_MASTER, master_update_progress, master_update_speed_bps);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    master_update_ok = !update_status_msg[0] && Update.end(true);
    if (!master_update_ok) snprintf(update_status_msg, sizeof(update_status_msg), "Update.end failed");
    master_update_active = false;
    scheduler.notifyDisplayUpdate(!master_update_ok, ADDR_MASTER, master_update_ok ? 100 : master_update_progress, master_update_speed_bps);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    master_update_abort_active();
  }
}

static void module_update_queue_reset() {
  portENTER_CRITICAL(&module_update_queue_mux);
  module_update_queue_head = 0;
  module_update_queue_tail = 0;
  module_update_queue_count = 0;
  module_update_queue_low_water = MODULE_FW_QUEUE_SIZE;
  module_update_queue_empty_polls = 0;
  module_update_frames_sent = 0;
  module_update_http_chunks = 0;
  module_update_last_http_ms = 0;
  module_update_max_http_gap_ms = 0;
  module_update_last_ack_ms = 0;
  module_update_max_ack_ms = 0;
  module_update_starve_count = 0;
  module_update_starve_since_ms = 0;
  module_update_starve_max_ms = 0;
  module_update_last_pump_ms = 0;
  module_update_last_pump_gap_ms = 0;
  module_update_max_pump_gap_ms = 0;
  module_update_stream_started = false;
  module_update_queued_offset = 0;
  module_update_signature_checked = false;
  portEXIT_CRITICAL(&module_update_queue_mux);
  module_update_speed_bps = 0;
  module_update_speed_sample_ms = millis();
  module_update_speed_sample_offset = 0;
}

static uint32_t module_update_sample_speed(bool force = false) {
  const uint32_t now = millis();
  if (!module_update_speed_sample_ms) {
    module_update_speed_sample_ms = now;
    module_update_speed_sample_offset = module_update_offset;
    return module_update_speed_bps;
  }
  const uint32_t elapsed_ms = now - module_update_speed_sample_ms;
  if (!elapsed_ms || (!force && elapsed_ms < 250UL)) return module_update_speed_bps;

  const uint32_t bytes = module_update_offset - module_update_speed_sample_offset;
  if (bytes) {
    const uint32_t sample_bps = (uint32_t)(((uint64_t)bytes * 1000ULL) / elapsed_ms);
    module_update_speed_bps = module_update_speed_bps
      ? (module_update_speed_bps * 3UL + sample_bps) / 4UL
      : sample_bps;
  }
  module_update_speed_sample_ms = now;
  module_update_speed_sample_offset = module_update_offset;
  return module_update_speed_bps;
}

static bool module_update_queue_push(const uint8_t* data, size_t len, uint32_t timeout_ms) {
  if (!data && len) return false;
  const uint32_t start = millis();
  const uint32_t now = start;
  if (module_update_last_http_ms) {
    const uint32_t gap = now - module_update_last_http_ms;
    if (gap > module_update_max_http_gap_ms) module_update_max_http_gap_ms = gap;
  }
  module_update_last_http_ms = now;
  module_update_http_chunks++;
  size_t pos = 0;
  while (pos < len) {
    size_t wrote = 0;
    portENTER_CRITICAL(&module_update_queue_mux);
    while (pos + wrote < len && module_update_queue_count < MODULE_FW_QUEUE_SIZE) {
      module_update_queue[module_update_queue_head] = data[pos + wrote];
      module_update_queue_head = (module_update_queue_head + 1) % MODULE_FW_QUEUE_SIZE;
      module_update_queue_count++;
      module_update_queued_offset++;
      wrote++;
    }
    portEXIT_CRITICAL(&module_update_queue_mux);

    pos += wrote;
    if (pos >= len) return true;
    if (update_status_msg[0] || !module_update_addr) return false;
    if ((uint32_t)(millis() - start) > timeout_ms) return false;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

static uint8_t module_update_queue_pop(uint8_t* out, uint8_t max_len) {
  if (!out || !max_len) return 0;
  uint8_t n = 0;
  portENTER_CRITICAL(&module_update_queue_mux);
  while (n < max_len && module_update_queue_count > 0) {
    out[n++] = module_update_queue[module_update_queue_tail];
    module_update_queue_tail = (module_update_queue_tail + 1) % MODULE_FW_QUEUE_SIZE;
    module_update_queue_count--;
  }
  if (module_update_queue_count < module_update_queue_low_water) module_update_queue_low_water = module_update_queue_count;
  if (n == 0) module_update_queue_empty_polls++;
  portEXIT_CRITICAL(&module_update_queue_mux);

  const uint32_t now = millis();
  if (n == 0) {
    // Count only real producer starvation: the update is active, more firmware
    // bytes are expected from HTTP, and the RS485 pump has nothing to send.
    if (module_update_stream_started && module_update_addr && module_update_size && module_update_queued_offset < module_update_size) {
      portENTER_CRITICAL(&module_update_queue_mux);
      if (!module_update_starve_since_ms) {
        module_update_starve_since_ms = now ? now : 1;
        module_update_starve_count++;
      }
      portEXIT_CRITICAL(&module_update_queue_mux);
    }
  } else {
    portENTER_CRITICAL(&module_update_queue_mux);
    if (module_update_starve_since_ms) {
      const uint32_t dur = now - module_update_starve_since_ms;
      if (dur > module_update_starve_max_ms) module_update_starve_max_ms = dur;
      module_update_starve_since_ms = 0;
    }
    portEXIT_CRITICAL(&module_update_queue_mux);
  }
  return n;
}

static void module_update_fail_from_pump(const char* prefix) {
  if (!update_status_msg[0]) {
    snprintf(update_status_msg, sizeof(update_status_msg), "%s at %lu: %s", prefix, (unsigned long)module_update_offset, scheduler.lastModuleFwError());
  }
  module_update_abort_active();
}

static void module_update_pump() {
  if (!module_update_addr || update_status_msg[0]) return;

  // Developer timing: start-to-start interval between pump calls while the
  // RS485 stream is active. This includes the preceding FW_CHUNK ACK wait, so
  // compare pump max with ACK max: a large excess identifies loop/task stalls.
  if (module_update_stream_started) {
    const uint32_t pump_now = millis();
    portENTER_CRITICAL(&module_update_queue_mux);
    if (module_update_last_pump_ms) {
      const uint32_t gap = pump_now - module_update_last_pump_ms;
      module_update_last_pump_gap_ms = gap;
      if (gap > module_update_max_pump_gap_ms) module_update_max_pump_gap_ms = gap;
    }
    module_update_last_pump_ms = pump_now;
    portEXIT_CRITICAL(&module_update_queue_mux);
  }

  // Prime one full browser block before the first FW_CHUNK. Without this, the
  // time between FW_BEGIN and the first HTTP chunk appears as a starvation and
  // the consumer starts with no reserve. Once started we never re-prefill, so a
  // genuine mid-stream producer gap remains visible in the Developer counters.
  if (!module_update_stream_started) {
    size_t queued = 0;
    uint32_t queued_offset = 0;
    portENTER_CRITICAL(&module_update_queue_mux);
    queued = module_update_queue_count;
    queued_offset = module_update_queued_offset;
    portEXIT_CRITICAL(&module_update_queue_mux);
    const size_t prefill = (module_update_size < (uint32_t)MODULE_FW_QUEUE_PREFILL_SIZE)
      ? (size_t)module_update_size : (size_t)MODULE_FW_QUEUE_PREFILL_SIZE;
    const bool producer_finished = module_update_size && queued_offset >= module_update_size;
    if (!producer_finished && queued < prefill) return;
    if (!queued) return;
    portENTER_CRITICAL(&module_update_queue_mux);
    module_update_stream_started = true;
    module_update_last_pump_ms = millis();
    module_update_last_pump_gap_ms = 0;
    portEXIT_CRITICAL(&module_update_queue_mux);
  }

  ModuleRecord* rec = registry.find(module_update_addr);
  const bool display_target = rec && rec->type == MODULE_DISPLAY;
  const uint8_t max_frames = display_target ? (uint8_t)MODULE_FW_DISPLAY_PUMP_FRAMES_PER_LOOP : (uint8_t)MODULE_FW_PUMP_FRAMES_PER_LOOP;
  const uint32_t start_ms = millis();

  for (uint8_t sent = 0; sent < max_frames; ++sent) {
    uint8_t frame[MAX_PAYLOAD - 4];
    const uint8_t fw_chunk_size = module_update_fw_chunk_size(module_update_addr);
    const uint8_t n = module_update_queue_pop(frame, fw_chunk_size);
    if (!n) return;

    const uint32_t ack_start = millis();
    if (!scheduler.moduleFwChunk(module_update_addr, module_update_offset, frame, n)) {
      module_update_fail_from_pump("FW_CHUNK failed");
      return;
    }

    const uint32_t ack_ms = millis() - ack_start;
    module_update_last_ack_ms = ack_ms;
    if (ack_ms > module_update_max_ack_ms) module_update_max_ack_ms = ack_ms;
    module_update_frames_sent++;

    module_update_offset += n;
    module_update_sample_speed();
    if (module_update_size) {
      uint8_t progress = (uint8_t)((module_update_offset * 100UL) / module_update_size);
      if (progress > 99) progress = 99;
      if (progress != module_update_progress) {
        module_update_progress = progress;
        scheduler.notifyDisplayUpdate(true, module_update_addr, module_update_progress, module_update_speed_bps);
      }
    }

    if ((uint32_t)(millis() - start_ms) >= MODULE_FW_PUMP_BUDGET_MS) return;
  }
}

static bool module_update_wait_sent(uint32_t target_offset, uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (module_update_addr && !update_status_msg[0] && module_update_offset < target_offset) {
    if ((uint32_t)(millis() - start) > timeout_ms) {
      snprintf(update_status_msg, sizeof(update_status_msg), "FW_CHUNK queue timeout at %lu", (unsigned long)module_update_offset);
      module_update_abort_active();
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return module_update_addr && !update_status_msg[0] && module_update_offset >= target_offset;
}
static bool module_update_abort_active() {
  const uint8_t abort_addr = module_update_addr
      ? module_update_addr
      : (scheduler.moduleFirmwareUpdateActive() ? scheduler.moduleFirmwareUpdateTarget() : ADDR_INVALID);

  // Stop the producer and consumer before FW_ABORT is sent. Otherwise a chunk
  // already waiting in the HTTP queue can race the abort and put the module
  // straight back into FW busy after it acknowledged the cancellation.
  module_update_addr = 0;
  module_update_ok = false;
  module_chunk_ok = false;

  // Preserve the last update timing counters for Developer-mode post-mortem.
  // Only discard queued bytes here; the next /begin resets all statistics.
  const uint32_t abort_ms = millis();
  portENTER_CRITICAL(&module_update_queue_mux);
  if (module_update_starve_since_ms) {
    const uint32_t dur = abort_ms - module_update_starve_since_ms;
    if (dur > module_update_starve_max_ms) module_update_starve_max_ms = dur;
    module_update_starve_since_ms = 0;
  }
  module_update_queue_head = 0;
  module_update_queue_tail = 0;
  module_update_queue_count = 0;
  portEXIT_CRITICAL(&module_update_queue_mux);

  const bool acknowledged = abort_addr != ADDR_INVALID
      ? scheduler.moduleFwAbort(abort_addr)
      : true;
  if (abort_addr == ADDR_INVALID) scheduler.notifyDisplayUpdate(false, ADDR_INVALID, 0);
  module_update_unsafe_fw_type = false;
  module_update_signature_checked = false;
  module_update_signature_reset();
  return acknowledged;
}

static void web_handle_module_chunk_abort() {
  if (!update_status_msg[0]) snprintf(update_status_msg, sizeof(update_status_msg), "Module update aborted");
  const bool acknowledged = module_update_abort_active();
  if (acknowledged) {
    web.send(200, "text/plain; charset=utf-8", update_status_msg);
  } else {
    web.send(504, "text/plain; charset=utf-8", "FW_ABORT was not acknowledged by the module");
  }
}

static void web_handle_module_update_done() {
  String msg = module_update_ok ? "Module update complete. Module rebooting..." : String("Module update failed: ") + update_status_msg;
  if (web.hasArg("xhr")) {
    web.send(module_update_ok ? 200 : 500, "text/plain; charset=utf-8", msg);
    return;
  }
  web.send(200, "text/html; charset=utf-8", update_page(msg));
}

static uint8_t module_update_target_from_request() {
  String arg = web.arg("addr");
  if (!arg.length()) return 0;
  return (uint8_t)strtoul(arg.c_str(), nullptr, 0);
}

static String module_firmware_filename_normalized(const String& filename) {
  String n = filename;
  n.toLowerCase();
  n.replace('\\', '/');
  int slash = n.lastIndexOf('/');
  if (slash >= 0) n = n.substring(slash + 1);
  return n;
}

static const char* module_firmware_hint(uint8_t type) {
  switch (type) {
    case MODULE_JBC_BUS: return "JbcBusModule*.bin";
    case MODULE_JBC_USB: return "JbcUsbModule*.bin";
    case MODULE_FAN_IO: return "FanIoModule*.bin";
    case MODULE_FAN_IO_PRO: return "FanIoProModule*.bin";
    case MODULE_WELLER_ZERO_SMOG: return "WellerZeroSmogModule*.bin";
    case MODULE_DISPLAY: return "DisplayModule*.bin";
    case MODULE_UNIVERSAL_RS232: return "UniversalRs232Module*.bin";
    case MODULE_MODBUS_RTU: return "ModbusRtuModule*.bin";
    case MODULE_SENSOR_RESERVED: return "Sensor*.bin";
    default: return "matching module firmware *.bin";
  }
}

static bool module_firmware_filename_allowed(uint8_t type, const String& filename) {
  String n = module_firmware_filename_normalized(filename);
  if (!n.length() || !n.endsWith(".bin")) return false;
  const bool has_jbc = n.indexOf("jbc") >= 0;
  const bool has_usb = n.indexOf("usb") >= 0;
  const bool has_display = n.indexOf("display") >= 0;
  const bool has_weller = n.indexOf("weller") >= 0 || (n.indexOf("zero") >= 0 && n.indexOf("smog") >= 0);
  const bool has_fanio = n.indexOf("fanio") >= 0 || n.indexOf("fan_io") >= 0 || n.indexOf("fan-io") >= 0;
  const bool has_pro = n.indexOf("pro") >= 0;
  const bool has_sensor = n.indexOf("sensor") >= 0;
  const bool has_universal = n.indexOf("universal") >= 0 || n.indexOf("rs232") >= 0 || n.indexOf("uart") >= 0 || n.indexOf("bridge") >= 0;
  const bool has_modbus = n.indexOf("modbus") >= 0 || n.indexOf("rtu") >= 0;

  switch (type) {
    case MODULE_JBC_BUS:
      return has_jbc && !has_usb;
    case MODULE_JBC_USB:
      return has_jbc && has_usb;
    case MODULE_FAN_IO:
      return has_fanio && !has_pro;
    case MODULE_FAN_IO_PRO:
      return (has_fanio && has_pro) || n.indexOf("faniopro") >= 0 || n.indexOf("relay") >= 0;
    case MODULE_WELLER_ZERO_SMOG:
      return has_weller;
    case MODULE_DISPLAY:
      return has_display;
    case MODULE_UNIVERSAL_RS232:
      return has_universal;
    case MODULE_MODBUS_RTU:
      return has_modbus || (has_universal && n.indexOf("modbus") >= 0);
    case MODULE_SENSOR_RESERVED:
      return has_sensor;
    default:
      return false;
  }
}

static const char* module_firmware_signature_hint(uint8_t type) {
  switch (type) {
    case MODULE_JBC_BUS: return "OFE_FW_SIG:v1;target=JBC_BUS;";
    case MODULE_JBC_USB: return "OFE_FW_SIG:v1;target=JBC_USB;";
    case MODULE_FAN_IO: return "OFE_FW_SIG:v1;target=FAN_IO;";
    case MODULE_FAN_IO_PRO: return "OFE_FW_SIG:v1;target=FAN_IO_PRO;";
    case MODULE_WELLER_ZERO_SMOG: return "OFE_FW_SIG:v1;target=WELLER_ZERO_SMOG;";
    case MODULE_DISPLAY: return "OFE_FW_SIG:v1;target=DISPLAY;";
    case MODULE_UNIVERSAL_RS232: return "OFE_FW_SIG:v1;target=UNIVERSAL_RS232;";
    case MODULE_MODBUS_RTU: return "OFE_FW_SIG:v1;target=MODBUS_RTU;";
    case MODULE_SENSOR_RESERVED: return "OFE_FW_SIG:v1;target=SENSOR;";
    default: return "";
  }
}

static String module_firmware_signature_hint_for(const ModuleRecord& rec) {
  if (rec.type == MODULE_DISPLAY || (rec.caps & CAP_DISPLAY)) {
    if (rec.caps & CAP_DISPLAY_800X480) return F("OFE_FW_SIG:v1;target=DISPLAY_800X480;");
    if (rec.caps & CAP_DISPLAY_320X480) return F("OFE_FW_SIG:v1;target=DISPLAY_320X480;");
    return F("OFE_FW_SIG:v1;target=DISPLAY_320X480; or DISPLAY_800X480;");
  }
  return String(module_firmware_signature_hint(rec.type));
}

static const char* module_firmware_auth_target_for(const ModuleRecord& rec) {
  if (rec.type == MODULE_DISPLAY || (rec.caps & CAP_DISPLAY)) {
    if (rec.caps & CAP_DISPLAY_800X480) return "DISPLAY_800X480";
    if (rec.caps & CAP_DISPLAY_320X480) return "DISPLAY_320X480";
    return "DISPLAY";
  }
  switch (rec.type) {
    case MODULE_JBC_BUS: return "JBC_BUS";
    case MODULE_JBC_USB: return "JBC_USB";
    case MODULE_FAN_IO: return "FAN_IO";
    case MODULE_FAN_IO_PRO: return "FAN_IO_PRO";
    case MODULE_WELLER_ZERO_SMOG: return "WELLER_ZERO_SMOG";
    case MODULE_UNIVERSAL_RS232: return "UNIVERSAL_RS232";
    case MODULE_MODBUS_RTU: return "MODBUS_RTU";
    case MODULE_SENSOR_RESERVED: return "SENSOR";
    default: return "";
  }
}

static bool firmware_signature_allowed(const String& sig, const char* expected) {
  if (!expected || !expected[0]) return false;
  String s = sig;
  s.trim();
  if (s.length() < 16 || s.length() > 160) return false;
  return s.indexOf(expected) >= 0;
}

static bool module_firmware_signature_allowed(const ModuleRecord& rec, const String& sig) {
  if (rec.type == MODULE_DISPLAY || (rec.caps & CAP_DISPLAY)) {
    if (rec.caps & CAP_DISPLAY_800X480) return firmware_signature_allowed(sig, "OFE_FW_SIG:v1;target=DISPLAY_800X480;");
    if (rec.caps & CAP_DISPLAY_320X480) {
      // Explicit 320x480 hardware must only accept the matching firmware.
      // Do not accept the old generic DISPLAY signature once the panel size is known.
      return firmware_signature_allowed(sig, "OFE_FW_SIG:v1;target=DISPLAY_320X480;");
    }
    return firmware_signature_allowed(sig, "OFE_FW_SIG:v1;target=DISPLAY;") ||
           firmware_signature_allowed(sig, "OFE_FW_SIG:v1;target=DISPLAY_320X480;") ||
           firmware_signature_allowed(sig, "OFE_FW_SIG:v1;target=DISPLAY_800X480;");
  }
  return firmware_signature_allowed(sig, module_firmware_signature_hint(rec.type));
}
static bool firmware_image_header_allowed(const uint8_t* data, size_t len) {
  if (!data || len < 8) return false;
  if (data[0] != 0xE9) return false; // ESP image magic
  if (data[1] == 0 || data[1] > 16) return false; // segment count
  if (data[2] > 3) return false; // flash mode
  bool all_zero = true, all_ff = true;
  for (size_t i = 0; i < 8; ++i) {
    if (data[i] != 0x00) all_zero = false;
    if (data[i] != 0xFF) all_ff = false;
  }
  return !all_zero && !all_ff;
}

static bool module_update_check_firmware_header(const uint8_t* data, size_t len) {
  if (module_update_unsafe_fw_type || module_update_signature_checked) return true;
  module_update_signature_checked = true;
  if (firmware_image_header_allowed(data, len)) return true;
  snprintf(update_status_msg, sizeof(update_status_msg), "Firmware image header invalid");
  module_update_abort_active();
  return false;
}

static uint8_t module_update_fw_chunk_size(uint8_t addr) {
  ModuleRecord* rec = registry.find(addr);
  if (rec && rec->type == MODULE_DISPLAY) return (uint8_t)MODULE_FW_DISPLAY_CHUNK_SIZE;
  return (uint8_t)MODULE_FW_CHUNK_SIZE;
}

static bool module_update_target_allowed(uint8_t addr, const String& filename) {
  if (addr < 0x10 || addr > 0x6F) return false;
  ModuleRecord* rec = registry.find(addr);
  if (!rec || !rec->online || !(rec->caps & CAP_FW_UPDATE)) return false;
  if (filename.length() && !module_firmware_filename_allowed(rec->type, filename)) return false;
  return true;
}

static void web_handle_module_chunk_begin() {
  if (module_update_addr || scheduler.moduleFirmwareUpdateActive()) {
    web.send(409, "text/plain; charset=utf-8", "A module firmware update is already active");
    return;
  }
  module_update_ok = false;
  module_chunk_ok = false;
  module_update_offset = 0;
  module_update_size = (uint32_t)strtoul(web.arg("size").c_str(), nullptr, 0);
  module_update_progress = 0;
  update_status_msg[0] = 0;
  const uint8_t target_addr = module_update_target_from_request();
  if (!target_addr) {
    web.send(400, "text/plain; charset=utf-8", "No module address");
    return;
  }
  ModuleRecord* rec = registry.find(target_addr);
  const String filename = web.arg("name");
  if (!rec || !rec->online || !(rec->caps & CAP_FW_UPDATE)) {
    web.send(400, "text/plain; charset=utf-8", "Module is not online or not firmware-update capable");
    return;
  }
  const bool unsafe_fw_type = developer_mode_enabled && web.hasArg("unsafe") && web.arg("unsafe") == "1";
  module_update_unsafe_fw_type = unsafe_fw_type;
  if (!unsafe_fw_type && !module_firmware_filename_allowed(rec->type, filename)) {
    String msg = F("Firmware filename does not match target module type. Expected: ");
    msg += module_firmware_hint(rec->type);
    web.send(400, "text/plain; charset=utf-8", msg);
    return;
  }
  if (!unsafe_fw_type && !module_firmware_signature_allowed(*rec, web.arg("sig"))) {
    String msg = F("Firmware signature mismatch. Expected: ");
    msg += module_firmware_signature_hint_for(*rec);
    web.send(400, "text/plain; charset=utf-8", msg);
    return;
  }

  module_update_queue_reset();
  module_update_signature_reset();
  if (!unsafe_fw_type) {
    const String expected_sig = module_firmware_signature_hint_for(*rec);
    expected_sig.toCharArray(module_update_expected_signature, sizeof(module_update_expected_signature));
    if (!module_update_auth.begin(web.arg("auth"), module_update_size, module_firmware_auth_target_for(*rec))) {
      snprintf(update_status_msg, sizeof(update_status_msg), "%s", module_update_auth.error());
      module_update_abort_active();
      web.send(400, "text/plain; charset=utf-8", update_status_msg);
      return;
    }
  }

  module_update_addr = target_addr;
  if (!scheduler.moduleFwBegin(module_update_addr, module_update_size)) {
    snprintf(update_status_msg, sizeof(update_status_msg), "FW_BEGIN failed: %s", scheduler.lastModuleFwError());
    module_update_abort_active();
    web.send(500, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  web.send(200, "text/plain; charset=utf-8", "RS485 update started");
}

static void web_handle_module_chunk_done() {
  if (module_chunk_ok) {
    if (developer_mode_enabled) {
      // Piggy-back developer diagnostics on the chunk response already in the
      // critical upload path. No extra HTTP stats request is needed per chunk.
      web.send(200, "application/json; charset=utf-8", module_update_stats_json());
    } else {
      web.send(200, "text/plain; charset=utf-8", String(module_update_queued_offset));
    }
  } else {
    web.send(500, "text/plain; charset=utf-8", update_status_msg[0] ? update_status_msg : "FW_CHUNK failed");
  }
}

static void web_handle_module_chunk_upload() {
  HTTPUpload& upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    module_chunk_ok = false;
    update_status_msg[0] = 0;
    const uint32_t expected = (uint32_t)strtoul(web.arg("offset").c_str(), nullptr, 0);
    if (!module_update_addr || expected != module_update_queued_offset) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Bad offset %lu expected %lu", (unsigned long)expected, (unsigned long)module_update_queued_offset);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (update_status_msg[0]) return;
    if (module_update_size && module_update_queued_offset + upload.currentSize > module_update_size) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Upload exceeds size %lu", (unsigned long)module_update_size);
      module_update_abort_active();
      return;
    }
    if (module_update_queued_offset == 0 && !module_update_check_firmware_header(upload.buf, upload.currentSize)) return;
    if (!module_update_check_real_signature(upload.buf, upload.currentSize)) return;
    if (!module_update_queue_push(upload.buf, upload.currentSize, 15000UL)) {
      if (!update_status_msg[0]) snprintf(update_status_msg, sizeof(update_status_msg), "FW_CHUNK queue full at %lu", (unsigned long)module_update_offset);
      module_update_abort_active();
      return;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    // The browser chunk is accepted once it is queued. The final /end request
    // waits until the RS485 side has drained the queue and commits FW_END.
    module_chunk_ok = module_update_addr && !update_status_msg[0];
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    snprintf(update_status_msg, sizeof(update_status_msg), "Upload aborted");
    module_chunk_ok = false;
    module_update_abort_active();
  }
}

static void web_handle_module_chunk_end() {
  if (!module_update_addr) {
    web.send(400, "text/plain; charset=utf-8", "No active module update");
    return;
  }
  if (!module_update_unsafe_fw_type && !module_update_payload_signature_verified) {
    snprintf(update_status_msg, sizeof(update_status_msg),
      "Embedded firmware signature missing/mismatch after %lu bytes (expected %.52s)",
      (unsigned long)module_update_signature_probe_bytes,
      module_update_expected_signature);
    module_update_abort_active();
    web.send(400, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  if (!module_update_unsafe_fw_type && !module_update_auth.finish()) {
    snprintf(update_status_msg, sizeof(update_status_msg), "%s", module_update_auth.error());
    module_update_abort_active();
    web.send(400, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  if (!module_update_wait_sent(module_update_queued_offset, 30000UL)) {
    web.send(500, "text/plain; charset=utf-8", update_status_msg[0] ? update_status_msg : "FW_CHUNK queue not drained");
    return;
  }
  if (module_update_size && module_update_offset != module_update_size) {
    String msg = F("Size mismatch ");
    msg += module_update_offset;
    msg += F("/");
    msg += module_update_size;
    snprintf(update_status_msg, sizeof(update_status_msg), "%s", msg.c_str());
    module_update_abort_active();
    web.send(500, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  module_update_ok = scheduler.moduleFwEnd(module_update_addr);
  if (!module_update_ok) {
    snprintf(update_status_msg, sizeof(update_status_msg), "FW_END failed: %s", scheduler.lastModuleFwError());
    module_update_abort_active();
    web.send(500, "text/plain; charset=utf-8", update_status_msg);
    return;
  }
  module_update_sample_speed(true);
  scheduler.notifyDisplayUpdate(false, module_update_addr, 100, module_update_speed_bps);
  // Keep the completed update diagnostics available for Developer mode. The
  // queue has already been drained by module_update_wait_sent(); counters are
  // reset at the next /begin, not here.
  const uint32_t update_end_ms = millis();
  portENTER_CRITICAL(&module_update_queue_mux);
  if (module_update_starve_since_ms) {
    const uint32_t dur = update_end_ms - module_update_starve_since_ms;
    if (dur > module_update_starve_max_ms) module_update_starve_max_ms = dur;
    module_update_starve_since_ms = 0;
  }
  portEXIT_CRITICAL(&module_update_queue_mux);
  module_update_unsafe_fw_type = false;
  module_update_signature_checked = false;
  module_update_signature_reset();
  module_update_addr = 0;
  web.send(200, "text/plain; charset=utf-8", "Module update complete. Module rebooting...");
}

static void web_handle_module_update_upload() {
  HTTPUpload& upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (module_update_addr || scheduler.moduleFirmwareUpdateActive()) {
      snprintf(update_status_msg, sizeof(update_status_msg), "A module firmware update is already active");
      return;
    }
    module_update_ok = false;
    module_update_offset = 0;
    module_update_size = 0;
    module_update_progress = 0;
    update_status_msg[0] = 0;
    const uint8_t target_addr = module_update_target_from_request();
    if (!target_addr) {
      snprintf(update_status_msg, sizeof(update_status_msg), "No module address");
      return;
    }
    ModuleRecord* rec = registry.find(target_addr);
    if (!rec || !rec->online || !(rec->caps & CAP_FW_UPDATE)) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Module is not online or not firmware-update capable");
      return;
    }
    const bool unsafe_fw_type = developer_mode_enabled && web.hasArg("unsafe") && web.arg("unsafe") == "1";
    module_update_queue_reset();
    module_update_unsafe_fw_type = unsafe_fw_type;
    if (!unsafe_fw_type && !module_firmware_filename_allowed(rec->type, upload.filename)) {
      snprintf(update_status_msg, sizeof(update_status_msg), "Firmware filename mismatch. Expected %s", module_firmware_hint(rec->type));
      return;
    }
    module_update_addr = target_addr;
    const uint32_t total = 0;
    if (!scheduler.moduleFwBegin(module_update_addr, total)) {
      snprintf(update_status_msg, sizeof(update_status_msg), "FW_BEGIN failed: %s", scheduler.lastModuleFwError());
      module_update_abort_active();
      return;
    }
    update_status_msg[0] = 0;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!module_update_addr || update_status_msg[0]) return;
    if (module_update_offset == 0 && !module_update_check_firmware_header(upload.buf, upload.currentSize)) return;
    size_t pos = 0;
    while (pos < upload.currentSize) {
      const size_t remaining = upload.currentSize - pos;
      const uint8_t fw_chunk_size = module_update_fw_chunk_size(module_update_addr);
      const uint8_t n = (uint8_t)(remaining > fw_chunk_size ? fw_chunk_size : remaining);
      const uint32_t ack_start = millis();
      if (!scheduler.moduleFwChunk(module_update_addr, module_update_offset, upload.buf + pos, n)) {
        snprintf(update_status_msg, sizeof(update_status_msg), "FW_CHUNK failed at %lu: %s", (unsigned long)module_update_offset, scheduler.lastModuleFwError());
        module_update_abort_active();
        return;
      }
      const uint32_t ack_ms = millis() - ack_start;
      module_update_last_ack_ms = ack_ms;
      if (ack_ms > module_update_max_ack_ms) module_update_max_ack_ms = ack_ms;
      module_update_frames_sent++;

      module_update_offset += n;
      module_update_sample_speed();
      pos += n;
#if MODULE_FW_SERVER_YIELD_MS > 0
      delay(MODULE_FW_SERVER_YIELD_MS);
#endif
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!module_update_addr || update_status_msg[0]) return;
    module_update_ok = scheduler.moduleFwEnd(module_update_addr);
    if (!module_update_ok) {
      snprintf(update_status_msg, sizeof(update_status_msg), "FW_END failed: %s", scheduler.lastModuleFwError());
      module_update_abort_active();
    } else {
      module_update_sample_speed(true);
      scheduler.notifyDisplayUpdate(false, module_update_addr, 100, module_update_speed_bps);
      // Preserve the just-finished Developer diagnostics until the next begin.
      module_update_unsafe_fw_type = false;
      module_update_signature_checked = false;
      module_update_addr = 0;
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    snprintf(update_status_msg, sizeof(update_status_msg), "Upload aborted");
    module_update_ok = false;
    module_update_abort_active();
  }
}
