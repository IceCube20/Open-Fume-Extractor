#pragma once
// Lucide Bluetooth, WiFi and ethernet-port paths, ISC; docs/licenses/Lucide-ISC.txt.
// USB flash drive: Material Design Icons / Pictogrammers, Apache-2.0;
// docs/licenses/MaterialDesign-USB.txt. Inline SVG does not need a remote font.
#define OFE_TRANSPORT_HEADER_HTML R"ICON(<span class="transport-icons"><a class="transport-ble" href="/display-link#ble" title="Bluetooth" aria-label="Bluetooth"><svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m7 7 10 10-5 5V2l5 5L7 17"/></svg></a><a class="transport-network transport-wifi" href="/config" title="Netzwerk / Network" aria-label="Netzwerk / Network"><svg data-network-icon="wifi" xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20h.01"/><path d="M2 8.82a15 15 0 0 1 20 0"/><path d="M5 12.859a10 10 0 0 1 14 0"/><path d="M8.5 16.429a5 5 0 0 1 7 0"/></svg><svg data-network-icon="ethernet" hidden xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10 8v1"/><path d="M14 8v1"/><path d="M18 8v1"/><path d="M19 17a2 2 0 00-1.765 1.059l-.47.882A2 2 0 0115 20H9a2 2 0 01-1.765-1.059l-.47-.882A2 2 0 005 17H4a2 2 0 01-2-2V6a2 2 0 012-2h16a2 2 0 012 2v9a2 2 0 01-2 2z"/><path d="M6 8v1"/></svg></a></span>)ICON"
#define OFE_TRANSPORT_HEADER_SCRIPT R"ICONS(<style>
.brand{flex-wrap:wrap;min-width:0}.brand .transport-icons{display:inline-flex;align-items:center;gap:4px;margin:0}
.transport-icons a{display:inline-flex;align-items:center;justify-content:center;width:32px;height:36px;color:#7b8794;text-decoration:none}
.transport-icons .transport-ble.is-on{color:#69b4ff}.transport-icons .transport-network.is-on{color:#59c77a}
.transport-icons .transport-network.is-ap{color:#f4c25b}.transport-icons svg[hidden]{display:none}.transport-icons a:hover{color:#fff}
.transport-icons .transport-usb[hidden]{display:none}.transport-icons .transport-usb{color:#f4c25b}.transport-icons .transport-usb.is-on{color:#59c77a}
</style><script>(()=>{
let last=null;
function usbHeader(s){
  let e=document.querySelector('.transport-usb');
  if(typeof s.usb_detected!=='boolean'){if(e)e.hidden=true;return}
  if(!e){e=document.createElement('a');e.className='transport-usb';e.href='/config#usb-storage';e.innerHTML='<svg xmlns="http://www.w3.org/2000/svg" data-icon="mdi:usb-flash-drive" width="22" height="22" viewBox="0 0 24 24" fill="currentColor"><path d="M8 15C8.55 15 9 15.45 9 16C9 16.55 8.55 17 8 17C7.45 17 7 16.55 7 16C7 15.45 7.45 15 8 15M15.07 4.69L16.5 6.1L15.07 7.5L13.66 6.1L15.07 4.69M17.9 7.5L19.31 8.93L17.9 10.34L16.5 8.93L17.9 7.5M8 13C6.34 13 5 14.34 5 16C5 17.66 6.34 19 8 19C9.66 19 11 17.66 11 16C11 14.34 9.66 13 8 13M9.77 4.33L10.5 5.08L14.29 1.29C14.47 1.11 14.72 1 15 1C15.28 1 15.53 1.11 15.71 1.29L22.78 8.36L22.78 8.37C22.92 8.54 23 8.76 23 9C23 9.3 22.87 9.57 22.66 9.76L22.66 9.76L18.93 13.5L19.67 14.23L12.95 20.95C11.68 22.22 9.93 23 8 23C4.13 23 1 19.87 1 16C1 14.07 1.78 12.32 3.05 11.05L9.77 4.33M20.59 9L15 3.41L11.93 6.5L17.5 12.08L20.59 9Z"/></svg>';document.querySelector('.transport-icons')?.appendChild(e)}
  e.hidden=!s.usb_detected;e.classList.toggle('is-on',!!s.usb_mounted);e.title=german()?(s.usb_mounted?'USB-Stick bereit':'USB-Stick erkannt, nicht eingebunden'):(s.usb_mounted?'USB stick ready':'USB stick detected, not mounted');e.setAttribute('aria-label',e.title);
}
function german(){const e=document.getElementById('lang_sel');return !e||e.value!=='en'}
window.ofeSetNetworkHeader=function(s){
  last=s;const de=german(),kind=s.network_transport||(s.wifi_connected?'wifi':'offline');
  const connected=kind==='ethernet'||kind==='wifi',ip=s.network_ip||s.master_ip||'';
  const names=de?{ethernet:'Ethernet verbunden',wifi:'WLAN verbunden',ap:'WLAN-Einrichtung',offline:'Netzwerk getrennt',unknown:'Netzwerkstatus nicht verf\u00fcgbar'}:{ethernet:'Ethernet connected',wifi:'WiFi connected',ap:'WiFi setup',offline:'Network disconnected',unknown:'Network status unavailable'};
  let label=names[kind]||names.unknown;
  if(connected&&ip&&ip!=='0.0.0.0')label+=': '+ip;
  if(kind==='ethernet'&&s.wifi_enabled===false)label+=de?' - WLAN aus':' - WiFi off';
  document.querySelectorAll('.transport-network').forEach(e=>{
    e.dataset.transport=kind;e.classList.toggle('is-on',connected);e.classList.toggle('is-ap',kind==='ap');
    e.title=label;e.setAttribute('aria-label',label);
    e.querySelectorAll('[data-network-icon]').forEach(svg=>svg.toggleAttribute('hidden',svg.dataset.networkIcon!==(kind==='ethernet'?'ethernet':'wifi')));
  });
};
async function update(){
  if(document.hidden)return;
  try{const r=await fetch('/display-link/ble?brief=1',{cache:'no-store'});if(!r.ok)throw Error(r.status);const s=await r.json();
    document.querySelectorAll('.transport-ble').forEach(e=>e.classList.toggle('is-on',!!s.active));window.ofeSetNetworkHeader(s);usbHeader(s);
  }catch(e){window.ofeSetNetworkHeader({network_transport:'unknown'});usbHeader({});document.querySelectorAll('.transport-ble').forEach(e=>e.classList.remove('is-on'));}
}
const selector=document.getElementById('lang_sel');if(selector)selector.addEventListener('change',()=>{if(last)window.ofeSetNetworkHeader(last)});
document.addEventListener('visibilitychange',update);update();setInterval(update,5000);
})();</script>)ICONS"
