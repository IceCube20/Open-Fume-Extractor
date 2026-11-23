#pragma once
static const char OFE_USB_UI[] = R"USBJS((()=>{
'use strict';
if(window.ofeUsbPick)return;
let root=document.getElementById('usb-storage'),picker=null,pending=null,returnFocus=null;
if(!root){
  picker=document.createElement('dialog');picker.id='usb-picker';picker.setAttribute('aria-labelledby','usb-picker-title');
  picker.innerHTML='<div class="usb-picker-head"><h2 id="usb-picker-title"></h2><button type="button" class="secondary usb-picker-close"></button></div><section id="usb-storage" data-usb-purpose="picker"></section><form class="usb-picker-footer"><label hidden for="usb-picker-name"></label><input hidden id="usb-picker-name" autocomplete="off" data-no-dirty="1"><div class="usb-picker-error" role="status"></div><div class="usb-picker-buttons"><button type="button" class="secondary usb-picker-cancel"></button><button type="submit" class="usb-picker-confirm"></button></div></form>';
  document.body.appendChild(picker);root=picker.querySelector('#usb-storage');
}
const de=document.getElementById('lang_sel')?.value!=='en';
const t=(a,b)=>de?a:b;
let status=null,busy=false,polling=false,selection='',listSignature='',notice='',noticeError=false;
let activeDirectory='',exportPending=false,downloadToken=0,driveSignature='';const openDirectories=new Set(['']);
const purpose=root.dataset.usbPurpose;
// Material Design Icons / Pictogrammers, Apache-2.0; docs/licenses/MaterialDesign-USB.txt.
const paths={
 folder:'M20,18H4V8H20M20,6H12L10,4H4C2.89,4 2,4.89 2,6V18A2,2 0 0,0 4,20H20A2,2 0 0,0 22,18V8C22,6.89 21.1,6 20,6Z',
 folderOpen:'M6.1,10L4,18V8H21A2,2 0 0,0 19,6H12L10,4H4A2,2 0 0,0 2,6V18A2,2 0 0,0 4,20H19C19.9,20 20.7,19.4 20.9,18.5L23.2,10H6.1M19,18H6L7.6,12H20.6L19,18Z',
 right:'M8.59,16.58L13.17,12L8.59,7.41L10,6L16,12L10,18L8.59,16.58Z',down:'M7.41,8.58L12,13.17L16.59,8.58L18,10L12,16L6,10L7.41,8.58Z',
 folderPlus:'M13 19C13 19.34 13.04 19.67 13.09 20H4C2.9 20 2 19.11 2 18V6C2 4.89 2.89 4 4 4H10L12 6H20C21.1 6 22 6.89 22 8V13.81C21.39 13.46 20.72 13.22 20 13.09V8H4V18H13.09C13.04 18.33 13 18.66 13 19M20 18V15H18V18H15V20H18V23H20V20H23V18H20Z',
 upload:'M9,16V10H5L12,3L19,10H15V16H9M5,20V18H19V20H5Z',
 move:'M14 2H6C4.9 2 4 2.9 4 4V20C4 20.41 4.12 20.8 4.34 21.12C4.41 21.23 4.5 21.33 4.59 21.41C4.95 21.78 5.45 22 6 22H13.53C13 21.42 12.61 20.75 12.35 20H6V4H13V9H18V12C18.7 12 19.37 12.12 20 12.34V8L14 2M18 23L23 18.5L20 15.8L18 14V17H14V20H18V23Z',
 trash:'M9,3V4H4V6H5V19A2,2 0 0,0 7,21H17A2,2 0 0,0 19,19V6H20V4H15V3H9M7,6H17V19H7V6M9,8V17H11V8H9M13,8V17H15V8H13Z',
 expand:'M12,18.17L8.83,15L7.42,16.41L12,21L16.59,16.41L15.17,15M12,5.83L15.17,9L16.58,7.59L12,3L7.41,7.59L8.83,9L12,5.83Z',
 usb:'M8 15C8.55 15 9 15.45 9 16C9 16.55 8.55 17 8 17C7.45 17 7 16.55 7 16C7 15.45 7.45 15 8 15M15.07 4.69L16.5 6.1L15.07 7.5L13.66 6.1L15.07 4.69M17.9 7.5L19.31 8.93L17.9 10.34L16.5 8.93L17.9 7.5M8 13C6.34 13 5 14.34 5 16C5 17.66 6.34 19 8 19C9.66 19 11 17.66 11 16C11 14.34 9.66 13 8 13M9.77 4.33L10.5 5.08L14.29 1.29C14.47 1.11 14.72 1 15 1C15.28 1 15.53 1.11 15.71 1.29L22.78 8.36L22.78 8.37C22.92 8.54 23 8.76 23 9C23 9.3 22.87 9.57 22.66 9.76L22.66 9.76L18.93 13.5L19.67 14.23L12.95 20.95C11.68 22.22 9.93 23 8 23C4.13 23 1 19.87 1 16C1 14.07 1.78 12.32 3.05 11.05L9.77 4.33M20.59 9L15 3.41L11.93 6.5L17.5 12.08L20.59 9Z',
 refresh:'M17.65,6.35C16.2,4.9 14.21,4 12,4A8,8 0 0,0 4,12A8,8 0 0,0 12,20C15.73,20 18.84,17.45 19.73,14H17.65C16.83,16.33 14.61,18 12,18A6,6 0 0,1 6,12A6,6 0 0,1 12,6C13.66,6 15.14,6.69 16.22,7.78L13,11H20V4L17.65,6.35Z',
 eject:'M12,5L5.33,15H18.67M5,17H19V19H5V17Z',download:'M5,20H19V18H5M19,9H15V3H9V9H5L12,16L19,9Z',
 search:'M9.5,3A6.5,6.5 0 0,1 16,9.5C16,11.11 15.41,12.59 14.44,13.73L14.71,14H15.5L20.5,19L19,20.5L14,15.5V14.71L13.73,14.44C12.59,15.41 11.11,16 9.5,16A6.5,6.5 0 0,1 3,9.5A6.5,6.5 0 0,1 9.5,3M9.5,5C7,5 5,7 5,9.5C5,12 7,14 9.5,14C12,14 14,12 14,9.5C14,7 12,5 9.5,5Z',
 close:'M19,6.41L17.59,5L12,10.59L6.41,5L5,6.41L10.59,12L5,17.59L6.41,19L12,13.41L17.59,19L19,17.59L13.41,12L19,6.41Z',
 firmware:'M14 2H6C4.89 2 4 2.9 4 4V20C4 21.11 4.89 22 6 22H18C19.11 22 20 21.11 20 20V8L14 2M18 20H6V4H13V9H18V20M9.54 15.65L11.63 17.74L10.35 19L7 15.65L10.35 12.3L11.63 13.56L9.54 15.65M17 15.65L13.65 19L12.38 17.74L14.47 15.65L12.38 13.56L13.65 12.3L17 15.65Z',
 backup:'M12,3A9,9 0 0,0 3,12H0L4,16L8,12H5A7,7 0 0,1 12,5A7,7 0 0,1 19,12A7,7 0 0,1 12,19C10.5,19 9.09,18.5 7.94,17.7L6.5,19.14C8.04,20.3 9.94,21 12,21A9,9 0 0,0 21,12A9,9 0 0,0 12,3M14,12A2,2 0 0,0 12,10A2,2 0 0,0 10,12A2,2 0 0,0 12,14A2,2 0 0,0 14,12Z',
 file:'M6,2A2,2 0 0,0 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2H6M6,4H13V9H18V20H6V4M8,12V14H16V12H8M8,16V18H13V16H8Z',
 profile:'M8 13C6.14 13 4.59 14.28 4.14 16H2V18H4.14C4.59 19.72 6.14 21 8 21S11.41 19.72 11.86 18H22V16H11.86C11.41 14.28 9.86 13 8 13M8 19C6.9 19 6 18.1 6 17C6 15.9 6.9 15 8 15S10 15.9 10 17C10 18.1 9.1 19 8 19M19.86 6C19.41 4.28 17.86 3 16 3S12.59 4.28 12.14 6H2V8H12.14C12.59 9.72 14.14 11 16 11S19.41 9.72 19.86 8H22V6H19.86M16 9C14.9 9 14 8.1 14 7C14 5.9 14.9 5 16 5S18 5.9 18 7C18 8.1 17.1 9 16 9Z',
 save:'M17 3H5C3.89 3 3 3.9 3 5V19C3 20.1 3.89 21 5 21H19C20.1 21 21 20.1 21 19V7L17 3M19 19H5V5H16.17L19 7.83V19M12 12C10.34 12 9 13.34 9 15S10.34 18 12 18 15 16.66 15 15 13.66 12 12 12M6 6H15V10H6V6Z'
};
const icon=name=>'<svg aria-hidden="true" viewBox="0 0 24 24" fill="currentColor"><path d="'+paths[name]+'"/></svg>';
root.innerHTML=`<style>
#usb-picker{width:min(900px,calc(100% - 24px));max-width:100%;max-height:calc(100dvh - 24px);overflow:auto;padding:20px;background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:8px}#usb-picker::backdrop{background:rgba(0,0,0,.7)}
#usb-picker .usb-picker-head{display:flex;align-items:center;gap:12px;margin-bottom:16px}#usb-picker h2{flex:1;font-size:18px;margin:0}#usb-picker .usb-picker-close{width:40px;height:40px;padding:8px}#usb-picker svg{width:20px;height:20px}
#usb-picker .usb-picker-footer{border-top:1px solid var(--line);margin-top:12px;padding-top:12px}#usb-picker .usb-picker-buttons{display:flex;justify-content:flex-end;gap:8px;margin-top:12px;flex-wrap:wrap}#usb-picker .usb-picker-error{color:#f28b82;overflow-wrap:anywhere;font-size:13px}#usb-picker .usb-files{max-height:min(340px,40dvh)}
#usb-storage[data-usb-purpose=picker] .usb-heading,#usb-storage[data-usb-purpose=picker] .usb-drive,#usb-storage[data-usb-purpose=picker] .usb-global,#usb-storage[data-usb-purpose=picker] .usb-actions{display:none}#usb-storage[data-usb-purpose=picker] .usb-head{justify-content:flex-end}body:has(#usb-picker[open]){overflow:hidden}
.ofe-usb-action{margin:0;gap:6px;min-height:40px;line-height:1.25;white-space:normal;text-align:center}.ofe-usb-action svg,.ofe-usb-nav svg{width:18px;height:18px;flex-shrink:0}.ofe-usb-nav{white-space:nowrap}
#usb-storage{min-width:0}#usb-storage [hidden]{display:none!important}#usb-storage svg{width:20px;height:20px;flex-shrink:0}
#usb-storage .usb-head{display:flex;align-items:center;gap:12px;margin-bottom:16px}#usb-storage .usb-drive{width:30px;height:30px;color:var(--green)}
#usb-storage .usb-heading{flex:1;min-width:0}#usb-storage h2{margin:0 0 4px}#usb-storage .usb-connection{display:flex;align-items:center;gap:7px;font-size:12px;color:var(--muted)}
#usb-storage .usb-dot{width:7px;height:7px;border-radius:50%;background:var(--muted);flex-shrink:0}#usb-storage[data-connection=ready] .usb-dot{background:var(--green)}#usb-storage[data-connection=detected] .usb-dot{background:#f4c25b}
#usb-storage .usb-tools{display:flex;gap:6px}#usb-storage .usb-icon-button{width:40px;height:40px;padding:8px;flex-shrink:0}
#usb-storage .usb-drive-picker{margin-bottom:14px;min-width:0}#usb-storage .usb-drive-picker select{width:100%;min-width:0;max-width:100%;font-size:13px}
#usb-storage button:disabled{opacity:.45;cursor:not-allowed}#usb-storage button:focus-visible{outline:2px solid var(--blue);outline-offset:2px}
#usb-storage .usb-toolbar{display:grid;grid-template-columns:minmax(0,1fr) 150px 170px;gap:10px;align-items:end;margin-bottom:14px}#usb-storage label{margin:0 0 6px}#usb-storage .usb-toolbar select{font-size:13px;padding:10px 8px}
#usb-storage .usb-search{position:relative}#usb-storage .usb-search>svg{position:absolute;left:11px;top:11px;color:var(--muted)}#usb-storage .usb-search input{padding-left:38px;padding-right:40px}
#usb-storage .usb-clear{position:absolute;right:1px;top:1px;width:38px;height:40px;min-height:0;background:transparent;border:0;padding:8px;color:var(--muted)}
#usb-storage .usb-list-head,#usb-storage .usb-row{display:grid;grid-template-columns:minmax(0,1fr) 110px 90px;gap:12px;align-items:center}
#usb-storage .usb-list-head{padding:9px 10px;color:var(--muted);font-size:12px;border-bottom:1px solid var(--line)}#usb-storage .usb-list-head span:last-child{text-align:right}
#usb-storage .usb-files{max-height:420px;overflow:auto;overscroll-behavior:contain}#usb-storage .usb-row{width:100%;min-height:58px;padding:11px 10px;border:0;border-bottom:1px solid var(--line);border-radius:0;background:transparent;text-align:left;font-weight:400;color:var(--text);transform:none}
#usb-storage .usb-row:hover{background:var(--card2)}#usb-storage .usb-row[aria-pressed=true]{background:rgba(74,144,217,.13);box-shadow:inset 3px 0 var(--blue)}
#usb-storage .usb-name{display:flex;align-items:center;gap:10px;min-width:0}#usb-storage .usb-name svg{color:var(--blue)}#usb-storage .usb-path{min-width:0;overflow-wrap:anywhere;font-size:13px;line-height:1.4}#usb-storage .usb-path small{color:var(--muted);font-size:11px}
#usb-storage .usb-kind,#usb-storage .usb-size{color:var(--muted);font-size:12px}#usb-storage .usb-size{text-align:right;font-variant-numeric:tabular-nums}
#usb-storage .usb-empty{padding:28px 12px;text-align:center;color:var(--muted);font-size:13px}#usb-storage .usb-count{font-size:12px;color:var(--muted);padding-top:10px;min-height:28px}
#usb-storage .usb-selection{display:flex;align-items:center;gap:12px;flex-wrap:wrap;padding:12px 0;margin-top:8px;border-top:1px solid var(--line)}#usb-storage .usb-selected-name{flex:1;min-width:120px;overflow-wrap:anywhere;font-size:13px;color:var(--muted)}
#usb-storage .usb-target{display:flex;align-items:center;gap:8px}#usb-storage .usb-target label{margin:0}#usb-storage .usb-target select{width:145px}
#usb-storage .usb-actions{display:flex;align-items:center;gap:8px;flex-wrap:wrap}#usb-storage .usb-global{display:flex;gap:8px;flex-wrap:wrap;border-top:1px solid var(--line);padding-top:12px}
#usb-storage .usb-feedback{margin-top:12px;min-height:20px;font-size:13px;line-height:1.4;overflow-wrap:anywhere}#usb-storage .usb-feedback[data-error=true]{color:#f28b82}#usb-storage .usb-feedback[data-success=true]{color:var(--green)}
#usb-storage .usb-progress{margin-top:10px}#usb-storage .usb-progress-line{display:flex;justify-content:space-between;gap:10px;font-size:12px;color:var(--muted);margin-bottom:7px}#usb-storage .usb-progress-name{overflow-wrap:anywhere;min-width:0}#usb-storage .usb-progress-track{height:5px;background:var(--card2);overflow:hidden;border-radius:3px}#usb-storage .usb-progress-fill{height:100%;width:0;background:var(--green)}
#usb-storage .usb-progress[data-indeterminate=true] .usb-progress-fill{width:35%;animation:usb-progress 1.2s ease-in-out infinite alternate}@keyframes usb-progress{to{transform:translateX(185%)}}
#usb-storage .usb-diagnostics{margin-top:14px;border-top:1px solid var(--line);padding-top:10px;font-size:12px;overflow-wrap:anywhere}#usb-storage .usb-diagnostics summary{cursor:pointer;color:var(--muted)}
#usb-storage .usb-directory{display:grid;grid-template-columns:minmax(0,1fr) 110px 90px;gap:12px;align-items:center;width:100%;min-height:48px;padding:10px;border:0;border-bottom:1px solid var(--line);border-radius:0;background:transparent;color:var(--text);text-align:left;transform:none;font-weight:400}
#usb-storage .usb-directory:hover{background:var(--card2)}#usb-storage .usb-directory[aria-pressed=true]{background:rgba(74,144,217,.13);box-shadow:inset 3px 0 var(--blue)}#usb-storage .usb-directory .usb-name>svg{color:#f4c25b}#usb-storage .usb-chevron{display:flex;width:16px;flex-shrink:0}#usb-storage .usb-chevron svg{width:16px;height:16px;color:var(--muted)}
#usb-storage .usb-row,#usb-storage .usb-directory{padding-left:calc(10px + var(--usb-depth,0)*12px)}#usb-storage .usb-row .usb-name{padding-left:26px}
#usb-storage .usb-dialog{width:min(520px,calc(100% - 24px));max-width:100%;background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:20px}#usb-storage .usb-dialog::backdrop{background:rgba(0,0,0,.65)}#usb-storage .usb-dialog form{display:grid;gap:14px;min-width:0}#usb-storage .usb-dialog h3{margin:0;font-size:18px}#usb-storage .usb-dialog input,#usb-storage .usb-dialog select{min-width:0;max-width:100%}#usb-storage .usb-dialog-path{color:var(--muted);font-size:12px;overflow-wrap:anywhere}#usb-storage .usb-dialog-error{color:#f28b82;font-size:13px;min-height:18px}#usb-storage .usb-dialog-actions{display:flex;justify-content:flex-end;gap:8px}
#usb-storage .usb-dialog-fields{display:grid;gap:12px}#usb-storage .usb-dialog-error:empty{display:none}
@media(max-width:650px){#usb-storage .usb-toolbar{grid-template-columns:1fr 1fr}#usb-storage .usb-search-field{grid-column:1/-1}#usb-storage .usb-list-head{display:none}#usb-storage .usb-row{grid-template-columns:minmax(0,1fr) auto;gap:5px 10px}#usb-storage .usb-name{grid-column:1/-1}#usb-storage .usb-kind{padding-left:30px}#usb-storage .usb-selection{align-items:stretch}#usb-storage .usb-selected-name{flex-basis:100%}#usb-storage .usb-actions{width:100%}#usb-storage .usb-actions button:not(.usb-icon-button){flex:1}#usb-storage .usb-global button{flex:1}#usb-storage .usb-target{margin-left:auto}}
@media(prefers-reduced-motion:reduce){#usb-storage .usb-progress-fill{animation:none!important}}
@media(max-width:650px){#usb-storage .usb-directory{grid-template-columns:minmax(0,1fr) auto;gap:5px 10px}#usb-storage .usb-directory .usb-kind{padding-left:52px}#usb-storage .usb-row .usb-kind{padding-left:56px}#usb-storage .usb-dialog{padding:16px}#usb-storage .usb-actions{align-items:stretch}}
</style><div class="usb-head">${icon('usb').replace('<svg ','<svg class="usb-drive" ')}<div class="usb-heading"><h2>USB</h2><div class="usb-connection"><span class="usb-dot"></span><span id="usb-connection"></span></div></div><div class="usb-tools"></div></div>
<div class="usb-drive-picker" hidden><label for="usb-drive">${t('USB-Laufwerk','USB drive')}</label><select id="usb-drive" data-no-dirty="1"></select></div>
<div class="usb-toolbar"><div class="usb-search-field"><label for="usb-search">${t('Suche','Search')}</label><div class="usb-search">${icon('search')}<input id="usb-search" type="search" autocomplete="off" data-no-dirty="1"><button class="usb-clear" type="button" hidden>${icon('close')}</button></div></div><div><label for="usb-kind">${t('Dateityp','File type')}</label><select id="usb-kind" data-no-dirty="1"></select></div><div><label for="usb-sort">${t('Sortierung','Sort')}</label><select id="usb-sort" data-no-dirty="1"></select></div></div>
<div class="usb-list-head" aria-hidden="true"><span>${t('Dateiname','File name')}</span><span>${t('Typ','Type')}</span><span>${t('Gr\u00f6\u00dfe','Size')}</span></div><div id="usb-file" class="usb-files" role="group" aria-label="${t('Dateien','Files')}"></div><div id="usb-count" class="usb-count"></div>
<div class="usb-selection"><div id="usb-selected" class="usb-selected-name"></div><div class="usb-target" hidden></div><div class="usb-actions"></div></div><div class="usb-global"></div>
<div id="usb-status" class="usb-feedback" role="status" aria-live="polite"></div><div class="usb-progress" hidden><div class="usb-progress-line"><span class="usb-progress-name"></span><span class="usb-progress-value"></span></div><div class="usb-progress-track" role="progressbar"><div class="usb-progress-fill"></div></div></div>
<details class="usb-diagnostics" hidden><summary>${t('USB-Diagnose','USB diagnostics')}</summary><div id="usb-host-status" hidden class="muted" style="margin-top:8px"></div><ul id="usb-host-devices" hidden style="padding-left:20px;margin:8px 0"></ul></details>
<dialog id="usb-dialog" class="usb-dialog"><form><h3></h3><div class="usb-dialog-fields"></div><div class="usb-dialog-error" role="status"></div><div class="usb-dialog-actions"><button type="button" class="secondary usb-dialog-cancel">${t('Abbrechen','Cancel')}</button><button type="submit" class="usb-dialog-submit"></button></div></form></dialog>`;
const $=id=>root.querySelector('#usb-'+id),output=$('status'),list=$('file'),search=$('search'),filter=$('kind'),sort=$('sort'),actions=root.querySelector('.usb-actions');
const groups={all:t('Alle Dateien','All files'),firmware:'Firmware',backup:t('Sicherungen','Backups'),logs:t('Protokolle','Logs'),profile:t('Profile','Profiles'),data:t('Daten','Data')};
for(const [value,name] of Object.entries(groups))filter.add(new Option(name,value));
for(const [value,name] of [['name','Name A-Z'],['name-desc','Name Z-A'],['size',t('Gr\u00f6\u00dfe \u2191','Size \u2191')],['size-desc',t('Gr\u00f6\u00dfe \u2193','Size \u2193')]])sort.add(new Option(name,value));
const filePath=f=>typeof f.path==='string'?f.path:(f.folder?'OFE/':'')+f.name;
const key=f=>JSON.stringify([filePath(f),f.size]);
const chosen=()=>status?.mounted?(status.files||[]).find(f=>key(f)===selection):null;
const parentPath=p=>p.includes('/')?p.slice(0,p.lastIndexOf('/')):'';
const directories=()=>status?.mounted?(status.directories||[{index:0,path:''},...((status.files||[]).some(f=>f.folder)?[{index:1,path:'OFE'}]:[])]):[];
const selectedDirectory=()=>directories().find(d=>d.path===activeDirectory)||directories().find(d=>!d.path);
function category(f){return /\.bin$/i.test(f.name)?'firmware':/\.ofeprofile$/i.test(f.name)?'profile':/\.json$/i.test(f.name)&&/backup|sicherung/i.test(f.name)?'backup':/\.(csv|log|txt|ndjson)$/i.test(f.name)||/trace|diagnostic/i.test(f.name)?'logs':'data';}
function size(bytes){const unit=bytes<1024?0:bytes<1048576?1:bytes<1073741824?2:3;return new Intl.NumberFormat(de?'de-DE':'en-GB',{maximumFractionDigits:1}).format(bytes/1024**unit)+[' B',' KiB',' MiB',' GiB'][unit];}
const buttons=[];
const transferLimit=()=>Math.min(status?.transfer_limit??2147483647,2147483647);
const context=(s=status)=>({drive:s?.drive,drive_instance:s?.drive_instance,generation:s?.generation});
const sameDrive=(s,c)=>s?.drive===c.drive&&s?.drive_instance===c.drive_instance;
const sameContext=(s,c)=>sameDrive(s,c)&&s?.generation===c.generation;
const transferActive=s=>!!s?.token||['queued','reading','writing','receiving','ready'].includes(s?.phase);
const driveLocked=()=>busy||exportPending||!!downloadToken||!status||transferActive(status);
const stale=()=>Error(t('USB-Laufwerk oder Dateiliste hat sich ge\u00e4ndert. Bitte erneut ausw\u00e4hlen.','USB drive or file list changed. Select again.'));
function requireContext(s,c){if(!s?.mounted||!sameContext(s,c))throw stale();}
const driveLabel=d=>d?(d.volume_name||d.label||'USB '+(d.index+1)):t('USB-Stick','USB stick');
function drivePicker(){
  const picker=$('drive'),drives=(status?.drives||[]).filter(d=>d.connected),signature=JSON.stringify(drives);
  picker.parentElement.hidden=!drives.length;
  if(signature!==driveSignature){
    driveSignature=signature;picker.replaceChildren(new Option(t('Laufwerk ausw\u00e4hlen','Select drive'),''));picker.options[0].disabled=true;
    for(const d of drives){
      const label=driveLabel(d)+' \u00b7 '+t('Anschluss ','Slot ')+(d.index+1)+(d.capacity_bytes?' \u00b7 '+size(d.capacity_bytes):'')+(d.mounted?'':t(' \u00b7 nicht eingebunden',' \u00b7 not mounted'));
      picker.add(new Option(label,JSON.stringify([d.index,d.instance])));
    }
  }
  picker.value=status?.mounted?JSON.stringify([status.drive,status.drive_instance]):'';
  picker.title=picker.selectedOptions[0]?.textContent||'';picker.disabled=driveLocked();
}
function controls(){
  const f=chosen(),locked=driveLocked()||!status?.mounted;drivePicker();
  for(const b of buttons){b.hidden=!!b.accept&&!b.accept(f);b.disabled=locked||(b.dataset.selection==='1'&&!f)||(b.unavailable&&b.unavailable())||(b.transfer&&f&&f.size>transferLimit());}
  for(const row of list.querySelectorAll('button')){row.disabled=locked;row.setAttribute('aria-pressed',String(row.dataset.directory!==undefined?!selection&&row.dataset.directory===activeDirectory:row.dataset.key===selection));}
  $('selected').textContent=f?filePath(f):t('Ordner: /','Folder: /')+(selectedDirectory()?.path||'');
  root.querySelector('.usb-target').hidden=purpose!=='update'||!f||category(f)!=='firmware';
  const target=$('target');if(target)target.disabled=locked;
  const dirs=directories(),allOpen=dirs.length>1&&dirs.every(d=>openDirectories.has(d.path));
  expand.title=expand.ariaLabel=allOpen?t('Alle Ordner zuklappen','Collapse all folders'):t('Alle Ordner aufklappen','Expand all folders');expand.disabled=locked||dirs.length<=1;
  if(picker&&pending)picker.querySelector('.usb-picker-confirm').disabled=locked||pending.invalid||(pending.mode==='read'&&(!f||!accepts(f,pending.accept)||f.size>transferLimit()));
}
function files(){
  const all=status?.mounted?(status.files||[]):[],dirs=directories(),query=search.value.trim().toLocaleLowerCase(de?'de':'en');
  const rootLabel=driveLabel(status?.drives?.find(d=>d.connected&&d.index===status.drive&&d.instance===status.drive_instance));
  const visible=all.filter(f=>(!pending||pending.mode!=='read'||accepts(f,pending.accept))&&(filter.value==='all'||category(f)===filter.value)&&filePath(f).toLocaleLowerCase(de?'de':'en').includes(query));
  const collator=new Intl.Collator(de?'de':'en',{numeric:true,sensitivity:'base'}),descending=sort.value.endsWith('desc')?-1:1;
  visible.sort((a,b)=>descending*((sort.value.startsWith('size')?a.size-b.size:0)||collator.compare(filePath(a),filePath(b))));
  const relevant=new Set(['']);
  function ancestors(path){for(;;){relevant.add(path);if(!path)break;path=parentPath(path);}}
  for(const f of visible)ancestors(parentPath(filePath(f)));
  if(filter.value==='all')for(const d of dirs)if(!query||d.path.toLocaleLowerCase(de?'de':'en').includes(query))ancestors(d.path);
  const signature=JSON.stringify([status?.drive,status?.drive_instance,status?.detected,status?.mounted,rootLabel,all,dirs,search.value,filter.value,sort.value,[...openDirectories].sort()]);
  if(signature!==listSignature){
    listSignature=signature;const fragment=document.createDocumentFragment();
    function fileRow(f,depth){
      const row=document.createElement('button');row.type='button';row.className='usb-row';row.dataset.key=key(f);
      row.style.setProperty('--usb-depth',String(depth));
      const kind=category(f),symbol=paths[kind]?kind:'file';row.innerHTML='<span class="usb-name">'+icon(symbol)+'<span class="usb-path"></span></span><span class="usb-kind"></span><span class="usb-size"></span>';
      row.querySelector('.usb-path').append(document.createTextNode(f.name));
      row.querySelector('.usb-kind').textContent=groups[kind];row.querySelector('.usb-size').textContent=size(f.size);
      row.title=filePath(f);row.onclick=()=>{selection=key(f);activeDirectory=parentPath(filePath(f));controls();};fragment.appendChild(row);
    }
    function visit(path,depth){
      if(depth>6||!relevant.has(path))return;
      const row=document.createElement('button');row.type='button';row.className='usb-directory';row.dataset.directory=path;row.style.setProperty('--usb-depth',String(depth));
      const expanded=openDirectories.has(path);row.setAttribute('aria-expanded',String(expanded));row.title='/'+path;
      row.innerHTML='<span class="usb-name"><span class="usb-chevron">'+icon(expanded?'down':'right')+'</span>'+icon(expanded?'folderOpen':'folder')+'<span class="usb-path"></span></span><span class="usb-kind"></span><span class="usb-size"></span>';
      row.querySelector('.usb-path').textContent=path?path.slice(path.lastIndexOf('/')+1):rootLabel;
      row.querySelector('.usb-kind').textContent=t('Ordner','Folder');
      const count=all.filter(f=>!path||filePath(f).startsWith(path+'/')).length;row.querySelector('.usb-size').textContent=count+' '+(count===1?t('Datei','file'):t('Dateien','files'));
      row.onclick=()=>{expanded?openDirectories.delete(path):openDirectories.add(path);activeDirectory=path;selection='';files();[...list.querySelectorAll('.usb-directory')].find(e=>e.dataset.directory===path)?.focus();};fragment.appendChild(row);
      if(!expanded)return;
      const children=dirs.filter(d=>d.path&&parentPath(d.path)===path&&relevant.has(d.path)).sort((a,b)=>collator.compare(a.path,b.path));
      const entries=visible.filter(f=>parentPath(filePath(f))===path);
      for(const d of children)visit(d.path,depth+1);
      for(const f of entries)fileRow(f,depth);
      if(!path&&!children.length&&!entries.length){const empty=document.createElement('div');empty.className='usb-empty';empty.textContent=t('Keine Dateien','No files');fragment.appendChild(empty);}
    }
    if(status?.mounted&&(visible.length||relevant.size>1||!query&&filter.value==='all'))visit('',0);
    if(!fragment.childNodes.length){const empty=document.createElement('div');empty.className='usb-empty';empty.textContent=!status?.detected?t('Kein USB-Stick','No USB stick'):!status?.mounted?t('USB-Stick nicht eingebunden','USB stick not mounted'):t('Keine passenden Dateien','No matching files');fragment.appendChild(empty);}
    list.replaceChildren(fragment);
  }
  $('count').textContent=status?.mounted?visible.length+' / '+all.length+' '+t('Dateien','files')+' · '+Math.max(0,dirs.length-1)+' '+t('Ordner','folders')+(status.truncated?t(' · Liste unvollst\u00e4ndig (Begrenzung)',' · List incomplete (limit)'):''):'';
  if(status?.drive_limit_reached){const limit=status.drive_limit??4;$('count').textContent+=($('count').textContent?' \u00b7 ':'')+t('Zus\u00e4tzlicher USB-Stick nicht eingebunden: Laufwerkslimit '+limit+' erreicht.','Additional USB drive not mounted: drive limit '+limit+' reached.');}
  root.querySelector('.usb-clear').hidden=!search.value;controls();
}
function filterChanged(){
  if(search.value.trim()||filter.value!=='all')for(const f of status?.files||[]){
    if((filter.value==='all'||category(f)===filter.value)&&filePath(f).toLocaleLowerCase(de?'de':'en').includes(search.value.trim().toLocaleLowerCase(de?'de':'en'))){
      let path=parentPath(filePath(f));for(;;){openDirectories.add(path);if(!path)break;path=parentPath(path);}
    }
  }
  files();
}
search.oninput=filterChanged;filter.onchange=filterChanged;sort.onchange=files;
const clear=root.querySelector('.usb-clear');clear.title=clear.ariaLabel=t('Suche leeren','Clear search');clear.onclick=()=>{search.value='';files();search.focus();};
function hostDiagnostics(d){
  const info=root.querySelector('#usb-host-status'),list=root.querySelector('#usb-host-devices');list.replaceChildren();
  root.querySelector('.usb-diagnostics').hidden=!d;
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
function show(text,error=false){output.textContent=text;output.dataset.error=String(error);output.dataset.success=String(!!notice&&!error);}
function progress(label,name='',done=0,total=0){
  show(label);const box=root.querySelector('.usb-progress'),track=box.querySelector('[role=progressbar]');box.hidden=false;box.dataset.indeterminate=String(!total);
  box.querySelector('.usb-progress-name').textContent=name;
  box.querySelector('.usb-progress-value').textContent=total?size(done)+' / '+size(total):'';
  track.setAttribute('aria-label',label);track.setAttribute('aria-valuemin','0');track.setAttribute('aria-valuemax','100');
  if(total){const percent=Math.min(100,Math.round(done*100/total));track.setAttribute('aria-valuenow',String(percent));box.querySelector('.usb-progress-fill').style.width=percent+'%';}else track.removeAttribute('aria-valuenow');
}
async function api(path,params,expected=context()){
  if(params&&['command','manage','write/begin'].includes(path)){
    expected={...expected,generation:params.generation??expected.generation};
    const current=await api('status');render(current);requireContext(current,expected);
    if(transferActive(current))throw stale();
    params={...params,...expected};
  }
  const r=await fetch('/usb/'+path,{method:params?'POST':'GET',body:params?new URLSearchParams(params):undefined,cache:'no-store'});if(!r.ok)throw Error(await r.text());return r.json();
}
function stamp(){return new Date().toISOString().replace(/[:.]/g,'-');}
function button(label,action,needsSelection=false,symbol='file',parent=actions,iconOnly=false){const b=document.createElement('button');b.type='button';b.className='secondary'+(iconOnly?' usb-icon-button':'');b.innerHTML=icon(symbol);if(!iconOnly)b.append(document.createTextNode(label));b.title=b.ariaLabel=label;b.dataset.selection=needsSelection?'1':'0';b.onclick=()=>run(action);parent.appendChild(b);buttons.push(b);return b;}
function render(s){
  if(downloadToken&&(!s.mounted||s.token!==downloadToken||s.phase==='idle'||s.phase==='error')){downloadToken=0;if(s.phase==='error'){notice=s.message;noticeError=true;}}
  if(status&&(status.mounted!==s.mounted||!sameDrive(s,context()))){selection='';activeDirectory='';openDirectories.clear();openDirectories.add('');notice='';noticeError=false;}status=s;
  if(!directories().some(d=>d.path===activeDirectory))activeDirectory='';
  if(selection&&!chosen())selection='';
  hostDiagnostics(s.developer_mode===true?s.diagnostics:null);
  root.dataset.connection=s.mounted?'ready':s.detected?'detected':'offline';
  $('connection').textContent=s.mounted?t('Verbunden','Connected'):s.detected?t('Erkannt, nicht eingebunden','Detected, not mounted'):t('Nicht verbunden','Disconnected');
  const labels={idle:t('Bereit','Ready'),queued:t('Wartet','Queued'),reading:t('Wird gelesen','Reading'),ready:t('Datei bereit','File ready'),receiving:t('Wird gespeichert','Saving'),writing:t('Wird gespeichert','Writing'),error:t('USB-Fehler','USB error')};
  if(!busy)show(notice||(s.phase==='error'?s.message:!s.detected?t('Kein USB-Stick','No USB stick'):!s.mounted?t('Erkannt, nicht eingebunden','Detected, not mounted'):labels[s.phase]||'USB'),notice?noticeError:s.phase==='error');
  files();
}
async function poll(){if(polling||document.hidden||busy||(picker&&!picker.open))return;polling=true;try{const s=await api('status');if(!busy)render(s);}catch(e){if(!busy){status=null;selection='';activeDirectory='';openDirectories.clear();openDirectories.add('');hostDiagnostics(null);root.dataset.connection='offline';$('connection').textContent=t('Nicht erreichbar','Unavailable');files();show(String(e),true);}}finally{polling=false;}}
async function wait(phase,expected=context()){
  const until=Date.now()+120000;
  while(Date.now()<until){
    const s=await api('status');
    if(phase==='ejected'&&!s.mounted&&s.phase==='idle'&&(sameDrive(s,expected)||s.drive===4294967295&&s.drive_instance===0))return s;
    if(!sameDrive(s,expected)){render(s);throw stale();}
    if(s.phase==='error'||!s.mounted)throw Error(s.message||t('USB getrennt','USB disconnected'));
    if(s.phase===phase)return s;await new Promise(r=>setTimeout(r,200));
  }
  throw Error(t('USB-Zeitlimit','USB timeout'));
}
async function selectDrive(index,instance){
  if(driveLocked())return;
  const source=context();
  await run(async()=>{
    const current=await api('status');render(current);
    if(!sameContext(current,source)||transferActive(current)||!current.drives?.some(d=>d.connected&&d.index===index&&d.instance===instance))throw stale();
    progress(t('USB-Laufwerk wird eingebunden','Mounting USB drive'));
    await api('drive',{index,instance,generation:source.generation});
    const until=Date.now()+120000;
    while(Date.now()<until){
      const s=await api('status'),target=s.drives?.find(d=>d.connected&&d.index===index&&d.instance===instance);
      if(!target){render(s);throw stale();}
      if(s.phase==='error')throw Error(s.message||'USB error');
      if(s.mounted&&s.phase==='idle'&&s.generation!==source.generation&&s.drive===index&&s.drive_instance===instance&&target.selected&&target.mounted){render(s);return;}
      if(s.phase==='idle'&&s.generation!==source.generation){render(s);throw stale();}
      await new Promise(r=>setTimeout(r,200));
    }
    throw Error(t('USB-Zeitlimit','USB timeout'));
  });
}
$('drive').onchange=()=>{if($('drive').value){const [index,instance]=JSON.parse($('drive').value);void selectDrive(index,instance);}};
async function run(action){if(busy)return;busy=true;notice='';noticeError=false;controls();try{await action();}catch(e){notice=String(e);noticeError=true;}finally{const result=notice,error=noticeError;busy=false;root.querySelector('.usb-progress').hidden=true;await poll();if(result){notice=result;noticeError=error;show(notice,noticeError);}controls();}}
async function prepareRead(){
  const expected=context(),f=chosen();if(!f)throw Error(t('Keine Datei','No file'));
  progress(t('Datei wird gelesen','Reading file'),filePath(f));
  await api('command',{action:'read',index:f.index},expected);
  const ready=await wait('ready',expected);render(ready);return {file:f,token:ready.token,expected};
}
async function selected(){
  let token=0;
  try{const prepared=await prepareRead(),f=prepared.file;token=prepared.token;const r=await fetch('/usb/file?token='+token,{cache:'no-store'});if(!r.ok)throw Error(await r.text());const blob=await r.blob();if(blob.size!==f.size)throw Error('USB file size mismatch');const current=await api('status');render(current);requireContext(current,prepared.expected);return new File([blob],f.name,{type:'application/octet-stream'});}
  finally{if(token)await api('release',{token}).catch(()=>{});}
}
async function save(blob,name,directory=null,expected=context()){
  let token=0;try{
    progress(t('Wird auf USB gespeichert','Saving to USB'),name,0,blob.size);const params={name,size:blob.size};if(directory!==null)params.directory=directory;const start=await api('write/begin',params,expected);token=start.token;
    for(let offset=0;offset<blob.size;offset+=8192){const data=new FormData();data.append('chunk',blob.slice(offset,offset+8192),'chunk.bin');const r=await fetch('/usb/write/chunk?token='+token+'&offset='+offset,{method:'POST',body:data,cache:'no-store'});if(!r.ok)throw Error(await r.text());progress(t('Wird auf USB gespeichert','Saving to USB'),name,Math.min(blob.size,offset+8192),blob.size);}
    progress(t('Datei wird geschrieben','Writing file'),name);await api('write/commit',{token});await wait('idle',expected);token=0;notice=t('Auf USB gespeichert: ','Saved to USB: ')+name;
  }finally{if(token)await api('release',{token}).catch(()=>{});}
}
window.ofeUsbSave=async(blob,name,expected=context())=>{
  if(picker)return requestBrowser({mode:'save',blob,name,expected:expected.drive===undefined?null:expected});
  if(busy||exportPending||downloadToken||dialog.open||!status?.mounted)return false;
  exportPending=true;controls();
  try{return await new Promise(resolve=>edit('export',{blob,name,resolve},expected));}
  finally{exportPending=false;controls();}
};
async function exportFrom(url,name){
  try{const expected=picker?context(await api('status')):context();const r=await fetch(url,{cache:'no-store'});if(!r.ok)throw Error(await r.text());return await window.ofeUsbSave(await r.blob(),name,expected);}
  catch(e){notice=String(e);noticeError=true;show(notice,true);if(picker&&!picker.open)alert(notice);return false;}
}
const dialog=$('dialog'),form=dialog.querySelector('form');
const safeLeaf=name=>!!name&&new TextEncoder().encode(name).length<96&&!/^[. ]|[. ]$|\.\.|[\x00-\x1f\x7f/\\:"*?<>|]/.test(name)&&!/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(name);
const managementErrors={EXISTS:t('Zieldatei oder Ordner existiert bereits. Nichts wurde überschrieben.','Destination already exists. Nothing was overwritten.'),NOT_EMPTY:t('Der Ordner ist nicht leer. Er wurde nicht gelöscht.','The folder is not empty. It was not deleted.'),STALE:t('Die Datei hat sich geändert. Bitte die Liste aktualisieren.','The file changed. Refresh the list.'),PATH:t('Name oder Ordnertiefe ist nicht zulässig.','Invalid name or folder depth.'),SAME_PATH:t('Name und Zielordner sind unverändert.','Name and destination are unchanged.'),DISCONNECTED:t('USB-Stick getrennt','USB stick disconnected'),IO:t('USB-Dateiaktion fehlgeschlagen.','USB file operation failed.')};
managementErrors.SUBTREE=t('Ein Ordner kann nicht in sich selbst verschoben werden.','A folder cannot be moved into itself.');
managementErrors.LIMIT=t('Die Liste ist unvollständig. Ordner können so nicht sicher verschoben werden.','The list is incomplete. Folders cannot be moved safely.');
async function manage(action,index,directory,name,expected=context()){
  const queued=await api('manage',{action,index,directory,name},expected),until=Date.now()+120000;
  while(Date.now()<until){const s=await api('status');if(!sameDrive(s,expected)){render(s);throw stale();}if(!s.mounted)throw Error(managementErrors.DISCONNECTED);if(s.completed_operation===queued.operation){if(s.result!=='OK')throw Error(managementErrors[s.result]||s.result);return s;}if(s.completed_operation&&((s.completed_operation-queued.operation)>>>0)<0x80000000)throw Error(t('Ergebnis durch einen weiteren USB-Vorgang ersetzt. Bitte die Liste prüfen.','Result replaced by another USB operation. Check the file list.'));await new Promise(r=>setTimeout(r,200));}
  throw Error(t('USB-Zeitlimit','USB timeout'));
}
dialog.querySelector('.usb-dialog-cancel').onclick=()=>dialog.close();
function edit(action,exportData=null,expected=context()){
  if(busy||downloadToken||dialog.open||!status?.mounted){exportData?.resolve(false);return;}
  if(!sameContext(status,expected)){exportData?.resolve(false);return;}
  const f=chosen(),current=selectedDirectory();
  const movingDirectory=action==='move'&&!f,sourcePath=movingDirectory?current?.path:'';
  if(movingDirectory&&!sourcePath)return;
  let submitted=false;dialog.onclose=()=>{if(!submitted)exportData?.resolve(false);};
  const title={move:t('Verschieben / Umbenennen','Move / Rename'),mkdir:t('Neuer Ordner','New folder'),upload:t('Datei hochladen','Upload file'),export:t('Auf USB speichern','Save to USB')}[action];
  dialog.querySelector('h3').textContent=title;
  const fields=dialog.querySelector('.usb-dialog-fields');fields.replaceChildren();
  const error=dialog.querySelector('.usb-dialog-error');error.textContent='';
  function field(label,id,type){const wrap=document.createElement('div'),l=document.createElement('label'),input=document.createElement('input');input.id=id;input.type=type;input.dataset.noDirty='1';l.htmlFor=id;l.textContent=label;wrap.append(l,input);fields.appendChild(wrap);return input;}
  const upload=action==='upload'?field(t('Datei','File'),'usb-upload','file'):null;
  const name=field(t('Name','Name'),'usb-new-name','text');name.required=true;name.autocomplete='off';name.value=exportData?.name||(action==='move'?(f?.name||sourcePath.slice(sourcePath.lastIndexOf('/')+1)):'');
  const label=document.createElement('label'),target=document.createElement('select'),targetWrap=document.createElement('div');label.htmlFor='usb-destination';label.textContent=t('Zielordner','Destination folder');target.id='usb-destination';target.dataset.noDirty='1';
  const choices=directories().filter(d=>!movingDirectory||d.path!==sourcePath&&!d.path.startsWith(sourcePath+'/')).sort((a,b)=>a.path.localeCompare(b.path,de?'de':'en',{numeric:true}));
  for(const d of choices)target.add(new Option('/'+d.path,String(d.index)));target.value=String((movingDirectory?choices.find(d=>d.path===parentPath(sourcePath)):current)?.index??0);targetWrap.append(label,target);fields.append(targetWrap);
  const path=document.createElement('div');path.className='usb-dialog-path';fields.appendChild(path);
  const updatePath=()=>{path.textContent='/'+(choices.find(d=>String(d.index)===target.value)?.path||'');};target.onchange=updatePath;updatePath();
  if(upload)upload.onchange=()=>{if(upload.files[0])name.value=upload.files[0].name;};
  const submit=dialog.querySelector('.usb-dialog-submit');submit.textContent=action==='export'?t('Speichern','Save'):action==='upload'?t('Hochladen','Upload'):action==='mkdir'?t('Anlegen','Create'):t('Verschieben / Umbenennen','Move / Rename');
  form.onsubmit=e=>{
    e.preventDefault();error.textContent='';
    if(!safeLeaf(name.value)||action!=='mkdir'&&/\.tmp$/i.test(name.value)){error.textContent=t('Ungültiger oder zu langer Name.','Invalid or too long name.');return;}
    if(!status?.mounted||!sameContext(status,expected)){error.textContent=t('Die Dateiliste oder das USB-Laufwerk hat sich ge\u00e4ndert. Bitte erneut ausw\u00e4hlen.','The file list or USB drive changed. Select again.');return;}
    const destination=choices.find(d=>String(d.index)===target.value),file=exportData?.blob||upload?.files[0];
    if(!destination)return;
    if((upload||exportData)&&!file){error.textContent=t('Keine Datei','No file');return;}
    if(file&&file.size>transferLimit()){error.textContent=t('Datei überschreitet das Dateisystemmaximum: ','File exceeds the filesystem maximum: ')+size(transferLimit());return;}
    const next=(destination.path?destination.path+'/':'')+name.value;
    if(action==='move'&&next===(movingDirectory?sourcePath:filePath(f))){error.textContent=managementErrors.SAME_PATH;return;}
    submitted=true;dialog.close();
    void run(async()=>{
      progress(title,next);
      if(upload||exportData)await save(file,name.value,destination.index,expected);
      else await manage(movingDirectory?'move_dir':action,movingDirectory?current.index:f?.index||0,destination.index,name.value,expected);
      for(let p=destination.path;;p=parentPath(p)){openDirectories.add(p);if(!p)break;}
      if(movingDirectory){for(const p of [...openDirectories])if(p===sourcePath||p.startsWith(sourcePath+'/')){openDirectories.delete(p);openDirectories.add(next+p.slice(sourcePath.length));}activeDirectory=next;selection='';}
      else if(action==='mkdir'){openDirectories.add(next);activeDirectory=next;selection='';}
      else {activeDirectory=destination.path;selection=JSON.stringify([next,file?file.size:f.size]);}
      notice=action==='mkdir'?t('Ordner angelegt: ','Folder created: ')+next:action==='move'?t('Verschoben: ','Moved: ')+next:t('Auf USB gespeichert: ','Saved to USB: ')+next;
    }).then(()=>exportData?.resolve(!noticeError));
  };
  dialog.showModal();(upload||name).focus();
}
const tools=root.querySelector('.usb-tools'),globalActions=root.querySelector('.usb-global');
const expand=button(t('Alle Ordner aufklappen','Expand all folders'),()=>{},false,'expand',tools,true);
expand.onclick=()=>{const dirs=directories();if(dirs.every(d=>openDirectories.has(d.path))){openDirectories.clear();openDirectories.add('');}else for(const d of dirs)openDirectories.add(d.path);files();};
button(t('Datei hochladen','Upload file'),()=>{},false,'upload',globalActions).onclick=()=>edit('upload');
button(t('Neuer Ordner','New folder'),()=>{},false,'folderPlus',globalActions).onclick=()=>edit('mkdir');
button(t('Aktualisieren','Refresh'),async()=>{progress(t('Dateiliste wird aktualisiert','Refreshing files'));await api('command',{action:'refresh'});await wait('idle');},false,'refresh',tools,true);
button(t('Auswerfen','Eject'),async()=>{progress(t('USB-Stick wird ausgeworfen','Ejecting USB stick'));await api('command',{action:'eject'});await wait('ejected');notice=t('USB-Stick kann entfernt werden','USB stick can be removed');},false,'eject',tools,true);
button(t('Herunterladen','Download'),async()=>{const {file,token}=await prepareRead(),a=document.createElement('a');a.href='/usb/file?token='+token;a.download=file.name;document.body.appendChild(a);a.click();a.remove();downloadToken=token;notice=t('Download gestartet: ','Download started: ')+file.name;show(notice);},true,'download').transfer=true;
const move=button(t('Verschieben / Umbenennen','Move / Rename'),()=>{},false,'move');move.onclick=()=>edit('move');move.accept=f=>!!f||!!selectedDirectory()?.path;
const remove=button(t('Löschen','Delete'),async()=>{
  const f=chosen(),d=selectedDirectory();if(!f&&!d?.path)return;
  const path=f?filePath(f):d.path;
  if(!confirm((f?t('Datei endgültig löschen?','Permanently delete file?'):t('Leeren Ordner endgültig löschen?','Permanently delete empty folder?'))+'\n/'+path))return;
  progress(t('Wird gelöscht','Deleting'),path);
  await manage(f?'delete':'rmdir',f?f.index:d.index,0,'');
  selection='';if(!f)activeDirectory=parentPath(d.path);notice=t('Gelöscht: ','Deleted: ')+path;
},false,'trash',actions,true);remove.classList.add('danger');remove.accept=f=>!!f||!!selectedDirectory()?.path;
if(purpose==='config'){
  button(t('Backup auf USB','Backup to USB'),()=>{},false,'save',globalActions).onclick=()=>{if(confirm(t('Das Backup enthält Zugangsdaten. Auf diesem USB-Stick speichern?','The backup contains credentials. Save to this USB stick?')))void exportFrom('/config/export','open-fume-extractor-backup-'+stamp()+'.json');};
  button(t('Backup auswählen','Select backup'),async()=>{const file=await selected();if(!/\.json$/i.test(file.name))throw Error('JSON required');await loadBackupFile(file);notice=t('Backup zur Prüfung ausgewählt','Backup selected for review');document.getElementById('backup_import_btn')?.scrollIntoView({block:'center'});},true,'backup').accept=f=>!!f&&/\.json$/i.test(f.name);
}
if(purpose==='update'){
  const targetArea=root.querySelector('.usb-target'),label=document.createElement('label');label.htmlFor='usb-target';label.textContent=t('Ziel','Target');targetArea.appendChild(label);
  const target=document.createElement('select');target.id='usb-target';target.dataset.noDirty='1';for(const [value,name] of [['master','MCU'],['module',t('Modul','Module')]])target.add(new Option(name,value));targetArea.appendChild(target);
  if(document.getElementById('c6_file'))target.add(new Option('ESP32-C6','c6'));
  const firmwareInput=()=>document.getElementById(target.value==='c6'?'c6_file':target.value==='master'?'master_fw_file':'module_fw_file');
  const firmware=button(t('Firmware auswählen','Select firmware'),async()=>{const input=firmwareInput();if(!input||input.disabled||typeof DataTransfer==='undefined')throw Error(t('Updatefeld derzeit nicht verfügbar','Update field currently unavailable'));const file=await selected();if(!/\.bin$/i.test(file.name))throw Error('BIN required');const data=new DataTransfer();data.items.add(file);input.files=data.files;input.dispatchEvent(new Event('change',{bubbles:true}));notice=t('Firmware zur Prüfung ausgewählt','Firmware selected for review');input.scrollIntoView({block:'center'});},true,'firmware');firmware.accept=f=>!!f&&/\.bin$/i.test(f.name);firmware.transfer=true;firmware.unavailable=()=>!firmwareInput()||firmwareInput().disabled;target.onchange=controls;
}
if(purpose==='trace'){
  button('CSV > USB',()=>{},false,'save',globalActions).onclick=()=>diagDownload('csv',true);
  button('JSON > USB',()=>{},false,'save',globalActions).onclick=()=>diagDownload('json',true);
  button(t('Diagnose auf USB','Diagnostics to USB'),()=>{},false,'save',globalActions).onclick=()=>exportFrom('/diagnostics/state','open-fume-extractor-diagnostics-'+stamp()+'.json');
}
const accepts=(f,accept)=>!accept||accept.split(',').some(ext=>ext.trim().startsWith('.')&&f.name.toLowerCase().endsWith(ext.trim().toLowerCase()));
function finishPicker(value){
  const request=pending;pending=null;
  if(picker?.open)picker.close();
  returnFocus?.focus();returnFocus=null;request?.resolve(value);
}
async function requestBrowser(options){
  if(!picker||pending||busy||downloadToken)return options.mode==='read'?null:false;
  returnFocus=document.activeElement;
  return new Promise(resolve=>{
    pending={...options,resolve};const request=pending;selection='';search.value='';filter.value='all';listSignature='';notice='';
    picker.querySelector('#usb-picker-title').textContent=options.title||(options.mode==='read'?t('Von USB ausw\u00e4hlen','Select from USB'):t('Auf USB speichern','Save to USB'));
    const input=picker.querySelector('#usb-picker-name'),label=picker.querySelector('label');
    input.hidden=label.hidden=options.mode!=='save';input.required=options.mode==='save';input.value=options.name||'';
    label.textContent=t('Dateiname','File name');picker.querySelector('.usb-picker-error').textContent='';
    picker.querySelector('.usb-picker-confirm').textContent=options.mode==='read'?t('Ausw\u00e4hlen','Select'):t('Hier speichern','Save here');
    picker.showModal();files();
    void (async()=>{try{const current=await api('status');if(pending!==request)return;render(current);if(options.expected)requireContext(current,options.expected);}catch(e){if(pending===request){request.invalid=true;picker.querySelector('.usb-picker-error').textContent=String(e);controls();}}})();
  });
}
window.ofeUsbPick=(options={})=>requestBrowser({...options,mode:'read'});
window.ofeUsbExport=exportFrom;
if(picker){
  const close=picker.querySelector('.usb-picker-close');close.innerHTML=icon('close');close.title=close.ariaLabel=t('Schlie\u00dfen','Close');
  const cancel=picker.querySelector('.usb-picker-cancel');cancel.textContent=t('Abbrechen','Cancel');
  close.onclick=cancel.onclick=()=>{if(!busy)finishPicker(pending?.mode==='read'?null:false);};
  picker.addEventListener('cancel',e=>{e.preventDefault();if(!busy)finishPicker(pending?.mode==='read'?null:false);});
  picker.querySelector('.usb-picker-footer').onsubmit=async e=>{
    e.preventDefault();if(!pending||pending.invalid||busy)return;
    const request=pending,error=picker.querySelector('.usb-picker-error');error.textContent='';let result;
    await run(async()=>{
      if(request.mode==='read'){
        const file=chosen();if(!file||!accepts(file,request.accept))throw Error(t('Passende Datei ausw\u00e4hlen','Select a matching file'));
        result=await selected();
      }else{
        const name=picker.querySelector('#usb-picker-name').value,directory=selectedDirectory();
        if(!safeLeaf(name)||/\.tmp$/i.test(name))throw Error(t('Ung\u00fcltiger Dateiname','Invalid file name'));
        if(!directory||request.blob.size>transferLimit())throw Error(t('Datei oder Ziel nicht verf\u00fcgbar','File or destination unavailable'));
        await save(request.blob,name,directory.index,context());result=true;
      }
    });
    if(pending&&noticeError)error.textContent=notice;
    else if(pending===request&&result!==undefined)finishPicker(result);
  };
}
function contextualButton(source,label,action){
  if(source.dataset.usbAction)return;source.dataset.usbAction='1';
  const b=document.createElement('button');b.type='button';b.className='secondary ofe-usb-action';b.innerHTML=icon('usb');b.append(document.createTextNode(label));
  const sync=()=>{b.disabled=source.matches(':disabled')||b.dataset.busy==='1';};
  b.onclick=async()=>{if(source.matches(':disabled')||b.dataset.busy==='1')return;b.dataset.busy='1';sync();try{await action();}catch(e){alert(String(e));}finally{delete b.dataset.busy;sync();}};
  new MutationObserver(sync).observe(source,{attributes:true,attributeFilter:['disabled']});sync();
  const group=document.createElement('div');group.className=source.matches('input[type=file]')?'ofe-file-actions':'ofe-export-actions';
  source.before(group);group.append(source,b);
  return b;
}
function enhanceFileActions(){
  for(const input of document.querySelectorAll('input[type=file]:not([data-usb-action])')){
    if(root.contains(input))continue;
    const firmware=['master_fw_file','module_fw_file','c6_file'].includes(input.id);
    if(!firmware&&input.id!=='backup_file'&&!/^uni_file_\d+$/.test(input.id))continue;
    contextualButton(input,firmware?t('Von USB installieren','Install from USB'):t('Von USB laden','Load from USB'),async()=>{
      if(input.disabled)return;
      const file=await window.ofeUsbPick({accept:input.accept,title:firmware?t('Firmware von USB','Firmware from USB'):t('Datei von USB','File from USB')});
      if(!file||!input.isConnected||input.disabled)return;
      const data=new DataTransfer();data.items.add(file);input.files=data.files;input.dispatchEvent(new Event('change',{bubbles:true}));
    });
  }
  const backup=document.querySelector('a[href="/config/export"]');
  if(backup)contextualButton(backup,t('Auf USB speichern','Save to USB'),async()=>{if(confirm(t('Das Backup enth\u00e4lt Zugangsdaten. Auf USB speichern?','The backup contains credentials. Save to USB?')))await exportFrom('/config/export','open-fume-extractor-backup-'+stamp()+'.json');});
  const exporters={universalExport,universalProtocolBuilderExport,modbusBuilderExport};
  for(const b of document.querySelectorAll('button[onclick]:not([data-usb-action])')){
    const call=b.getAttribute('onclick')||'',profile=call.match(/^(universalExport|universalProtocolBuilderExport|modbusBuilderExport)\((\d+)\)$/),trace=call.match(/^diagDownload\(['"](csv|json)['"]\)$/);
    if(profile)contextualButton(b,t('Auf USB speichern','Save to USB'),()=>exporters[profile[1]](Number(profile[2]),true));
    else if(trace){const save=contextualButton(b,t('Auf USB','To USB'),()=>diagDownload(trace[1],true));if(save)save.title=trace[1].toUpperCase()+' > USB';}
  }
  const quality=document.getElementById('rq_export');if(quality)contextualButton(quality,t('Auf USB','To USB'),()=>window.ofeRs485QualityExport(true));
  for(const snapshot of document.querySelectorAll('[data-diag-snapshot]:not([data-usb-action])')){
    const save=contextualButton(snapshot,t('Auf USB','To USB'),()=>exportFrom('/diagnostics/state','open-fume-extractor-diagnostics-'+stamp()+'.json'));
    if(save)save.dataset.usbDiagnostics='1';
  }
}
// Export functions exist only on the status page; resolve them when activated.
function universalExport(...args){return window.universalExport?.(...args);}
function universalProtocolBuilderExport(...args){return window.universalProtocolBuilderExport?.(...args);}
function modbusBuilderExport(...args){return window.modbusBuilderExport?.(...args);}
if(purpose==='picker'||purpose==='browser'){
  const nav=document.querySelector('header nav.nav,header .nav');
  if(nav&&!nav.querySelector('a[href="/usb"]')){const link=document.createElement('a');link.href='/usb';link.className='btn '+(purpose==='browser'?'active':'secondary')+' ofe-usb-nav';link.innerHTML=icon('usb')+'USB';nav.insertBefore(link,nav.querySelector('button.danger'));}
  enhanceFileActions();
  let scheduled=false;
  new MutationObserver(records=>{if(!scheduled&&records.some(r=>!root.contains(r.target)&&!picker?.contains(r.target))){scheduled=true;requestAnimationFrame(()=>{scheduled=false;enhanceFileActions();});}}).observe(document.body,{childList:true,subtree:true});
}
globalActions.hidden=!globalActions.children.length;files();
poll();setInterval(poll,3000);document.addEventListener('visibilitychange',poll);
})();)USBJS";
