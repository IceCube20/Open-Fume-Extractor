'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const source=fs.readFileSync(path.join(src,'WebStatus.inc.h'),'utf8');
const css=source.match(/<style>([\s\S]*?)<\/style>/)[1]+fs.readFileSync(path.join(src,'WebFormStyles.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const rows=source.split(/\r?\n/).find(line=>line.startsWith("let rows='';(d.modules||[]).forEach"));
const save=source.split(/\r?\n/).find(line=>line.startsWith('async function saveConnectionWarnings('));
const table=source.split(/\r?\n/).find(line=>line.startsWith('<section class="row table-wrap"><table>'));
const fixture=[true,false,true,false].map((supported,i)=>({addr:16+i,uid:'000000000000000'+i,fw:'2.1.43RC',online:true,telemetry_valid:true,
 label:'Module '+i,type_name:'Module '+i,connection_warnings_supported:supported,connection_warnings_enabled:i!==2}));
const out=path.join(__dirname,'build-checks/module-monitor-control');fs.mkdirSync(out,{recursive:true});

(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.OFE_TEST_BROWSER_CHANNEL||(process.platform==='win32'?'msedge':undefined)});
 try{
  for(const width of [320,390,1440,1920])for(const lang of ['de','en'])for(const dev of [false,true]){
   const page=await browser.newPage({viewport:{width,height:760}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.setContent(`<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><main>${table}</main>`);
   await page.addScriptTag({content:`let uiLang=${JSON.stringify(lang)},connectionWarningSaving=false,posts=[],alerts=[],failSave=false;
    const t=k=>({on:uiLang==='de'?'Ein':'On',off:uiLang==='de'?'Aus':'Off',online:'Online',connection_monitoring:uiLang==='de'?'Verbindungsueberwachung':'Connection monitoring',reboot_module:uiLang==='de'?'Neustart':'Restart'}[k]||k);
    const moduleRoleText=()=>'-',kb=()=>'-',ups=()=>'-',commText=()=> 'OK',commClass=()=> 'on',commTitle=()=> 'RS485',hx=n=>'0x'+n.toString(16),moduleListType=m=>m.type_name;
    function load(){}function saveLabel(){}function rebootModule(){}
    window.alert=s=>alerts.push(s);window.fetch=async(url,options)=>{posts.push({url,uid:options.body.get('uid'),enabled:options.body.get('enabled')});return {ok:!failSave,text:async()=> 'Test error'}};
    ${save}
    function renderRows(d){${rows};return rows}
    document.getElementById('mt').innerHTML=renderRows({modules:${JSON.stringify(fixture)}});
   `});
   await page.evaluate(dev=>document.body.classList.toggle('dev-mode',dev),dev);
   const label=`${width} ${lang} dev=${dev}`;
   const heights=await page.locator('#mt tr').evaluateAll(rows=>rows.map(r=>r.getBoundingClientRect().height));
   assert(heights.every(h=>Math.abs(h-heights[0])<.1),label+' checkbox rows must not be taller: '+heights);
   assert.equal(await page.locator('.connection-monitor-control').count(),2);
   assert.deepEqual(await page.locator('.connection-monitor-control').allTextContents(),['',''],label+' redundant on/off text');
   assert.deepEqual(await page.locator('.connection-monitor-toggle').evaluateAll(nodes=>nodes.map(n=>({checked:n.checked,height:n.getBoundingClientRect().height}))),[{checked:true,height:18},{checked:false,height:18}]);
   await page.locator('.connection-monitor-control').first().click();
   await page.waitForFunction(()=>posts.length===1&&!connectionWarningSaving);
   assert.deepEqual(await page.evaluate(()=>posts[0]),{url:'/module/connection_warnings',uid:fixture[0].uid,enabled:'0'});
   await page.locator('.connection-monitor-control').last().click();
   await page.waitForFunction(()=>posts.length===2&&!connectionWarningSaving);
   assert.equal(await page.evaluate(()=>posts[1].enabled),'1');
   await page.evaluate(()=>failSave=true);await page.locator('.connection-monitor-control').first().click();
   await page.waitForFunction(()=>alerts.length===1&&!connectionWarningSaving);
   assert.equal(await page.locator('.connection-monitor-toggle').first().isChecked(),false,label+' failed save rolls back');
   assert.deepEqual(errors,[],label+' JS errors');
   if(width===1920&&lang==='de'&&dev)await page.screenshot({path:path.join(out,'desktop.png'),fullPage:true});
   await page.close();
  }
  console.log('PASS: compact module-monitor checkboxes; equal row heights, no on/off labels, checked/unchecked/unsupported, save and rollback; 320-1920px, DE/EN, developer/community');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
