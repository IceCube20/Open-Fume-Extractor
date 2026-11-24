#pragma once
#define OFE_C6_UPSTREAM_SCRIPT R"C6(<script>(()=>{
'use strict';
const api='https://api.github.com/repos/espressif/arduino-esp32',sha=/^[a-f0-9]{40}$/;
async function json(url){
 const r=await fetch(url,{credentials:'omit',referrerPolicy:'no-referrer',redirect:'error',signal:AbortSignal.timeout(20000),headers:{Accept:'application/vnd.github+json'}});
 if(!r.ok)throw Error('GitHub HTTP '+r.status);
 const text=await r.text();if(text.length>524288)throw Error('UPSTREAM_SIZE');return JSON.parse(text);
}
async function load(){
 const ref=await json(api+'/git/ref/heads/gh-pages'),commit=ref?.object?.sha;
 if(!sha.test(commit||'')||ref.object.type!=='commit')throw Error('UPSTREAM_COMMIT');
 const entries=await json(api+'/contents/hosted?ref='+commit);
 if(!Array.isArray(entries)||entries.length>512)throw Error('UPSTREAM_LIST');
 const seen=new Set(),versions=[];
 for(const e of entries){
  const match=typeof e.name==='string'&&e.name.match(/^esp32c6-v(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.bin$/);if(!match)continue;
  const p=match.slice(1).map(Number),version=p.join('.'),expected=p[0]*65536+p[1]*256+p[2];
  if(p.some(n=>n>255)||e.name!=='esp32c6-v'+version+'.bin'||e.type!=='file'||e.path!=='hosted/'+e.name||!sha.test(e.sha||'')||!Number.isInteger(e.size)||e.size<256||e.size>2097152||seen.has(expected))throw Error('UPSTREAM_ENTRY');
  seen.add(expected);
  // Older Hosted protocols cannot reliably provide the version/OTA RPCs used here.
  if(expected<0x020600||expected>0x02ffff)continue;
  versions.push({expected,size:e.size,blob:e.sha,commit,url:'https://raw.githubusercontent.com/espressif/arduino-esp32/'+commit+'/hosted/'+e.name});
 }
 if(!versions.length)throw Error('UPSTREAM_EMPTY');return versions.sort((a,b)=>b.expected-a.expected);
}
window.OFE_C6_UPSTREAM={load};
})();</script>)C6"
