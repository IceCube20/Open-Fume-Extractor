#pragma once
#include "HostedC6CatalogUi.h"
#include "HostedC6UpstreamUi.h"
#define OFE_C6_UI_SCRIPT R"C6(<script>(()=>{
const cfg=window.OFE_C6,de=cfg.de,$=id=>document.getElementById('c6_'+id);
const stable={expected:cfg.expected,size:cfg.size,url:cfg.url,sha256:cfg.sha256};
const baseline=cfg.baseline||stable,hostExpected=cfg.hostExpected||stable.expected;
const targetSelect=$('target'),upstreamTargets=new Map();
const upstreamMode=()=>!!targetSelect&&targetSelect.value.startsWith('upstream:');
let upstreamBusy=false;
const trialMode=()=>!!targetSelect&&targetSelect.value!=='stable';
function selectTarget(mode){
 if(!targetSelect)return;targetSelect.value=mode;
 Object.assign(cfg,upstreamTargets.get(mode)||(mode==='test'?cfg.test:mode==='restore'?baseline:stable));
 $('trial_note').hidden=mode==='stable';$('recovery').checked=false;
 $('recovery_text').textContent=mode==='stable'?say('C6-UART-Wiederherstellung ist verfügbar','C6 UART recovery is available'):say('Ich akzeptiere den experimentellen Test und den moeglicherweise erforderlichen C6-UART-Rueckweg.','I accept the experimental trial and potentially required direct C6 UART recovery.');
}
async function loadUpstream(){
 if(upstreamBusy||running||token)return;upstreamBusy=true;controls();
 $('upstream_state').textContent=say('Espressif-Versionen werden geladen...','Loading Espressif versions...');
 try{
  const versions=await window.OFE_C6_UPSTREAM.load();
  const selectedVersion=cfg.expected;
  for(const option of [...targetSelect.options])if(option.value!=='stable')option.remove();
  upstreamTargets.clear();
  for(const image of versions){if(image.expected===stable.expected)continue;const key='upstream:'+image.expected+':'+image.commit;upstreamTargets.set(key,image);targetSelect.add(new Option(ver(image.expected),key));}
  for(const option of [...targetSelect.options].sort((a,b)=>(upstreamTargets.get(b.value)?.expected||stable.expected)-(upstreamTargets.get(a.value)?.expected||stable.expected)))targetSelect.append(option);
  selectTarget([...upstreamTargets].find(([,image])=>image.expected===selectedVersion)?.[0]||'stable');
  $('upstream_state').textContent=say('Espressif-Versionen geladen. Keine OFE-Freigabe.','Espressif versions loaded. Not OFE-approved.');
 }catch(e){$('upstream_state').textContent=say('Versionsliste nicht verfuegbar: ','Version list unavailable: ')+e.message;}
 finally{upstreamBusy=false;controls();}
}
let token=0,running=false,last=null,refreshing=false,rebooting=false,ownedError='';
let catalog=null,catalogBusy=false,catalogError='',catalogChecked='';
try{token=Number(sessionStorage.getItem('ofe_c6_token')||0)>>>0}catch(_){}
function saveToken(value){token=value;try{if(value)sessionStorage.setItem('ofe_c6_token',String(value));else sessionStorage.removeItem('ofe_c6_token')}catch(_){}}
const say=(a,b)=>de?a:b,ver=n=>n?[(n>>>16)&255,(n>>>8)&255,n&255].join('.'):'-';
const phases=de?['Bereit','Firmware wird geladen','Firmware geprüft','C6 wird vorbereitet','C6 wird geschrieben','MCU startet neu','Vorgang beendet','Neustart erforderlich']:['Ready','Loading firmware','Firmware verified','Preparing C6','Writing C6','MCU restarting','Operation ended','Restart required'];
const errors=de?{LAN_REQUIRED:'Stabile LAN-Verbindung erforderlich',NO_PSRAM:'Nicht genügend PSRAM',SIZE:'Falsche Dateigröße',OFFSET:'Upload-Reihenfolge ungültig',WRONG_CHIP:'Keine ESP32-C6-Firmware',SHA256:'Firmware-Prüfsumme stimmt nicht',INCOMPLETE:'Firmware unvollständig',EXPIRED:'Vorgang abgelaufen',HOST_SDK_MISMATCH:'MCU-SDK passt nicht zur C6-Firmware',C6_UNAVAILABLE:'C6 nicht erreichbar',C6_TOO_OLD:'C6 benötigt eine UART-Aktualisierung',C6_NEWER:'C6 ist neuer; Downgrade gesperrt',ALREADY_CURRENT:'C6 ist bereits aktuell',NVS_FAILED:'Prüfstatus konnte nicht gespeichert werden',BEGIN_FAILED:'C6-Update konnte nicht gestartet werden',WRITE_FAILED:'C6-Schreibfehler',END_FAILED:'C6-Prüfung fehlgeschlagen',ACTIVATE_FAILED:'C6-Aktivierung nicht bestätigt',TASK_FAILED:'Update-Task konnte nicht gestartet werden',TIMEOUT:'Zeitlimit überschritten',BUSY:'Anderer Vorgang aktiv'}:{};
const english={LAN_REQUIRED:'Stable Ethernet connection required',NO_PSRAM:'Insufficient PSRAM',SIZE:'Incorrect file size',OFFSET:'Invalid upload order',WRONG_CHIP:'Not ESP32-C6 firmware',SHA256:'Firmware checksum mismatch',INCOMPLETE:'Incomplete firmware',EXPIRED:'Operation expired',HOST_SDK_MISMATCH:'MCU SDK does not match C6 firmware',C6_UNAVAILABLE:'C6 unavailable',C6_TOO_OLD:'C6 requires a UART update',C6_NEWER:'Newer C6 firmware; downgrade blocked',ALREADY_CURRENT:'C6 is already up to date',NVS_FAILED:'Could not save verification state',BEGIN_FAILED:'Could not start C6 update',WRITE_FAILED:'C6 write failed',END_FAILED:'C6 verification failed',ACTIVATE_FAILED:'C6 activation unconfirmed',TASK_FAILED:'Could not start update task',TIMEOUT:'Operation timed out',BUSY:'Another operation is active'};
const describe=code=>code==='C6_TRIAL_NOT_ALLOWED'?say('C6-Test nicht erlaubt: Entwicklermodus, passendes P4-Board und bekannte C6-Version erforderlich','C6 trial denied: developer mode, supported P4 board and known C6 version required'):code==='C6_VERSION_UNAVAILABLE'?say('C6-Versionsabfrage fehlgeschlagen; keine Firmwaredaten geschrieben','C6 version query failed; no firmware data written'):code==='C6_CONTROL_UNAVAILABLE'?say('C6-Steuerabfrage fehlgeschlagen; keine Firmwaredaten geschrieben','C6 control query failed; no firmware data written'):code==='C6_TOO_OLD'?say('Aeltere C6-Firmware: Legacy-Modus oder UART verwenden','Older C6 firmware: use legacy mode or UART'):(de?errors:english)[code]||String(code||'');
function catalogText(){
 if(trialMode()){$('catalog_state').textContent=say('Experiment, keine Freigabe: C6 ','Experimental, not approved: C6 ')+ver(cfg.expected)+say(' / P4-Treiber bleibt 2.12.13',' / P4 driver stays 2.12.13');$('catalog_check').disabled=true;return}
 let text='';
 if(catalogBusy)text=say('Freigaben werden geprüft…','Checking approved releases…');
 else if(catalogError)text=say('Online-Prüfung nicht verfügbar. Lokale Freigabe bleibt nutzbar.','Online check unavailable. Built-in approval remains available.');
 else if(catalog&&last){try{
  const result=window.OFE_C6_CATALOG.inspect(catalog,cfg,last.host);
  if(result.kind==='approved'){
   if(!last.current)text=say('Installierte C6-Version noch nicht bestätigt.','Installed C6 version not yet confirmed.');
   else if(last.current<0x020600)text=describe('C6_TOO_OLD');
   else if(last.current===cfg.expected)text=say('Für dieses MCU-SDK aktuell: ','Up to date for this MCU SDK: ')+ver(cfg.expected);
   else if(last.current>cfg.expected)text=say('C6 neuer als lokale Freigabe; Downgrade gesperrt.','C6 newer than built-in approval; downgrade blocked.');
   else text=say('Passendes C6-Update verfügbar: ','Compatible C6 update available: ')+ver(cfg.expected);
  }else text=say('Keine passende Online-Freigabe. Lokale Freigabe: ','No matching online approval. Built-in approval: ')+ver(cfg.expected);
  if(result.newerSdk)text+=' · '+say('Weitere Freigabe benötigt ein aktualisiertes MCU-SDK.','Another approved release requires an updated MCU SDK.');
  else if(result.kind==='master_update')text+=' · '+say('Abweichende Freigabe benötigt eine aktualisierte MCU-Firmware.','Changed approval requires updated MCU firmware.');
 }catch(_){text=say('Ungültiger Updatekatalog. Lokale Freigabe bleibt nutzbar.','Invalid update catalog. Built-in approval remains available.');}}
 else if(catalog)text=say('MCU-Status nicht verfügbar.','MCU status unavailable.');
 if(catalogChecked&&!catalogBusy)text+=' · '+say('Geprüft: ','Checked: ')+catalogChecked;
 $('catalog_state').textContent=text;$('catalog_check').disabled=catalogBusy;
}
async function checkCatalog(){if(catalogBusy||trialMode())return;catalogBusy=true;catalogError='';catalogText();try{const data=await window.OFE_C6_CATALOG.load(cfg.catalog);window.OFE_C6_CATALOG.inspect(data,cfg,hostExpected);catalog=data;catalogChecked=new Date().toLocaleTimeString(de?'de-DE':'en-GB');}catch(e){catalog=null;catalogError=e.message;catalogChecked='';}finally{catalogBusy=false;catalogText();}}
function phaseText(s){if([0,2].includes(s.phase)&&s.current===cfg.expected)return describe('ALREADY_CURRENT');if([0,2].includes(s.phase)&&s.current>0&&s.current<0x020600)return describe('C6_TOO_OLD');return phases[s.phase]||'-';}
function controls(){const s=last,trial=trialMode(),busy=s&&s.phase>0&&s.phase!==6,needsLegacy=!trial&&s&&(!s.current||s.current<0x020600),trialDenied=trial&&(!s||!s.trial_allowed||(s.current<0x020600||s.current>0x02ffff)); $('download').disabled=upstreamBusy||running||busy||!s||!s.lan||s.other_busy||s.host!==hostExpected||trialDenied; $('file').disabled=$('download').disabled; $('cancel').disabled=running||!token||!s||![1,2].includes(s.phase);if(targetSelect){targetSelect.disabled=upstreamBusy||running||!!token||busy;if($('upstream_check'))$('upstream_check').disabled=upstreamBusy||running||!!token||busy;targetSelect.parentElement.hidden=!s?.trial_allowed}$('legacy').disabled=trial||running||!s||[3,4,5,7].includes(s.phase)||!needsLegacy;if(!needsLegacy)$('legacy').checked=false;$('install').disabled=running||!token||!s||s.phase!==2||!s.lan||s.other_busy||s.host!==hostExpected||trialDenied||(trial?s.current===cfg.expected:s.current>=cfg.expected)||s.expected!==cfg.expected||!!s.trial!==trial||!$('recovery').checked||(needsLegacy&&!$('legacy').checked);}
async function request(action,args={},body){const q=new URLSearchParams(args);const r=await fetch('/update/c6/'+action+'?'+q,{method:'POST',headers:{'X-CSRF-Token':cfg.csrf},body,cache:'no-store'});let s;try{s=await r.json()}catch(_){throw Error('HTTP '+r.status)}if(!r.ok||!s.ok)throw Error(describe(s.error));return s;}
)C6"
#define OFE_C6_UI_TAIL R"C6(async function refresh(){if(refreshing||document.hidden)return;refreshing=true;const observedToken=token;try{const r=await fetch('/update/c6/status?token='+observedToken,{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);last=await r.json();if(token===observedToken&&token&&!last.owned)saveToken(0);if(last.owned&&last.trial&&targetSelect&&last.developer_image){const key='upstream:'+last.expected+':'+last.commit;if(!upstreamTargets.has(key)){upstreamTargets.set(key,{expected:last.expected,size:last.size,commit:last.commit});targetSelect.add(new Option(ver(last.expected),key));}if(targetSelect.value!==key)selectTarget(key);$('upstream_state').textContent='SHA-256: '+(last.sha256||'-')+' / Commit: '+last.commit;}else if(last.owned&&last.trial&&targetSelect&&!trialMode())selectTarget(last.expected===cfg.test.expected?'test':'restore');$('current').textContent=ver(last.current);let text=phaseText(last);if(last.error!=='OK')text+=' · '+describe(last.error);if(!last.lan&&![3,4,5,7].includes(last.phase))text+=' · '+say('LAN erforderlich','Ethernet required');if(last.verification===1)text+=' · '+say('Version nach Neustart bestätigt','Version confirmed after restart');if(last.verification===2)text+=' · '+say('Letztes Update nicht bestätigt','Previous update unconfirmed');$('state').textContent=ownedError||text;const count=last.phase>=3?last.written:last.received;$('bar').style.width=Math.round(100*count/cfg.size)+'%';if(last.phase===5)rebooting=true;controls();catalogText();}catch(e){last=null;$('state').textContent=rebooting?say('MCU startet neu…','MCU restarting…'):e.message;controls();catalogText();}finally{refreshing=false}}
async function stage(file){if(running)return;running=true;ownedError='';controls();try{if(!file||file.size!==cfg.size)throw Error(describe('SIZE'));const begin=await request('begin',{size:file.size,...(upstreamMode()?{trial:'1',target:'upstream',risk:'1',version:cfg.expected,blob:cfg.blob,commit:cfg.commit}:trialMode()?{trial:'1',target:targetSelect.value==='test'?'test':'stable'}:{})});saveToken(begin.token);for(let offset=0;offset<file.size;){const end=Math.min(offset+16384,file.size),data=new FormData();data.append('firmware',file.slice(offset,end),'c6.bin');const reply=await request('chunk',{token,offset},data);if(reply.received!==end)throw Error(describe('OFFSET'));offset=end;$('bar').style.width=Math.round(100*offset/file.size)+'%';$('state').textContent=phases[1]+' '+Math.round(100*offset/file.size)+'%';}await request('seal',{token});}catch(e){ownedError=e.message;if(token){try{await request('cancel',{token})}catch(_){}saveToken(0);}}finally{running=false;await refresh();controls();}}
$('download').addEventListener('click',async()=>{if(running)return;running=true;ownedError='';controls();$('state').textContent=say('Firmware wird von Espressif geladen…','Loading firmware from Espressif…');let file;try{const r=await fetch(cfg.url,{credentials:'omit',referrerPolicy:'no-referrer',redirect:'error',signal:AbortSignal.timeout(60000)});if(!r.ok)throw Error('HTTP '+r.status);if(Number(r.headers.get('content-length'))>cfg.size||!r.body)throw Error(describe('SIZE'));const reader=r.body.getReader(),parts=[];let size=0;try{for(;;){const p=await reader.read();if(p.done)break;size+=p.value.length;if(size>cfg.size)throw Error(describe('SIZE'));parts.push(p.value);}}finally{await reader.cancel();reader.releaseLock();}file=new Blob(parts);if(file.size!==cfg.size)throw Error(describe('SIZE'));}catch(e){ownedError=e.message;$('state').textContent=ownedError;}finally{running=false;controls();}if(file)await stage(file);});
$('file').addEventListener('change',()=>stage($('file').files[0]));
$('recovery').addEventListener('change',controls);
$('legacy').addEventListener('change',controls);
if(targetSelect)targetSelect.addEventListener('change',()=>{selectTarget(targetSelect.value);ownedError='';$('file').value='';controls();catalogText()});
$('catalog_check').addEventListener('click',checkCatalog);
if($('upstream_check'))$('upstream_check').addEventListener('click',loadUpstream);
$('cancel').addEventListener('click',async()=>{try{await request('cancel',{token});saveToken(0);ownedError='';await refresh()}catch(e){$('state').textContent=e.message}});
$('install').addEventListener('click',async()=>{const legacy=$('legacy').checked,trial=trialMode();if($('install').disabled||!confirm(trial?say('Experimentellen C6-Wechsel starten? Ziel: ','Start experimental C6 switch? Target: ')+ver(cfg.expected)+(last?.current>cfg.expected?say(' (DOWNGRADE)',' (DOWNGRADE)'):'')+say('. P4-Treiber bleibt 2.12.13. Bei SDIO-Ausfall ist direkter C6-UART-Zugang erforderlich.','. P4 driver stays 2.12.13. SDIO failure requires direct C6 UART access.'):legacy?say('Legacy-C6-Wiederherstellung starten? Die installierte Version ist unbekannt oder alt. Ein Downgrade ist moeglich. UART-Wiederherstellung muss verfuegbar sein.','Start legacy C6 recovery? The installed version is unknown or old. A downgrade is possible. UART recovery must be available.'):say('C6 aktualisieren und danach die MCU neu starten? WLAN und Bluetooth werden unterbrochen.','Update C6 and restart the MCU afterwards? WiFi and Bluetooth will be interrupted.')))return;running=true;controls();try{await request('install',{token,recovery:'1',legacy:legacy?'1':'0',...(trial?{trial:'1',risk:'1'}:{})});saveToken(0);ownedError='';}catch(e){ownedError=e.message;}finally{running=false;await refresh();}});
document.addEventListener('visibilitychange',refresh);controls();refresh();checkCatalog();setInterval(refresh,1000);
})();</script>)C6"
