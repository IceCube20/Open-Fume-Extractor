'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '../..');
const sharedHeader = fs.readFileSync(path.join(root, 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebAutomationSignals.h'), 'utf8');
const shared = sharedHeader.match(/<script>([\s\S]*?)<\/script>/)[1];
const status = fs.readFileSync(path.join(root, 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebStatus.inc.h'), 'utf8');
const logic = fs.readFileSync(path.join(root, 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebLogic.inc.h'), 'utf8');
const line = (source, name) => {
  const row=source.split(/\r?\n/).find(row=>row.includes(`function ${name}(`));
  assert(row, name);
  return row.startsWith(`async function ${name}(`)?row:row.slice(row.indexOf(`function ${name}(`));
};
const modules = [
  {addr:16,type:1,caps:1}, {addr:17,type:9,caps:16777216}, {addr:32,type:3,caps:65536},
  {addr:48,type:5,caps:512}, {addr:64,type:6,caps:1024|536870912}, {addr:65,type:6,caps:1024|536870912},
  {addr:80,type:7,caps:65536}, {addr:96,type:8,caps:65536},
].map(module => ({...module, online:true, system_inputs_mask:0}));
modules[0].label='FAE DDE alias';modules[1].label='USB CLMU alias';
const state = {modules,jbc_inputs_count:2,system_inputs_mask:(1<<11)|(1<<12)|(1<<14),automation_markers:0x81};
const context = vm.createContext({uiLang:'de',liveState:state,signals:{inputs:[],outputs:[]},
  logicDe:()=>context.uiLang==='de', logicDescriptorCache:{}, universalDescriptorCache:{}, t:(de,en)=>en===undefined?de:context.uiLang==='de'?de:en,
  amn:module=>`0x${module.addr.toString(16)} ${module.label||'Module'}`, escAttr:String, escHtml:String,
  hx:addr=>'0x'+Number(addr).toString(16),
  universalDefsForModule:()=>[], fanIoLogicChannels:()=>({inputs:[],outputs:[],main:false}),
  fanIoDef:()=>null, fanIoRouteOutputs:()=>[], fanIoMainRouteEntity:()=>null,
  universalInputDefs:()=>[], universalMainInputDefs:()=>[], universalTargetDefs:()=>[],
  fanIoMainConfigured:()=>false, universalOutputDefs:()=>[], ioInputLabel:()=>'IN',
  moduleLogicName:module=>module.label||'Module', logicModuleGroup:()=>'',
  fetch:async url=>({ok:true,json:async()=>url==='/state'?state:{modules:[]}}),
  document:{getElementById:()=>null}, window:{lastState:state},
});
vm.runInContext(shared,context);
for (const name of ['dec','optionHas','automationRuleOptions','routingSignals','extractorActionLabel','fanIoRouteInputs','sourceOptions','targetOptions','ruleLabel','systemRuleSourceLabel','ruleAvailable']) {
  vm.runInContext(line(status,name),context);
}
vm.runInContext(line(logic,'extractorActionSignals'),context);
vm.runInContext(line(logic,'loadSignals'),context);
vm.runInContext(line(logic,'logicHydrateSignalMetadata'),context);
vm.runInContext(line(logic,'logicBuildSignals'),context);
vm.runInContext(line(logic,'moduleAddr'),context);
vm.runInContext(line(logic,'liveSignalInfo'),context);
vm.runInContext(status.slice(status.indexOf('function mainInputText('),status.indexOf('function mainOutputText(')),context);
context.liveModule=addr=>modules.find(m=>m.addr===Number(addr));
context.logicUniversalDefs=()=>[];
const optionValues = html => [...html.matchAll(/<option value="([^"]+)"/g)].map(match=>match[1]);

async function test() {
  for(const lang of ['de','en']) {
    context.uiLang=lang;
    await context.loadSignals();
    const catalogue=context.ofeAutomationSignals(state,lang,context.ofeSignalModuleLabel);
    const source=optionValues(context.sourceOptions(state,'0,0,0'));
    const target=optionValues(context.targetOptions(state,'0,0,0'));
    const routing=context.ofeRoutingSignals(state,lang);
    for(const addr of [0,16,17]) {
      const work=routing.inputs.find(item=>item.id===`jbc:${addr}`);
      assert(work&&work.name.endsWith(' - JBC Work'),`Explicit Work label for ${addr}`);
      assert.equal(context.signals.inputs.find(item=>item.id===work.id).name,work.name,'Logic and routing use identical Work labels');
      assert.equal(routing.outputs.find(item=>item.id===`extractor:select_input:1:${addr}:0`).name,work.name);
      assert.equal(routing.inputs.find(item=>item.id===`state:input:selected:1:${addr}:0`).name,work.name);
      assert(context.mainInputText({...state,main_input_source_type:1,main_input_source_addr:addr}).endsWith(' - JBC Work'));
      const offline={modules:modules.filter(m=>Number(m.addr)===addr).map(m=>({...m,online:false})),jbc_inputs_count:0,
        main_input_source_type:1,main_input_source_addr:addr,main_input_source_bit:0};
      assert(context.ofeRoutingSignals(offline,lang).inputs.find(item=>item.id===`state:input:selected:1:${addr}:0`).name.endsWith(' - JBC Work'));
      assert(context.ruleLabel(offline,{source_type:1,source_addr:addr,source_bit:0},'source').endsWith(' - JBC Work'));
    }
    assert(routing.inputs.find(item=>item.id==='jbc:16').name.includes('FAE DDE alias'));
    assert(routing.inputs.find(item=>item.id==='jbc:17').name.includes('USB CLMU alias'));
    for(const addr of [0,16,17]){
      const stand=routing.inputs.find(item=>item.id===`state:system:20:${addr}`);
      assert.equal(stand.name,routing.inputs.find(item=>item.id===`jbc:${addr}`).name.replace(/ - JBC Work$/,' - JBC Stand'));
      assert.equal(context.signals.inputs.find(item=>item.id===stand.id).name,stand.name,'Logic and routing use identical Stand labels');
    }
    for(const count of [0,1,3]){
      const stand=context.ofeAutomationSignals({...state,jbc_inputs_count:count},lang,context.ofeSignalModuleLabel).inputs.find(item=>item.id==='state:system:20:0');
      assert.equal(stand.name,`${lang==='de'?'Alle JBC-Module':'All JBC modules'} (${count}) - JBC Stand`);
      assert.equal(stand.source_type,4);assert.equal(stand.source_addr,0);assert.equal(stand.source_bit,20);
    }
    for(const item of catalogue.inputs){
      assert(source.includes([item.source_type,item.source_addr,item.source_bit].join(',')),item.id);
      assert(context.signals.inputs.some(input=>input.id===item.id),`Logic missing ${item.id}`);
      assert(context.ruleAvailable(state,{enabled:true,source_type:item.source_type,source_addr:item.source_addr,source_bit:item.source_bit,target_type:7,target_addr:0,target_bit:1}),item.id);
    }
    for(const item of catalogue.outputs){
      assert(target.includes([item.target_type,item.target_addr,item.target_bit].join(',')),item.id);
      assert(context.signals.outputs.some(output=>output.id===item.id),`Logic missing ${item.id}`);
      assert.equal(context.ruleLabel(state,item,'target'),item.name);
    }
    assert.equal(catalogue.inputs.filter(item=>item.source_type===5).length,8);
    assert.equal(catalogue.outputs.filter(item=>item.target_type===8).length,8);
    assert.equal(catalogue.outputs.filter(item=>item.id.startsWith('system:display:wake:')).length,3);
    assert(!context.sourceOptions(state,'0,0,0',false).includes('value="4,0,11"'));
    assert(!context.sourceOptions(state,'0,0,0',false).includes('value="5,0,1"'));
    assert(context.ofeAutomationInputInfo(state,'system:marker:1').on);
    assert(!context.ofeAutomationInputInfo(state,'system:marker:2').on);
    assert(context.ofeAutomationInputInfo(state,'system:marker:8').on);
    assert(context.liveSignalInfo('state:system:11:0').on);
    assert(!context.liveSignalInfo('state:system:13:0').on);
    assert(!context.liveSignalInfo('state:system:15:48').known,'Missing filter validity must not look healthy');
    modules[3].system_inputs_mask=(1<<17)|(1<<15);
    assert(context.liveSignalInfo('state:system:15:48').known);
    assert(context.liveSignalInfo('state:system:15:48').on);
    modules[3].system_inputs_mask=0;
  }
  const before=state.modules[4].label;
  state.modules[4].label='<img src=x onerror=alert(1)>';
  const named=context.ofeAutomationSignals(state,'de',context.amn).outputs.find(item=>item.id==='system:display:wake:64');
  assert(named.name.includes('<img'),'Catalogue must preserve names, not inject HTML');
  state.modules[4].label=before;
  console.log('PASS: routing/boolean catalogue parity, explicit Work vs Stand labels in main inputs/actions/selected states, FAE/USB aliases and offline fallbacks, DE/EN, eight markers, display actions and filter validity');

  if(process.argv.includes('--browser')) {
    const {chromium}=require('playwright');
    const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
    const output=path.join(__dirname,'build-checks/system-automation');fs.mkdirSync(output,{recursive:true});
    try {
      const css=status.match(/<style>([\s\S]*?)<\/style>/)[1];
      const typography=JSON.parse(fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebTypography.h'),'utf8').match(/#define OFE_WEB_TYPOGRAPHY_STYLE (".*")/)[1]);
      for(const width of [320,390,1440]) {
        const page=await browser.newPage({viewport:{width,height:900}});
        context.uiLang='de';
        const source=context.sourceOptions(state,'1,17,0'),target=context.targetOptions(state,'7,0,3');
        await page.setContent(`<style>${css}</style>${typography}<main><section class="control-panel routing-panel"><div class="routing-rule"><span class="rule-number">1</span><select aria-label="Source">${source}</select><span class="rule-arrow">&rarr;</span><select aria-label="Target">${target}</select><button class="secondary">Entfernen</button></div></section></main>`);
        assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));
        assert(await page.evaluate(()=>{const a=document.querySelectorAll('select')[1].getBoundingClientRect(),b=document.querySelector('button').getBoundingClientRect();return a.right<=b.left||a.bottom<=b.top||b.bottom<=a.top;}),'Controls must not overlap');
        assert.equal(await page.locator('select').first().inputValue(),'1,17,0');
        assert((await page.locator('select').first().locator('option:checked').textContent()).endsWith(' - JBC Work'));
        assert.equal(await page.locator('select').last().inputValue(),'7,0,3');
        await page.screenshot({path:path.join(output,`routing-${width}.png`),fullPage:true});
        await page.locator('select').first().selectOption('5,0,8');
        await page.locator('select').last().selectOption('7,64,2');
        await page.close();
      }
      console.log('PASS: actual routing controls/CSS in browser at 320, 390 and 1440px');
    } finally {await browser.close();}
  }
}
test().catch(error=>{console.error(error);process.exitCode=1;});
