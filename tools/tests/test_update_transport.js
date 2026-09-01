// Execute the JavaScript embedded in the firmware, with network and DOM adapters.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/WebUpdate.inc.h'), 'utf8');
const lines = source.split(/\r?\n/);
const scripts = [];
for (const line of lines) {
  const m = line.match(/^  html \+= F\(("(?:\\.|[^"\\])*")\);$/);
  if (!m) continue;
  const js = JSON.parse(m[1]);
  if (/^(?:function |async function |var moduleUpdateRun=|var masterUpdateRun=)/.test(js)) scripts.push(js);
}
new vm.Script(scripts.join('\n'));

async function exercise(de, developer, liveTransport, selectedTransport, validSignature=true) {
  const statuses = [], requests = [];
  let now = 0;
  const file = {name:'DisplayModule_800x480.bin', size:1000, slice:(a,b)=>({size:b-a})};
  const option = {dataset:{type:'6', caps:'8388608', transport:selectedTransport}};
  const form = {addr:{options:[option], selectedIndex:0, value:'64'},
    querySelector(selector) { return selector==='input[type=file]' ? {files:[file]} : {checked:false}; }};
  const stat = {set textContent(value) { statuses.push(value); }, get textContent() { return statuses.at(-1); }};
  const context = vm.createContext({UI_DE:de, DEV_MODE:developer,
    RS485_FW_CHUNK:188, RS485_FW_CHUNK_DISPLAY:188, MODULE_FW_HTTP_CHUNK:500, MASTER_FW_CHUNK:4096,
    performance:{now:()=>{now+=100; return now;}},
    document:{getElementById:()=>({style:{}})},
    fetch:async(url)=>{requests.push(url); return {ok:true, text:async()=>'', json:async()=>({})};},
    setTimeout, clearTimeout, encodeURIComponent, console});
  vm.runInContext(scripts.join('\n'), context);
  context.findFwAuth=async()=>validSignature?{size:1000,target:'DISPLAY_800X480',version:'1.3.53beta',text:'signed'}:null;
  context.findFwSig=async()=> 'OFE_FW_SIG:v1;target=DISPLAY_800X480;version=1.3.53beta;';
  context.getState=async()=>({uptime_ms:100,modules:[{addr:64,transport:liveTransport}]});
  context.postBlob=async()=>'';
  context.moduleStatsText=async()=>'';
  context.waitOnline=async()=>({name:'Display 800x480',fw:'1.3.53beta'});
  await context.sendModule(form,{style:{}},stat);
  if (!validSignature) {
    assert.equal(requests.length,0);
    assert.match(stat.textContent,/Blockiert|Blocked/);
    return;
  }
  assert(requests.some(url=>url.startsWith('/update/module/begin?')));
  assert(requests.includes('/update/module/end'));
  const progress=statuses.filter(s=>/^\d+% /.test(s));
  assert.equal(progress.length,2);
  const transport=liveTransport==='wifi'?(de?'WLAN':'WiFi'):'RS485';
  for (const text of progress) {
    assert(text.includes('% '+transport+' ('),text);
    assert.equal(text.includes('188 B'),developer,text);
    assert(text.includes('/ 1000 B)'),text);
    assert(text.includes(de?'Restzeit':'ETA'),text);
  }
}

(async()=>{
  for (const de of [false,true]) for (const developer of [false,true]) {
    await exercise(de,developer,'wifi','rs485');
    await exercise(de,developer,'rs485','wifi');
  }
  await exercise(true,false,'wifi','wifi',false);
  console.log('PASS: embedded update JS syntax, live transport labels DE/EN, developer-only chunk size and missing-signature rejection.');
})().catch(error=>{console.error(error);process.exitCode=1;});
