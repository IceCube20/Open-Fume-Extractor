'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),vm=require('node:vm');
const {chromium}=require('playwright'),root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const read=n=>fs.readFileSync(path.join(src,n),'utf8'),status=read('WebStatus.inc.h'),theme=read('WebTheme.h').match(/R"THEME\(([\s\S]*?)\)THEME"/)[1];
const css=status.match(/<style>([\s\S]*?)<\/style>/)[1]+read('WebHeaderStyles.h').match(/<style>([\s\S]*?)<\/style>/)[1];
const code=status.slice(status.indexOf('function fanIoMainConfigured('),status.indexOf('function renderDetails(d)'));
new vm.Script(code);
const header=status.match(/<body>(<header>[\s\S]*?<\/header>)/)[1].replace(/\)HTML" OFE_TRANSPORT_HEADER_HTML R"HTML\(/,'');
const out=path.join(__dirname,'build-checks/led-theme');fs.mkdirSync(out,{recursive:true});
(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 try{
  for(const width of [320,390,1440]){
   const page=await browser.newPage({viewport:{width,height:900},colorScheme:'light'}),errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://ofe.test/**',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,sans-serif}body,input,select,button{font-family:var(--ofe-font-ui)}'+css+'</style>'+theme+header+'<main></main>'}));
   await page.goto('http://ofe.test/');
   assert.equal(await page.getAttribute('html','data-ofe-theme'),'light');
   await page.selectOption('#ofe_theme','dark');assert.equal(await page.getAttribute('html','data-ofe-theme'),'dark');
   await page.reload();assert.equal(await page.inputValue('#ofe_theme'),'dark');
   await page.emulateMedia({colorScheme:'light'});assert.equal(await page.getAttribute('html','data-ofe-theme'),'dark');
   await page.selectOption('#ofe_theme','auto');await page.emulateMedia({colorScheme:'dark'});await page.waitForFunction(()=>document.documentElement.dataset.ofeTheme==='dark');assert.equal(await page.getAttribute('html','data-ofe-theme'),'dark');
   await page.emulateMedia({colorScheme:'light'});await page.waitForFunction(()=>document.documentElement.dataset.ofeTheme==='light');assert.equal(await page.getAttribute('html','data-ofe-theme'),'light');
   await page.addScriptTag({content:`var uiLang='de',detailsSig='',routingSig='';const t=k=>k,mn=m=>m.name,hx=a=>'0x'+a.toString(16),escHtml=v=>String(v).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;'),escAttr=escHtml;
    const fanIoDescriptorHeader=(m,k,d)=>new RegExp('^'+k+'=(.*)$','m').exec(m.universal_descriptor)?.[1]??d;
    const universalDefsForModule=()=>[];async function load(){}
    window.lastState={modules:[{addr:32,uid:'test',name:'Fan/IO',type:2,universal_descriptor:'led_schema=1\\nled_pin=255\\nled_count=8\\nled_ch0=1|0|4|0|255|0|0|0|1000|Werkbank'}]};
    ${code}
    fanIoEditorOpen(32);`});
   assert.equal(await page.evaluate(()=>fanIoEditorDraft.leds.channels[0].name),'Werkbank');
   assert.equal(await page.locator('.fanio-led-zone').count(),1);
   await page.evaluate(()=>{fanIoEditorDraft.fan.enabled=false;fanIoEditorDraft.leds.pin=4});
   assert.match(await page.evaluate(()=>{try{fanIoEditorValidate();return ''}catch(e){return e.message}}),/GPIO 4/);
   await page.evaluate(()=>{fanIoEditorDraft.leds.pin=23;fanIoEditorDraft.outputs[0]={enabled:true,name:'Relay',pin:23}});
   assert.match(await page.evaluate(()=>{try{fanIoEditorValidate();return ''}catch(e){return e.message}}),/doppelt/);
   await page.evaluate(()=>{fanIoEditorDraft.outputs[0].enabled=false;fanIoEditorValidate()});
   const requests=[];await page.route('http://ofe.test/module/**',route=>{requests.push(new URLSearchParams(route.request().postData()));return route.fulfill({body:'OK'})});
   await page.evaluate(()=>fanIoEditorSave());
   assert.equal(requests.filter(q=>q.get('kind')==='led_channel').length,32);
   assert.equal(requests.find(q=>q.get('kind')==='led_strip').get('pin'),'23');
   assert(requests.find(q=>q.get('kind')==='commit'));
   await page.evaluate(()=>{fanIoEditorOpen(32);document.getElementById('fanio_editor_scroll').scrollTop=100000});
   const close=await page.locator('.fanio-editor-close').boundingBox();assert(close.x>=0&&close.x+close.width<=width+1&&close.y>=0,'Reachable close');
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false,'No page overflow');
   assert.equal(await page.locator('#fanio_editor_scroll').evaluate(e=>e.scrollWidth>e.clientWidth+1),false,'No editor overflow');
   await page.screenshot({path:path.join(out,'editor-'+width+'.png')});assert.deepEqual(errors,[]);await page.close();
  }
  console.log('PASS: persistent auto/light/dark theme, system changes, actual LED editor, pin reservations/collisions, 32-channel transaction and mobile/desktop layout');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
