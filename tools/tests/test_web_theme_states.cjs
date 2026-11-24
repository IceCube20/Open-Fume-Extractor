'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'),read=n=>fs.readFileSync(path.join(src,n),'utf8');
const raw=(file,tag)=>read(file).match(new RegExp('R"'+tag+'\\(([\\s\\S]*?)\\)'+tag+'"'))[1];
const theme=raw('WebTheme.h','THEME'),logic=read('WebLogic.inc.h'),diag=read('WebDiagnostics.inc.h');
const shell=JSON.parse(read('WebShell.inc.h').match(/html \+= F\(("<\/title><style>[^\n]+?") OFE_WEB_TYPOGRAPHY_STYLE/)[1]).match(/<style>([\s\S]*?)<\/style>/)[1];
const typography=JSON.parse(read('WebTypography.h').match(/#define OFE_WEB_TYPOGRAPHY_STYLE ("[^\n]+?") OFE_WEB_FORM_STYLE/)[1]);
const header='<header><nav class="nav"><select id="lang_sel"><option>de</option></select></nav></header>';
const styleValue=(page,selector,prop)=>page.locator(selector).evaluate((e,p)=>getComputedStyle(e)[p],prop);
const out=path.join(__dirname,'build-checks/theme-states');fs.mkdirSync(out,{recursive:true});
let editor=logic.slice(logic.indexOf('html += F(R"HTML(',logic.indexOf('static void web_handle_logic()'))+'html += F(R"HTML('.length);editor=editor.slice(0,editor.indexOf(')HTML");'));
const diagnosticGlyphs=diag.slice(diag.indexOf('function diagSerialGlyph()'),diag.indexOf('let diagModuleCardSignature'));
const palette=['rgb(23, 102, 62)','rgb(23, 102, 62)','rgb(36, 95, 146)','rgb(36, 95, 146)','rgb(25, 100, 120)','rgb(103, 67, 151)','rgb(119, 87, 19)','rgb(140, 70, 30)'];
const moduleTypes=[1,9,2,3,5,6,7,8];
const progress=[...read('WebUpdate.inc.h').matchAll(/<div class='progress'><div id='(?:masterBar|moduleBar)' class='bar'><\/div><\/div>/g)].map(m=>m[0]);
progress.push(read('WebHostedC6.inc.h').match(/<div class='progress'><div id='c6_bar' class='bar'><\/div><\/div>/)[0]);assert.equal(progress.length,3);
(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 try{
  for(const mode of ['light','dark'])for(const width of [390,1440]){
   const page=await browser.newPage({viewport:{width,height:900},colorScheme:mode});
   await page.setContent('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><style>'+shell+'</style>'+typography+theme+header+'<main>'+progress.map((p,i)=>'<section class="panel"><h2>'+['MCU','Module','ESP32-C6'][i]+'</h2>'+p+'</section>').join('')+'</main>');
   for(const percent of [0,25,75,100]){
    await page.locator('.progress>.bar').evaluateAll((es,p)=>es.forEach(e=>e.style.width=p+'%'),percent);await page.waitForTimeout(200);
    const bars=await page.locator('.progress>.bar').evaluateAll(es=>es.map(e=>({width:e.getBoundingClientRect().width,track:e.parentElement.clientWidth,bg:getComputedStyle(e).backgroundColor,parent:getComputedStyle(e.parentElement).backgroundColor})));
    bars.forEach(b=>{assert.notEqual(b.bg,b.parent,'Visible update fill');assert(Math.abs(b.width-b.track*percent/100)<1,'Actual update progress width')});
   }
   await page.screenshot({path:path.join(out,'updates-'+mode+'-'+width+'.png')});
   await page.setContent('<style>'+shell+'</style>'+typography+theme+'<style>'+raw('WebDiagnostics.inc.h','CSS').replace(/^<style>|<\/style>$/g,'')+'</style>'+header+'<main><div class="diag-module-grid" id="cards"></div></main>');
   await page.addScriptTag({content:diagnosticGlyphs});
   await page.evaluate(types=>{document.getElementById('cards').innerHTML=types.map(type=>{const v=diagModuleVisual({type});return '<article class="diag-module-card '+v.cls+'"><div class="diag-module-head"><div class="diag-module-title">'+v.sub+'</div><div class="diag-module-head-main"><div class="diag-module-identity"><span class="diag-module-icon">'+v.icon+'</span><div class="diag-module-copy"><div class="diag-module-type">'+v.sub+'</div></div></div></div></div></article>'}).join('')},moduleTypes);
   if(mode==='light')assert.deepEqual(await page.locator('.diag-module-icon').evaluateAll(es=>es.map(e=>getComputedStyle(e).color)),palette,'Actual diagnostic renderer keeps all eight icon identities');
   await page.screenshot({path:path.join(out,'diagnostics-'+mode+'-'+width+'.png')});
   await page.close();
  }
  for(const width of [390,1440]){
   const page=await browser.newPage({viewport:{width,height:1050},colorScheme:'light'}),errors=[];page.on('pageerror',e=>errors.push(e.message));await page.addInitScript(()=>{window.setInterval=()=>0});
   const fixture={schema:1,name:'States',enabled:true,nodes:[{id:'on',type:'input',name:'Ein',x:40,y:60,signal:'manual',value:true},{id:'off',type:'input',name:'Aus',x:40,y:300,signal:'manual',value:false},{id:'gate',type:'or',name:'ODER',x:350,y:60},{id:'out',type:'output',name:'Ausgang',x:650,y:60}],links:[{from:'on',to:'gate',port:0},{from:'gate',to:'out',port:0}]};
   await page.route('http://ofe.test/**',route=>{
    const p=new URL(route.request().url()).pathname;
    const scripts={'/logic/responsive.js':raw('WebLogicResponsive.h','LOGICUI'),'/logic/connections.js':raw('WebLogicConnections.h','LOGICWIRE')};
    if(scripts[p])return route.fulfill({contentType:'application/javascript',body:scripts[p]});
    if(p==='/logic/responsive.css')return route.fulfill({contentType:'text/css',body:raw('WebLogicResponsive.h','LOGICCSS')});
    if(p==='/logic/router.mjs')return route.fulfill({contentType:'application/javascript',body:fs.readFileSync(path.join(root,'assets/vendor/libavoid/index.js'),'utf8')});
    if(p==='/logic/libavoid.wasm')return route.fulfill({contentType:'application/wasm',body:fs.readFileSync(path.join(root,'assets/vendor/libavoid/libavoid.wasm'))});
    if(p==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><style>'+shell+'</style>'+typography+theme+header+'<main>'+raw('WebAutomationSignals.h','HTML')+editor+'</main>'});
    return route.fulfill({contentType:'application/json',body:JSON.stringify(p==='/logic/list'?{active:0,items:[{slot:0,used:true,name:'States'}]}:p==='/logic/json'?fixture:{modules:[]})});
   });
   await page.goto('http://ofe.test/');await page.waitForFunction(()=>graph.nodes.length===4&&document.getElementById('logic_msg').textContent==='Geladen');
   await page.evaluate(()=>{logicSetZoom(innerWidth<700?.5:.8);showLogicPane('plan');render()});
   assert(await page.locator('[data-id=on]').evaluate(e=>e.classList.contains('is-on')));
   const normal=await styleValue(page,'[data-id=off]','boxShadow'),sim=await styleValue(page,'[data-id=on]','boxShadow');
   assert.notEqual(sim,normal);assert(sim.includes('66, 128, 92'),'Simulation green halo');assert.equal(await styleValue(page,'[data-id=on]','backgroundColor'),'rgb(204, 224, 211)');
   await page.locator('[data-id=on]').evaluate(e=>e.classList.add('live-on'));assert.equal(await styleValue(page,'[data-id=on]','boxShadow'),sim,'Simulation keeps green halo even when a live input is also on');
   await page.screenshot({path:path.join(out,'logic-simulation-'+width+'.png')});
   await page.evaluate(()=>{logicViewMode='live';logicRuntimeState={slot:currentLogicSlot,ready:true,used:true,enabled:true,nodes:graph.nodes.map(n=>({id:n.id,value:n.id!=='off'}))};render()});
   const live=await styleValue(page,'[data-id=on]','boxShadow');assert.notEqual(live,sim);assert(live.includes('59, 113, 154'),'Live blue halo');assert.equal(await styleValue(page,'[data-id=on]','backgroundColor'),'rgb(203, 221, 236)');
   await page.evaluate(()=>setLogicSelection(['on']));assert.equal(await styleValue(page,'[data-id=on]','outlineStyle'),'solid');assert.equal(await styleValue(page,'[data-id=on]','boxShadow'),live,'Selection preserves active halo');
   await page.screenshot({path:path.join(out,'logic-live-'+width+'.png')});assert.deepEqual(errors,[]);await page.close();
  }
  console.log('PASS: actual MCU/module/C6 progress markup at 0/25/75/100%, diagnostic glyph mapping including JBC USB, simulation/live block colors and halos, selection, desktop/mobile');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
