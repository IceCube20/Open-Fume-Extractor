#pragma once
static const char OFE_USB_UI[] = R"USBJS((()=>{
'use strict';
const root=document.getElementById('usb-storage');if(!root)return;
const de=document.getElementById('lang_sel')?.value!=='en';
const t=(a,b)=>de?a:b;
let status=null,busy=false,polling=false;
const purpose=root.dataset.usbPurpose;
root.innerHTML='<h2>USB</h2><div id="usb-status" role="status"></div><div id="usb-host-status" hidden class="muted" style="margin-top:8px;overflow-wrap:anywhere"></div><ul id="usb-host-devices" hidden style="padding-left:20px;margin:8px 0;overflow-wrap:anywhere"></ul><label for="usb-file"></label><select id="usb-file"></select><div class="actions" style="margin-top:12px;gap:8px"></div>';
const output=root.querySelector('#usb-status'),select=root.querySelector('select'),actions=root.querySelector('.actions');
root.querySelector('label').textContent=t('Datei','File');
const buttons=[];
function hostDiagnostics(d){
  const info=root.querySelector('#usb-host-status'),list=root.querySelector('#usb-host-devices');list.replaceChildren();
  info.hidden=list.hidden=!d;
  if(!d){info.textContent='';return}
  const yes=t('ja','yes'),no=t('nein','no');
  info.textContent='Host: '+(d.host_ready?yes:no)+' · '+t('Beobachter: ','Observer: ')+(d.observer_ready?yes:no)+' · '+t('MSC-Treiber: ','MSC driver: ')+(d.msc_ready?yes:no)+' · '+t('Root-Link: ','Root link: ')+(d.root_connected?yes:no)+' · '+t('Portfreigabe: ','Port enabled: ')+(d.root_powered?yes:no)+' · '+t('USB-Objekte: ','USB objects: ')+Number(d.allocated??d.enumerated??0)+' · '+t('Konfiguriert: ','Configured: ')+Number(d.configured??(d.devices||[]).length);
  for(const key of ['host_error','observer_error','msc_error','event_error'])if(d[key])info.textContent+=' · '+key+'=0x'+Number(d[key]).toString(16).toUpperCase();
  for(const dev of d.devices||[]){
    const item=document.createElement('li'),hex=n=>Number(n||0).toString(16).toUpperCase().padStart(4,'0');
    const kind=dev.error?t('Geräteinfos nicht lesbar','Device information unavailable')+' (0x'+Number(dev.error).toString(16).toUpperCase()+')':dev.hub?'Hub':dev.bot?t('Massenspeicher (SCSI/BOT)','Mass storage (SCSI/BOT)'):dev.storage?t('Massenspeicher (anderes Protokoll)','Mass storage (other protocol)'):t('Anderes USB-Gerät','Other USB device');
    item.textContent=t('USB-Adresse ','USB address ')+Number(dev.address)+' · '+hex(dev.vendor)+':'+hex(dev.product)+' · '+kind;list.appendChild(item);
  }
}
function show(text){output.textContent=text;}
async function api(path,params){const r=await fetch('/usb/'+path,{method:params?'POST':'GET',body:params?new URLSearchParams(params):undefined,cache:'no-store'});if(!r.ok)throw Error(await r.text());return r.json();}
function stamp(){return new Date().toISOString().replace(/[:.]/g,'-');}
function button(label,action,needsSelection=false){const b=document.createElement('button');b.type='button';b.className='secondary';b.textContent=label;b.dataset.selection=needsSelection?'1':'0';b.onclick=()=>run(action);actions.appendChild(b);buttons.push(b);return b;}
function render(s){
  const previous=select.value;status=s;select.replaceChildren();
  hostDiagnostics(s.developer_mode===true?s.diagnostics:null);
  for(const f of s.files||[]){const o=document.createElement('option');o.value=String(f.index);o.textContent=(f.folder?'OFE/':'')+f.name+' ('+Math.ceil(f.size/1024)+' KiB)';select.appendChild(o);}
  if([...select.options].some(o=>o.value===previous))select.value=previous;
  select.disabled=busy||!s.mounted;
  const labels={idle:t('Bereit','Ready'),queued:t('Wartet','Queued'),reading:t('Wird gelesen','Reading'),ready:t('Datei bereit','File ready'),receiving:t('Wird gepuffert','Buffering'),writing:t('Wird gespeichert','Writing'),error:t('USB-Fehler','USB error')};
  if(!busy)show(s.phase==='error'?s.message:!s.detected?t('Kein USB-Stick','No USB stick'):!s.mounted?t('Erkannt, nicht eingebunden','Detected, not mounted'):(labels[s.phase]||'USB')+(s.truncated?t(' · Liste auf 32 Dateien begrenzt',' · List limited to 32 files'):''));
  for(const b of buttons)b.disabled=busy||!s.mounted||(b.dataset.selection==='1'&&!select.options.length);
}
async function poll(){if(polling||document.hidden||busy)return;polling=true;try{render(await api('status'));}catch(e){show(String(e));}finally{polling=false;}}
async function wait(phase){const until=Date.now()+120000;while(Date.now()<until){const s=await api('status');if(s.phase==='error'||!s.mounted)throw Error(s.message||t('USB getrennt','USB disconnected'));if(s.phase===phase)return s;await new Promise(r=>setTimeout(r,200));}throw Error(t('USB-Zeitlimit','USB timeout'));}
async function run(action){if(busy)return;busy=true;let error='';if(status)render(status);try{await action();}catch(e){error=String(e);}finally{busy=false;await poll();if(error)show(error);}}
async function selected(){
  const s=status,f=s?.files?.find(f=>String(f.index)===select.value);if(!f)throw Error(t('Keine Datei','No file'));
  await api('command',{action:'read',generation:s.generation,index:f.index});
  let token=0;
  try{const ready=await wait('ready');token=ready.token;const r=await fetch('/usb/file?token='+token,{cache:'no-store'});if(!r.ok)throw Error(await r.text());const blob=await r.blob();if(blob.size!==f.size)throw Error('USB file size mismatch');return new File([blob],f.name,{type:'application/octet-stream'});}
  finally{if(token)await api('release',{token}).catch(()=>{});}
}
async function save(blob,name){
  let token=0;try{
    const start=await api('write/begin',{name,size:blob.size});token=start.token;
    for(let offset=0;offset<blob.size;offset+=8192){const data=new FormData();data.append('chunk',blob.slice(offset,offset+8192),'chunk.bin');const r=await fetch('/usb/write/chunk?token='+token+'&offset='+offset,{method:'POST',body:data,cache:'no-store'});if(!r.ok)throw Error(await r.text());show(t('USB speichern: ','USB saving: ')+Math.round(Math.min(blob.size,offset+8192)*100/blob.size)+'%');}
    await api('write/commit',{token});await wait('idle');token=0;show(t('Auf USB gespeichert','Saved to USB'));
  }finally{if(token)await api('release',{token}).catch(()=>{});}
}
window.ofeUsbSave=(blob,name)=>run(()=>save(blob,name));
button(t('Aktualisieren','Refresh'),async()=>{await api('command',{action:'refresh'});await wait('idle');});
button(t('Auswerfen','Eject'),async()=>{await api('command',{action:'eject'});});
button(t('Herunterladen','Download'),async()=>{const file=await selected(),a=document.createElement('a'),url=URL.createObjectURL(file);a.href=url;a.download=file.name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);},true);
if(purpose==='config'){
  button(t('Backup auf USB','Backup to USB'),async()=>{if(!confirm(t('Das Backup enthält Zugangsdaten. Auf diesem USB-Stick speichern?','The backup contains credentials. Save to this USB stick?')))return;const r=await fetch('/config/export',{cache:'no-store'});if(!r.ok)throw Error(await r.text());await save(await r.blob(),'open-fume-extractor-backup-'+stamp()+'.json');});
  button(t('Backup auswählen','Select backup'),async()=>{const file=await selected();if(!/\.json$/i.test(file.name))throw Error('JSON required');await loadBackupFile(file);document.getElementById('backup_import_btn')?.scrollIntoView({block:'center'});},true);
}
if(purpose==='update'){
  const label=document.createElement('label');label.htmlFor='usb-target';label.textContent=t('Update-Ziel','Update target');root.insertBefore(label,actions);
  const target=document.createElement('select');target.id='usb-target';for(const [value,name] of [['master',t('MCU','MCU')],['module',t('Modul','Module')]]){const o=document.createElement('option');o.value=value;o.textContent=name;target.appendChild(o);}root.insertBefore(target,actions);
  button(t('Firmware auswählen','Select firmware'),async()=>{const file=await selected();if(!/\.bin$/i.test(file.name))throw Error('BIN required');const input=document.getElementById(target.value==='master'?'master_fw_file':'module_fw_file');if(!input||typeof DataTransfer==='undefined')throw Error(t('Datei herunterladen und im Updatefeld auswählen','Download file and select it in the update form'));const data=new DataTransfer();data.items.add(file);input.files=data.files;input.dispatchEvent(new Event('change',{bubbles:true}));input.scrollIntoView({block:'center'});},true);
}
if(purpose==='trace'){
  button('CSV > USB',()=>{}).onclick=()=>diagDownload('csv',true);
  button('JSON > USB',()=>{}).onclick=()=>diagDownload('json',true);
  button(t('Diagnose auf USB','Diagnostics to USB'),async()=>{const r=await fetch('/diagnostics/state',{cache:'no-store'});if(!r.ok)throw Error(await r.text());await save(await r.blob(),'open-fume-extractor-diagnostics-'+stamp()+'.json');});
}
poll();setInterval(poll,3000);document.addEventListener('visibilitychange',poll);
})();)USBJS";
