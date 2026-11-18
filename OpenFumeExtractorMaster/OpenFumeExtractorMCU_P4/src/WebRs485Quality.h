#pragma once
#define OFE_RS485_QUALITY_SCRIPT R"RQ(<script>(()=>{
'use strict';
const keys=['requests','responses','timeouts','bad_seq','bad_cmd','late_responses','update_requests','recovery_requests'];
const parserKeys=['ofe_crc_errors','ofe_bad_length','ofe_bad_version','ofe_escape_errors','ofe_overflow_errors','ofe_short_frames'];
const copy=m=>Object.fromEntries(keys.map(k=>[k,Number(m[k]||0)>>>0]));
const delta=(a,b)=>{const n=((Number(a)>>>0)-(Number(b)>>>0))>>>0;if(n>0x7fffffff)throw Error('reset');return n;};
class Session{
 constructor(data,seconds,now){
  if(![30,60,300].includes(seconds)||!data||data.ofe_baud!==250000||!Number.isInteger(data.sample_ms)||data.sample_ms<0||data.sample_ms>0xffffffff)throw Error('sample');
  this.duration=seconds*1000;this.started=now;this.lastAt=now;this.ms=Number(data.sample_ms)>>>0;this.elapsed=0;this.samples=1;this.gaps=0;this.reason='running';this.parser=Object.fromEntries(parserKeys.map(k=>[k,0]));this.previous=Object.fromEntries(parserKeys.map(k=>[k,Number(data[k]||0)>>>0]));
  this.modules=(data.modules||[]).filter(m=>m.transport==='rs485').map(m=>({addr:m.addr,name:String(m.name||m.type_name||''),type:m.type,previous:copy(m),totals:Object.fromEntries(keys.map(k=>[k,0])),sampled_latency_max_ms:0}));
  this.noResponseWindows=0;this.resumptions=0;this.awaitingResponse=false;
  if(!this.modules.length)throw Error('modules');
 }
 feed(data,now){
  if(this.reason!=='running')return;
  if(now-this.lastAt>2000)this.gaps++;
  const ms=Number(data.sample_ms)>>>0;
  try{
   const dt=delta(ms,this.ms);if(!dt)return;if(dt>10000)throw Error('reset');
   if(data.ofe_baud!==250000)throw Error('changed');
   const current=(data.modules||[]).filter(m=>m.transport==='rs485');
   if(current.length!==this.modules.length)throw Error('changed');
   const updates=this.modules.map(row=>{const m=current.find(m=>m.addr===row.addr&&m.type===row.type);if(!m)throw Error('changed');return {row,m,counts:Object.fromEntries(keys.map(k=>[k,delta(m[k]||0,row.previous[k])]))};});
   const errors=Object.fromEntries(parserKeys.map(k=>[k,delta(data[k]||0,this.previous[k])]));
   const replies=updates.reduce((n,v)=>n+v.counts.responses,0),timeouts=updates.reduce((n,v)=>n+v.counts.timeouts,0);
   if(!replies&&timeouts){this.noResponseWindows++;this.awaitingResponse=true;}else if(replies&&this.awaitingResponse){this.resumptions++;this.awaitingResponse=false;}
   for(const {row,m,counts} of updates){for(const k of keys)row.totals[k]+=counts[k];row.previous=copy(m);if(counts.responses)row.sampled_latency_max_ms=Math.max(row.sampled_latency_max_ms,Number(m.latency_last_ms)||0);}
   for(const k of parserKeys){this.parser[k]+=errors[k];this.previous[k]=Number(data[k]||0)>>>0;}
   this.elapsed+=dt;this.ms=ms;this.lastAt=now;this.samples++;
   if(this.elapsed>=this.duration)this.reason='complete';
  }catch(error){this.reason=error.message==='reset'?'reset':'changed';}
 }
 stop(){if(this.reason==='running')this.reason='stopped';}
 stale(now){if(this.reason==='running'&&now-this.lastAt>4000)this.reason='connection';}
 report(){
  const modules=this.modules.map(m=>({addr:m.addr,name:m.name,...m.totals,sampled_latency_max_ms:m.sampled_latency_max_ms}));
  const sum=k=>modules.reduce((n,m)=>n+m[k],0),errors=Object.values(this.parser).reduce((a,b)=>a+b,0)+sum('timeouts')+sum('bad_seq')+sum('bad_cmd')+sum('late_responses');
  let result=this.reason==='running'?'running':this.reason!=='complete'?'inconclusive':this.gaps||sum('update_requests')||sum('recovery_requests')?'inconclusive':modules.some(m=>!m.requests)?'insufficient':!sum('responses')?'no_response':errors?'errors':'ok';
  return {mode:'passive',baud:250000,result,reason:this.reason,elapsed_ms:this.elapsed,samples:this.samples,sample_gaps:this.gaps,no_response_windows:this.noResponseWindows,response_resumptions:this.resumptions,parser_errors:{...this.parser},modules};
 }
}
window.OfeRs485QualitySession=Session;
const $=name=>document.getElementById('rq_'+name);if(!$('start'))return;
const de=()=>{const e=document.getElementById('lang_sel');return !e||e.value!=='en';},say=(a,b)=>de()?a:b;
let last=null,receivedAt=0,session=null;
const labels={running:['Messung läuft','Measuring'],ok:['Keine Fehler im Messfenster','No errors in observation window'],errors:['Kommunikationsfehler gemessen','Communication errors observed'],no_response:['Keine Antworten empfangen','No responses received'],insufficient:['Zu wenig Verkehr für eine Bewertung','Insufficient traffic for assessment'],inconclusive:['Messung nicht vollständig auswertbar','Observation inconclusive']};
function render(){
 document.querySelectorAll('[data-rq-de]').forEach(e=>e.textContent=e.getAttribute(de()?'data-rq-de':'data-rq-en'));
 const report=session&&session.report(),active=report&&report.result==='running';
 $('start').disabled=!!active||!last||performance.now()-receivedAt>2000||!(last.modules||[]).some(m=>m.transport==='rs485');$('stop').disabled=!active;$('duration').disabled=!!active;$('export').disabled=!report||!!active;
 if(!report){$('state').textContent=say('Bereit · passiv · 250 kBaud','Ready · passive · 250 kbaud');return;}
 $('state').textContent=labels[report.result][de()?0:1]+' · '+(report.elapsed_ms/1000).toFixed(1)+' s · '+report.samples+' '+say('Messwerte','samples');
 $('progress').value=Math.min(session.duration,report.elapsed_ms);$('progress').max=session.duration;
 const rows=$('rows');rows.replaceChildren();
 for(const m of report.modules){const tr=document.createElement('tr');for(const value of ['0x'+m.addr.toString(16).toUpperCase()+' '+m.name,m.requests,m.responses,m.timeouts,m.bad_seq+m.bad_cmd,m.sampled_latency_max_ms+' ms']){const td=document.createElement('td');td.textContent=value;tr.appendChild(td);}rows.appendChild(tr);}
 $('errors').textContent='CRC '+report.parser_errors.ofe_crc_errors+' · LEN '+report.parser_errors.ofe_bad_length+' · ESC '+report.parser_errors.ofe_escape_errors+' · '+say('Weitere','Other')+' '+(report.parser_errors.ofe_bad_version+report.parser_errors.ofe_overflow_errors+report.parser_errors.ofe_short_frames)+' · '+say('Antwortlose Fenster','No-response windows')+' '+report.no_response_windows+' · '+say('Antworten wiederaufgenommen','Response resumptions')+' '+report.response_resumptions;
}
window.ofeBusQuality={feed(data){last=data;receivedAt=performance.now();if(session)session.feed(data,receivedAt);render();}};
$('start').onclick=()=>{if($('start').disabled)return;try{session=new Session(last,Number($('duration').value),performance.now());render();}catch(_){$('state').textContent=say('Keine gültigen RS485-Messwerte','No valid RS485 samples');}};
$('stop').onclick=()=>{if(session)session.stop();render();};
window.ofeRs485QualityExport=(usb=false)=>{if(!session||$('export').disabled)return;const blob=new Blob([JSON.stringify(session.report(),null,2)],{type:'application/json'}),name='open-fume-extractor-rs485-quality-'+new Date().toISOString().replace(/[:.]/g,'-')+'.json';if(usb&&window.ofeUsbSave)return window.ofeUsbSave(blob,name);const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
$('export').onclick=()=>window.ofeRs485QualityExport();
setInterval(()=>{if(session)session.stale(performance.now());render();},1000);render();
})();</script>)RQ"

static void web_append_rs485_quality(String& html) {
  html+=F("<section class='panel' id='rq_panel'><h2 data-rq-de='RS485-Qualitätstest' data-rq-en='RS485 quality test'>RS485-Qualitätstest</h2>");
  html+=F("<div class='diag-control-row'><div class='diag-target-wrap'><label for='rq_duration' data-rq-de='Messdauer' data-rq-en='Duration'>Messdauer</label><select id='rq_duration'><option value='30'>30 s</option><option value='60' selected>60 s</option><option value='300'>5 min</option></select></div>");
  html+=F("<div class='diag-session-actions'><button id='rq_start' disabled data-rq-de='Starten' data-rq-en='Start'>Starten</button><button id='rq_stop' class='secondary' disabled data-rq-de='Stoppen' data-rq-en='Stop'>Stoppen</button></div></div><p id='rq_state' role='status'></p><progress id='rq_progress' value='0' max='60000' style='width:100%'></progress>");
  html+=F("<div class='diag-export-actions diag-quality-export'><div class='diag-export-group'><span class='diag-export-label' data-rq-de='Testergebnis (JSON)' data-rq-en='Test report (JSON)'>Testergebnis (JSON)</span><button id='rq_export' type='button' class='secondary' disabled><svg viewBox='0 0 24 24' aria-hidden='true'><path d='M5,20H19V18H5M19,9H15V3H9V9H5L12,16L19,9Z'/></svg><span data-rq-de='Herunterladen' data-rq-en='Download'>Herunterladen</span></button></div></div>");
  html+=F("<div style='overflow-x:auto'><table><thead><tr><th data-rq-de='Modul' data-rq-en='Module'>Modul</th><th>TX</th><th>RX</th><th>Timeout</th><th>SEQ/CMD</th><th data-rq-de='Latenz max. (Stichprobe)' data-rq-en='Max latency (sampled)'>Latenz max. (Stichprobe)</th></tr></thead><tbody id='rq_rows'></tbody></table></div><p id='rq_errors' class='muted'></p></section>");
}
