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
  if (/^(?:function |async function |var moduleUpdateRun=|var masterUpdateRun=|try\{var before=|run\.finalizing=true;|}catch\(e\)\{if\(run\.cancelled)/.test(js)) scripts.push(js);
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

async function exerciseMasterFailure(stage) {
  const statuses=[], requests=[];
  let checks=0;
  const file={name:'OpenFumeExtractorMaster.bin',size:1000,slice:(a,b)=>({size:b-a})};
  const form={querySelector(selector){return selector==='input[type=file]'?{files:[file]}:{checked:false};}};
  const stat={set textContent(v){statuses.push(v);},get textContent(){return statuses.at(-1);}};
  const context=vm.createContext({UI_DE:true,DEV_MODE:false,MASTER_FW_CHUNK:4096,
    performance:{now:()=>100000},document:{getElementById:()=>({style:{}})},
    fetch:async(url)=>{requests.push(url);if(stage==='end-lost'&&url==='/update/master/end')throw new TypeError('NetworkError');
      if(stage==='end-rejected'&&url==='/update/master/end')return {ok:false,text:async()=> 'Update.end failed'};
      return {ok:true,text:async()=> 'OK'};},
    setTimeout,clearTimeout,encodeURIComponent,console});
  vm.runInContext(scripts.join('\n'),context);
  context.findFwAuth=async()=>({size:1000,target:'MASTER',version:'2.0.18RC',text:'signed'});
  context.findFwSig=async()=> 'OFE_FW_SIG:v1;target=MASTER;version=2.0.18RC;';
  context.getState=async()=>({master_fw:'2.0.17RC',uptime_ms:60000});
  context.postBlob=async()=>{if(stage==='chunk-lost')throw new TypeError('NetworkError');return '';};
  context.waitMasterBack=async()=>{++checks;return true;};
  await context.sendMaster(form,{style:{}},stat);
  if(stage==='end-lost'){
    assert.equal(checks,1,'Lost END acknowledgement must trigger version verification');
    assert.equal(requests.includes('/update/master/abort'),false,'Committed update must not be aborted');
    assert.equal(statuses.some(s=>s.includes('Master-Update fehlgeschlagen')),false);
  } else {
    assert.equal(checks,0,'Pre-commit failure must not be called successful');
    assert.equal(requests.includes('/update/master/abort'),true);
    assert.match(stat.textContent,/Master-Update fehlgeschlagen/);
  }
}

async function exerciseMasterRestartVerification(state,oldVersion,oldUptime,expected) {
  const redirects=[];
  let lastStatus='';
  const context=vm.createContext({UI_DE:true,performance:{now:()=>120000},
    fetch:async()=>({ok:true,json:async()=>state}),
    location:{set href(v){redirects.push(v);}},setTimeout,console});
  vm.runInContext(scripts.join('\n'),context);
  context.sleep=async()=>{};
  const confirmed=await context.waitMasterBack({set textContent(v){lastStatus=v;}},expected,oldVersion,oldUptime,0);
  assert.equal(confirmed,redirects.length===1);
  return {confirmed,lastStatus};
}

(async()=>{
  const targets=vm.createContext({});
  vm.runInContext(scripts.join('\n'),targets);
  const p4Caps=8388608|268435456;
  const sig=target=>'OFE_FW_SIG:v1;target='+target+';version=1.0.0alpha-P4Prep;';
  for(const [caps,target] of [[4194304,'DISPLAY_320X480'],[8388608,'DISPLAY_800X480'],
      [4194304|134217728,'DISPLAY_ST7796_320X480'],[p4Caps,'DISPLAY_P4_800X480']]) {
    assert.equal(targets.fwTargetOk(6,target,caps,false),true);
    assert.equal(targets.fwSigOk(6,sig(target),caps),true);
    assert.equal(targets.fwExpectedSig(6,caps),'OFE_FW_SIG:v1;target='+target+';');
    for(const wrong of ['MASTER','DISPLAY','DISPLAY_320X480','DISPLAY_800X480','DISPLAY_ST7796_320X480','DISPLAY_P4_800X480']) {
      if(wrong===target)continue;
      assert.equal(targets.fwTargetOk(6,wrong,caps,false),false);
      assert.equal(targets.fwSigOk(6,sig(wrong),caps),false);
    }
  }
  assert.equal(targets.fwSigTargetLabel(sig('DISPLAY_P4_800X480')),'Display P4 800x480');
  const abortRequests=[];
  const ownerContext=vm.createContext({fetch:async url=>{
    abortRequests.push(url);return {ok:true,text:async()=>''};
  }});
  vm.runInContext(scripts.join('\n'),ownerContext);
  await ownerContext.abortOwnedUpdate({started:false,beginPromise:Promise.resolve({ok:false})},'/abort');
  assert.equal(abortRequests.length,0,'Rejected begin must not abort another upload');
  let resolveBegin;
  const run={started:false,beginPromise:new Promise(resolve=>{resolveBegin=resolve;})};
  const cancel=ownerContext.abortOwnedUpdate(run,'/abort');
  assert.equal(abortRequests.length,0,'Cancel must wait for begin ownership');
  resolveBegin({ok:true});
  await cancel;
  assert.deepEqual(abortRequests,['/abort']);
  for (const de of [false,true]) for (const developer of [false,true]) {
    await exercise(de,developer,'wifi','rs485');
    await exercise(de,developer,'rs485','wifi');
  }
  await exercise(true,false,'wifi','wifi',false);
  for(const stage of ['end-lost','end-rejected','chunk-lost'])await exerciseMasterFailure(stage);
  assert.equal((await exerciseMasterRestartVerification({master_fw:'2.0.18RC',uptime_ms:4000},'2.0.17RC',60000,'2.0.18RC')).confirmed,true);
  assert.equal((await exerciseMasterRestartVerification({master_fw:'2.0.17RC',uptime_ms:120000},'2.0.17RC',60000,'2.0.18RC')).confirmed,false);
  assert.equal((await exerciseMasterRestartVerification({master_fw:'2.0.18RC',uptime_ms:4000},'2.0.18RC',60000,'2.0.18RC')).confirmed,true);
  assert.equal((await exerciseMasterRestartVerification({master_fw:'2.0.18RC',uptime_ms:180000},'2.0.18RC',60000,'2.0.18RC')).confirmed,false);
  assert.equal((await exerciseMasterRestartVerification({master_fw:'2.0.18RC',uptime_ms:119744},'2.0.18RC',0xFFFFFF00,'2.0.18RC')).confirmed,false);
  console.log('PASS: embedded update JS syntax, live transport labels DE/EN, developer-only chunk size and missing-signature rejection.');
})().catch(error=>{console.error(error);process.exitCode=1;});
