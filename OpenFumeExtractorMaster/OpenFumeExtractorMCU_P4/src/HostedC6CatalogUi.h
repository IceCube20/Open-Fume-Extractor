#pragma once
#define OFE_C6_CATALOG_SCRIPT R"C6CAT(<script>(()=>{
'use strict';
function version(text){if(typeof text!=='string'||!/^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(text))throw Error('CATALOG_INVALID');const p=text.split('.').map(Number);if(p.some(n=>n>255))throw Error('CATALOG_INVALID');return p[0]*65536+p[1]*256+p[2];}
function inspect(data,cfg,host){
 if(!data||data.schema!==1||!Array.isArray(data.releases)||data.releases.length>32)throw Error('CATALOG_INVALID');
 const ids=new Set(),matches=[];let newest=0;
 for(const r of data.releases){
  const h=r&&r.host,c=r&&r.coprocessor;
  if(!r||typeof r.id!=='string'||r.id.length<1||r.id.length>96||ids.has(r.id)||!['approved','withdrawn'].includes(r.status)||typeof r.board!=='string'||r.transport!=='sdio'||!h||!c||c.chip!=='esp32c6'||!Number.isInteger(c.size)||c.size<1||c.size>2097152||typeof c.sha256!=='string'||!/^[a-f0-9]{64}$/.test(c.sha256)||typeof c.url!=='string'||c.url!==`https://espressif.github.io/arduino-esp32/hosted/esp32c6-v${c.version}.bin`)throw Error('CATALOG_INVALID');
  ids.add(r.id);version(h.arduino_esp32);version(h.esp_idf);const v=version(c.version);if(v!==version(h.esp_hosted))throw Error('CATALOG_INVALID');
  if(r.board!==cfg.board||r.status!=='approved')continue;
  newest=Math.max(newest,v);
  if(h.arduino_esp32===cfg.arduino&&h.esp_idf===cfg.idf&&v===host)matches.push(r);
 }
 if(matches.length>1)throw Error('CATALOG_INVALID');
 const r=matches[0],c=r&&r.coprocessor;
 const pinned=!!c&&version(c.version)===cfg.expected&&c.url===cfg.url&&c.size===cfg.size&&c.sha256===cfg.sha256;
 return {kind:pinned?'approved':r?'master_update':'none',version:pinned?cfg.expected:newest,newerSdk:newest>host};
}
async function load(url){
 const response=await fetch(url,{cache:'no-store',credentials:'omit',referrerPolicy:'no-referrer',signal:AbortSignal.timeout(8000)});
 if(!response.ok)throw Error('HTTP '+response.status);
 const reader=response.body.getReader(),decoder=new TextDecoder();let size=0,text='';
 try{for(;;){const part=await reader.read();if(part.done)break;size+=part.value.byteLength;if(size>32768)throw Error('CATALOG_INVALID');text+=decoder.decode(part.value,{stream:true});}text+=decoder.decode();return JSON.parse(text);}finally{await reader.cancel();reader.releaseLock();}
}
window.OFE_C6_CATALOG={inspect,load};
})();</script>)C6CAT"
