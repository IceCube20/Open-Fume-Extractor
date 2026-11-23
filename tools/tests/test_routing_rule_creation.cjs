'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('playwright');
const root = path.resolve(__dirname, '../..');
const folders = ['OpenFumeExtractorMCU_S3', 'OpenFumeExtractorMCU_P4', 'OpenFumeExtractorMCU_P4_POE'];

function extract(source, name, multiline = false) {
  const row = source.split(/\r?\n/).find(row => row.includes(`function ${name}(`));
  assert(row, name);
  const start = source.indexOf(row) + row.indexOf(row.includes(`async function ${name}(`) ? 'async function ' + name : 'function ' + name);
  if (multiline) return source.slice(start, source.indexOf('\n}', start) + 2);
  return source.slice(start, source.indexOf('\n', start));
}

async function main() {
  const browser = await chromium.launch({headless:true, executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try {
    for (const folder of folders) {
      const dir = path.join(root, 'OpenFumeExtractorMaster', folder, 'src');
      const source = fs.readFileSync(path.join(dir, 'WebStatus.inc.h'), 'utf8');
      const shared = fs.readFileSync(path.join(dir, 'WebAutomationSignals.h'), 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
      const css = source.match(/<style>([\s\S]*?)<\/style>/)[1];
      const functions = ['enc','dec','moduleSig','routeSig','visibleRules','routingSignals','optionHas',
        'automationRuleOptions','sourceOptions','targetOptions','ruleLabel','systemRuleSourceLabel',
        'renderRules','updateRules','canRender','isFormField','fieldBusy','val','clearFieldDirty',
        'universalAddrFromField','routingSaveOk','setRoutingRuleBusy'].map(name => extract(source, name)).join('\n');
      const actions = ['ruleChanged','addRule','deleteRule'].map(name => extract(source, name, true)).join('\n');
      for (const lang of ['de','en']) for (const width of [320,1440]) {
        const page = await browser.newPage({viewport:{width,height:900}});
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.setContent(`<style>${css}</style><main><section class="control-panel routing-panel"><select id="main_input_sel"><option value="0,0,0">No input</option><option value="1,0,0">JBC</option></select><div id="rule_box"></div><button class="route-add secondary" onclick="addRule()">Add rule</button></section></main>`);
        await page.addScriptTag({content:`
          let uiLang=${JSON.stringify(lang)},routingSig='';
          const routingRuleDrafts={},routingRuleWrites=new Set(),universalDescriptorCache={};
          const t=key=>({no_input:uiLang==='de'?'Kein Eingang':'No input',no_output:uiLang==='de'?'Kein Ausgang':'No output',remove:uiLang==='de'?'Entfernen':'Remove',extractor_output:'Extractor'})[key]||key;
          const escHtml=value=>String(value).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;');
          const amn=()=>'',universalDirtyBadge=()=>{};
          window.alerts=[];window.alert=message=>alerts.push(message);
          const emptyRule=()=>({enabled:false,source_type:0,source_addr:0,source_bit:0,target_type:0,target_addr:0,target_bit:0});
          window.serverRules=Array.from({length:16},emptyRule);window.calls=[];window.requestMode='ok';window.releaseWrite=null;window.pauseRefresh=false;
          const snapshot=()=>({modules:[],input_rules:JSON.parse(JSON.stringify(serverRules))});
          window.lastState=snapshot();
          window.fetch=async url=>{
            const p=new URL(url,'http://ofe.test').searchParams;calls.push(url);
            if(requestMode==='network')throw Error('offline');
            if(requestMode==='http')return {ok:false,text:async()=> 'failed to persist input rule'};
            if(requestMode==='hold')await new Promise(resolve=>window.releaseWrite=resolve);
            const r=emptyRule();
            for(const [field,key] of [['source_type','st'],['source_addr','sa'],['source_bit','sb'],['target_type','tt'],['target_addr','ta'],['target_bit','tb']])r[field]=Number(p.get(key));
            r.enabled=!!(r.source_type&&r.target_type);serverRules[Number(p.get('idx'))]=r;
            return {ok:true};
          };
          function load(){if(pauseRefresh)return;window.lastState=snapshot();updateRules(window.lastState)}
          ${shared}\n${functions}\n${actions}
          renderRules(window.lastState);
        `});
        await page.locator('.route-add').click();
        assert.equal(await page.locator('.routing-rule').count(),1);
        assert.equal(await page.locator('#rule_s_0').inputValue(),'0,0,0');
        assert.equal(await page.locator('#rule_t_0').inputValue(),'1,0,0');
        assert.equal(await page.evaluate(()=>calls.length),0,'Adding a blank rule must not store a disabled invisible rule');
        await page.evaluate(()=>{for(let i=0;i<10;i++)updateRules(window.lastState)});
        assert.equal(await page.locator('.routing-rule').count(),1,'Draft survives status polling');
        await page.locator('#rule_t_0').selectOption('0,0,0');
        await page.locator('#rule_s_0').selectOption('5,0,1');
        await page.evaluate(()=>{document.activeElement.blur();updateRules(window.lastState)});
        assert.equal(await page.locator('#rule_s_0').inputValue(),'5,0,1');
        assert.equal(await page.evaluate(()=>calls.length),0,'Incomplete draft must not activate an output');
        await page.evaluate(()=>requestMode='http');
        await page.locator('#rule_t_0').selectOption('8,0,1');
        await page.waitForFunction(()=>alerts.length===1&&routingRuleWrites.size===0);
        assert.equal(await page.locator('.routing-rule').count(),1,'Failed save keeps the editable draft');
        assert.equal(await page.evaluate(()=>serverRules[0].enabled),false);
        await page.evaluate(()=>requestMode='network');
        await page.locator('#rule_t_0').selectOption('8,0,2');
        await page.waitForFunction(()=>alerts.length===2&&routingRuleWrites.size===0);
        await page.evaluate(()=>requestMode='hold');
        await page.locator('#rule_t_0').selectOption('8,0,1');
        await page.waitForFunction(()=>!!window.releaseWrite);
        assert(await page.locator('#rule_s_0').isDisabled());
        await page.evaluate(()=>{updateRules(window.lastState);addRule();deleteRule(0);ruleChanged(0)});
        assert.equal(await page.evaluate(()=>calls.length),3,'Busy guard prevents overlapping writes and drafts');
        await page.evaluate(()=>releaseWrite());
        await page.waitForFunction(()=>routingRuleWrites.size===0&&serverRules[0].enabled);
        await page.evaluate(()=>{document.activeElement.blur();updateRules(window.lastState)});
        assert.equal(await page.locator('.routing-rule').count(),1,'Old status cannot hide a just-saved draft');
        await page.waitForFunction(()=>window.lastState.input_rules[0].enabled);
        assert.equal(await page.evaluate(()=>Object.keys(routingRuleDrafts).length),0,'Authoritative state replaces saved draft');
        assert.equal(await page.locator('#rule_s_0').inputValue(),'5,0,1');
        await page.locator('#main_input_sel').selectOption('1,0,0');
        await page.locator('.route-add').click();
        assert.equal(await page.locator('#rule_s_1').inputValue(),'0,0,0','A new rule is independent of the main input');
        const beforeCancel = await page.evaluate(()=>calls.length);
        await page.locator('.routing-rule').nth(1).locator('button').click();
        assert.equal(await page.evaluate(()=>calls.length),beforeCancel,'Canceling an unsaved draft does not write flash');
        await page.evaluate(()=>{requestMode='ok';pauseRefresh=true});
        await page.locator('.route-add').click();
        await page.locator('#rule_s_1').selectOption('5,0,2');
        await page.waitForFunction(()=>routingRuleDrafts[1]?.persisted&&routingRuleWrites.size===0);
        await page.locator('#rule_t_1').selectOption('0,0,0');
        await page.locator('.routing-rule').nth(1).locator('button').click();
        await page.waitForFunction(()=>routingRuleWrites.size===0&&!serverRules[1].enabled);
        assert.equal(await page.evaluate(()=>calls.length),beforeCancel+2,'Deleting a saved but unconfirmed draft must clear its persisted rule');
        await page.evaluate(()=>{pauseRefresh=false;load()});
        assert.equal(await page.locator('.routing-rule').count(),1);
        if (folder===folders[0]&&lang==='de') {
          const out=path.join(__dirname,'build-checks/routing-rule-creation');fs.mkdirSync(out,{recursive:true});
          await page.screenshot({path:path.join(out,`s3-${width}.png`),fullPage:true});
        }
        const beforeLimit = await page.evaluate(()=>calls.length);
        await page.evaluate(()=>{for(let i=0;i<16;i++)addRule()});
        assert.equal(await page.locator('.routing-rule').count(),16,'Slots are reserved for drafts without overwriting existing rules');
        assert.equal(await page.evaluate(()=>serverRules[0].source_type),5);
        assert.equal(await page.evaluate(()=>calls.length),beforeLimit);
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),true);
        assert.equal(await page.locator('.routing-rule').first().locator('select').count(),2);
        assert.deepEqual(errors,[]);
        await page.close();
      }
    }
    console.log('PASS: actual rule creation/editing, no main input, safe drafts, status refresh, HTTP/network failures, retry, busy guards, cancellation and slot limits; all 3 MCUs, DE/EN, mobile/desktop');
  } finally {await browser.close();}
}

main().catch(error=>{console.error(error);process.exitCode=1;});
