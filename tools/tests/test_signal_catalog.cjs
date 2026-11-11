'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/src');
const status=fs.readFileSync(path.join(src,'WebStatus.inc.h'),'utf8');
const logic=fs.readFileSync(path.join(src,'WebLogic.inc.h'),'utf8');
const shared=fs.readFileSync(path.join(src,'WebAutomationSignals.h'),'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const clone=value=>JSON.parse(JSON.stringify(value));
const getFunction=(source,name)=>{const row=source.split(/\r?\n/).find(row=>row.includes(`function ${name}(`));assert(row,name);return row.startsWith(`async function ${name}(`)?row:row.slice(row.indexOf(`function ${name}(`));};
const fanDescriptor=['fan_enabled=1',...Array.from({length:8},(_,bit)=>`${20+bit} ${bit===3?'switch':'binary_sensor'} in${bit+1} ro source=profile role=${bit===0||bit===7?'main_input':'none'} de=Taster Absaugung ${bit+1} en=Extraction button ${bit+1}`),
  ...Array.from({length:8},(_,bit)=>`${40+bit} switch out${bit+1} rw source=profile de=LED Absaugung ${bit+1} en=Extraction LED ${bit+1}`),
  '60 switch fan rw role=main_output_enable de=Hauptluefter en=Main fan'].join('\n');
const bridgeDescriptor=['20 binary_sensor work ro role=main_input de=Stations Freigabe en=Station enable',
  '21 switch aux rw de=Arbeitsplatz Beleuchtung en=Workplace lighting',
  '22 switch only_write wo de=Ausgang ohne Rueckmeldung en=Output without feedback',
  '23 button reset wo de=Zaehler zuruecksetzen en=Reset counter',
  '24 binary_sensor blower ro role=main-output-enable de=Haupt Absaugung en=Main extraction',
  '25 sensor power ro role=main_output_power de=Leistung en=Power',
  '26 sensor temperature ro de=Temperatur en=Temperature'].join('\n');
const data={jbc_inputs_count:2,modules:[
  {addr:16,type:1,caps:1,label:'FAE am Loetplatz',display_name:'Wrong stale FAE name',jbc_work_mask:0},
  {addr:17,type:9,caps:16777216,label:'USB Stationen',jbc_work_mask:1},
  {addr:32,type:3,caps:65536|2048|16384|36,label:'Fan/IO Pro Tisch',io_in1_alias:'Taster Haupt Absaugung',io_out1_alias:'LED Haupt Absaugung',io_main_alias:'Absaugung Tisch'},
  {addr:33,type:2,caps:65536|2048|16384,label:'Fan/IO Nebenplatz'},
  {addr:34,type:3,caps:65536|2048|16384|36,label:'Offline Fan',online:false},
  {addr:48,type:5,caps:512|16384,label:'Weller Werkbank'},
  {addr:64,type:6,caps:1024|536870912,label:'Display <Buero & Test>'},
  {addr:80,type:7,caps:65536,label:'Universal Geraet'},
  {addr:96,type:8,caps:65536,label:'Modbus Maschine'},
  {addr:97,type:4,caps:2048|16384,label:'Legacy I/O',io_in1_alias:'Freigabe Altgeraet',io_out2_alias:'Warnleuchte Altgeraet'},
].map(m=>({online:true,name:'Factory module',type_name:'Module',system_inputs_mask:0,universal_descriptor_crc:1000+m.addr,...m}))};
const descriptors={modules:data.modules.filter(m=>m.caps&65536).map(m=>({addr:m.addr,universal_descriptor_crc:m.universal_descriptor_crc,universal_descriptor_valid:true,
  universal_descriptor:m.addr===32?fanDescriptor:m.addr===33?'fan_enabled=0\n20 switch input ro de=Eingang Nebenplatz en=Side input\n46 switch output wo de=Ausgang Sieben en=Output seven':m.addr===34?fanDescriptor:bridgeDescriptor}))};
let apiState=clone(data),apiDescriptors=clone(descriptors),descriptorFailure=false,stateFailure=false;
const c=vm.createContext({uiLang:'de',logicDescriptorCache:{},universalDescriptorCache:{},signals:{inputs:[],outputs:[]},
  liveState:null,window:{lastState:data},document:{getElementById:()=>null},console:{warn:()=>{}},
  logicDe:()=>c.uiLang==='de',t:(de,en)=>en===undefined?de:c.uiLang==='de'?de:en,
  fetch:async url=>({ok:url==='/state'?!stateFailure:!descriptorFailure,status:stateFailure?503:200,json:async()=>clone(url==='/state'?apiState:apiDescriptors)}),
});
vm.runInContext(shared,c);
for(const name of ['escHtml','escAttr','routingSignals','dec','optionHas','ruleLabel','sourceOptions','targetOptions'])vm.runInContext(getFunction(status,name),c);
for(const name of ['logicHydrateSignalMetadata','logicBuildSignals','loadSignals','logicSignalOption','renderLogicSignalOptions','liveModule','liveSignalInfo'])vm.runInContext(getFunction(logic,name),c);
const routeKey=(item,side)=>side==='inputs'?[item.source_type,item.source_addr,item.source_bit].join(','):[item.target_type,item.target_addr,item.target_bit].join(',');
const names=list=>list.map(x=>[x.id,x.name,x.group||'']);

async function run(){
  for(const lang of ['de','en']){
    c.uiLang=lang;
    c.universalDescriptorCache=Object.fromEntries(apiDescriptors.modules.map(m=>[m.addr,{crc:m.universal_descriptor_crc,text:m.universal_descriptor}]));
    await c.loadSignals();
    const routing=c.routingSignals(apiState);
    assert.deepEqual(names(c.signals.inputs.filter(x=>x.id!=='manual')),names(routing.inputs));
    assert.deepEqual(names(c.signals.outputs),names(routing.outputs));
    const source=c.sourceOptions(apiState,'0,0,0'),target=c.targetOptions(apiState,'0,0,0');
    for(const [side,html] of [['inputs',source],['outputs',target]])for(const item of routing[side])assert(html.includes(`value="${routeKey(item,side)}">${c.ofeSignalEscape(item.name)}</option>`),item.id);
    assert(routing.inputs.some(x=>x.id==='jbc:0'),'All JBC input must not be missing');
    assert(c.liveSignalInfo('jbc:0').on,'Aggregate JBC live feedback');
    const savedLive=c.liveState;
    c.liveState={...savedLive,preferred_output_addr:32,active_output_addr:32,output_enabled:false};
    assert(c.liveSignalInfo('state:output:selected:32').on);
    assert(!c.liveSignalInfo('state:output:active:32').on,'Idle assignment is not active extraction');
    c.liveState.output_enabled=true;
    assert(c.liveSignalInfo('state:output:active:32').on);
    c.liveState.afterrun_ms=1000;
    assert(c.liveSignalInfo('state:output:active:32').on,'Afterrun remains active');
    c.liveState.output_enabled=false;c.liveState.afterrun_ms=0;
    assert(!c.liveSignalInfo('state:output:active:32').on);
    c.liveState.preferred_output_addr=0;
    assert(c.liveSignalInfo('state:output:selected:auto').on);
    assert(!c.liveSignalInfo('state:output:selected:32').on);
    c.liveState.output_enabled=true;
    assert(c.liveSignalInfo('state:output:active:32').on,'Automatic selection can run without a manual preference');
    c.liveState.preferred_output_addr=32;c.liveState.active_output_addr=48;
    assert(c.liveSignalInfo('state:output:selected:32').on);
    assert(!c.liveSignalInfo('state:output:active:32').on);
    assert(c.liveSignalInfo('state:output:active:48').on);
    c.liveState=savedLive;
    for(let bit=0;bit<8;bit++){
      assert(routing.inputs.some(x=>x.source_type===3&&x.source_addr===32&&x.source_bit===20+bit));
      assert(routing.outputs.some(x=>x.target_type===3&&x.target_addr===32&&x.target_bit===40+bit));
    }
    assert(routing.inputs.some(x=>x.id==='io:32:in:3'),'Readable switch input is valid too');
    assert(routing.outputs.some(x=>x.id==='io:32:2'&&x.name==='0x20 Fan/IO Pro Tisch - LED Haupt Absaugung'),'Keep saved physical output ID and full alias');
    assert(routing.inputs.some(x=>x.id==='io:32:0'&&x.name==='0x20 Fan/IO Pro Tisch - Taster Haupt Absaugung'));
    assert(routing.inputs.some(x=>x.name.endsWith(lang==='de'?'Taster Absaugung 8':'Extraction button 8')),'Descriptor alias with spaces');
    assert(!routing.outputs.some(x=>x.id==='io:33:main'),'Disabled main output');
    assert(!routing.inputs.some(x=>x.id==='io:33:1'),'No invented channel');
    assert(!routing.outputs.some(x=>x.id.startsWith('io:34:')),'Offline physical output');
    assert(routing.inputs.some(x=>x.id==='state:system:18:34'),'Offline-state monitoring remains available');
    assert(routing.outputs.some(x=>x.id==='weller:48:light'));
    assert(routing.inputs.some(x=>x.id==='io:97:0'&&x.name.endsWith('Freigabe Altgeraet')));
    for(const addr of [80,96]){
      assert(routing.inputs.some(x=>x.id===`uni:${addr}:entity:20`),'Profile input loaded from descriptor endpoint');
      assert(routing.outputs.some(x=>x.id===`uni:${addr}:switch:22`),'Write-only profile output');
      assert(!routing.inputs.some(x=>x.id===`uni:${addr}:entity:22`),'Write-only is not readable');
      assert(routing.outputs.some(x=>x.id===`uni:${addr}:button:23`),'Profile button');
      assert(routing.outputs.some(x=>x.id===`uni:${addr}:switch:24`),'Normalized profile role');
      assert(routing.outputs.some(x=>x.id===`extractor:select_output:${addr}`),'Profile main output');
      assert(!routing.inputs.some(x=>x.id===`uni:${addr}:entity:26`),'Numeric sensors are not booleans');
    }
    assert(!source.includes('Wrong stale FAE name'),'Module alias takes priority');
  }
  const previous=names(c.signals.inputs);
  descriptorFailure=true;await c.loadSignals();
  assert.deepEqual(names(c.signals.inputs),previous,'Matching CRC cache survives temporary descriptor failure');
  apiState.modules.find(m=>m.addr===80).universal_descriptor_crc++;
  await c.loadSignals();
  assert(!c.signals.inputs.some(x=>x.id==='uni:80:entity:20'),'Changed CRC must not reuse stale entities');
  descriptorFailure=false;
  apiDescriptors.modules.find(m=>m.addr===80).universal_descriptor_crc++;
  apiDescriptors.modules.find(m=>m.addr===80).universal_descriptor='30 binary_sensor replacement ro de=Neue Freigabe en=New enable';
  await c.loadSignals();
  assert(c.signals.inputs.some(x=>x.id==='uni:80:entity:30'));
  assert(!c.signals.inputs.some(x=>x.id==='uni:80:entity:20'));
  stateFailure=true;const good=c.signals.inputs;await c.loadSignals();assert.equal(c.signals.inputs,good,'State failure must not wipe the list');stateFailure=false;
  apiState=clone(data);apiDescriptors=clone(descriptors);await c.loadSignals();
  console.log('PASS: complete Designer/routing parity, aliases, descriptor-only profiles, eight I/O channels, IDs, access modes, offline modules and CRC cache changes');
  if(!process.argv.includes('--browser'))return;
  const {chromium}=require('playwright');
  const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  const output=path.join(__dirname,'build-checks/signal-catalog');fs.mkdirSync(output,{recursive:true});
  try{
    for(const lang of ['de','en'])for(const width of [390,1440]){
      c.uiLang=lang;await c.loadSignals();
      c.universalDescriptorCache=Object.fromEntries(apiDescriptors.modules.map(m=>[m.addr,{crc:m.universal_descriptor_crc,text:m.universal_descriptor}]));
      const catalogue=c.routingSignals(apiState);
      const page=await browser.newPage({viewport:{width,height:900}});
      await page.setContent('<style>body{margin:20px;background:#171a1f;color:#eee;font:14px Arial,sans-serif}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,320px),1fr));gap:24px}section{min-width:0}select{width:100%;height:44px;padding:8px;color:inherit;background:#101318;border:1px solid #4b5665;border-radius:4px;margin:8px 0 20px;font:inherit}label{display:block}</style><main><section><h2>Routing</h2><label>Input<select id="ri"></select></label><label>Output<select id="ro"></select></label></section><section><h2>Designer</h2><label>Input<select id="di"></select></label><label>Output<select id="do"></select></label></section></main>');
      await page.addScriptTag({content:shared+'\n'+getFunction(logic,'logicSignalOption')+'\n'+getFunction(logic,'renderLogicSignalOptions')+'\nwindow.t=(a,b)=>a;'});
      const inputHtml=c.sourceOptions(apiState,'3,32,27'),outputHtml=c.targetOptions(apiState,'3,80,22');
      await page.evaluate(({inputHtml,outputHtml,inputs,outputs})=>{
        document.querySelector('#ri').innerHTML=inputHtml;document.querySelector('#ro').innerHTML=outputHtml;
        renderLogicSignalOptions(document.querySelector('#di'),inputs,'io:32:in:7');
        renderLogicSignalOptions(document.querySelector('#do'),outputs,'uni:80:switch:22');
      },{inputHtml,outputHtml,inputs:clone(c.signals.inputs),outputs:clone(c.signals.outputs)});
      const actual=await page.evaluate(()=>Object.fromEntries(['ri','ro','di','do'].map(id=>[id,[...document.querySelector('#'+id).options].map(o=>({value:o.value,name:o.textContent,group:o.parentElement.tagName==='OPTGROUP'?o.parentElement.label:''}))])));
      for(const [side,routeId,designerId] of [['inputs','ri','di'],['outputs','ro','do']]){
        const expected=clone(catalogue[side]).map(x=>({value:routeKey(x,side),name:x.name,group:x.group||''}));
        const mapped=actual[designerId].filter(x=>x.value!=='manual').map(x=>({...x,value:routeKey(catalogue[side].find(y=>y.id===x.value),side)}));
        const routes=actual[routeId].filter(x=>x.value!=='0,0,0');
        assert.deepEqual(mapped,routes);
        assert.equal(expected.length,routes.length);
      }
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
      assert.equal(await page.locator('img').count(),0,'Module names must remain text');
      await page.screenshot({path:path.join(output,`${lang}-${width}.png`),fullPage:true});
      await page.close();
    }
    console.log('PASS: actual rendered optgroups/options match in both editors, DE/EN at 390/1440px');
  }finally{await browser.close();}
}
run().catch(error=>{console.error(error);process.exitCode=1;});
