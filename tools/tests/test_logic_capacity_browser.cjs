'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const read=file=>fs.readFileSync(path.join(src,file),'utf8'),logic=read('WebLogic.inc.h'),assets=read('WebLogicResponsive.h');
let editor=logic.slice(logic.indexOf('html += F(R"HTML(',logic.indexOf('static void web_handle_logic()'))+'html += F(R"HTML('.length);editor=editor.slice(0,editor.indexOf(')HTML");'));
const css=assets.match(/R"LOGICCSS\(([\s\S]*?)\)LOGICCSS"/)[1],js=assets.match(/R"LOGICUI\(([\s\S]*?)\)LOGICUI"/)[1];
const connectionJs=read('WebLogicConnections.h').match(/R"LOGICWIRE\(([\s\S]*?)\)LOGICWIRE"/)[1];
const shell=JSON.parse(read('WebShell.inc.h').match(/html \+= F\(("<\/title><style>[^\n]+?") OFE_WEB_TYPOGRAPHY_STYLE/)[1]).match(/<style>([\s\S]*?)<\/style>/)[1];
const automation=read('WebAutomationSignals.h').match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const out=path.join(__dirname,'build-checks/logic-capacity');fs.mkdirSync(out,{recursive:true});

(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.OFE_TEST_BROWSER_CHANNEL||(process.platform==='win32'?'msedge':undefined)});
 try{
  for(const width of [390,1440])for(const lang of ['de','en']){
   const page=await browser.newPage({viewport:{width,height:1000}}),errors=[],dialogs=[];let saved=null,saveCount=0;
   let stored={schema:1,name:'Capacity',enabled:false,nodes:[],links:[]};
   page.on('pageerror',e=>errors.push(e.message));page.on('dialog',async d=>{dialogs.push(d.message());await d.dismiss()});
   await page.route('http://ofe.test/**',async route=>{
    const p=new URL(route.request().url()).pathname;
    if(p==='/logic/responsive.css')return route.fulfill({contentType:'text/css',body:css});
    if(p==='/logic/responsive.js')return route.fulfill({contentType:'application/javascript',body:js});
    if(p==='/logic/connections.js')return route.fulfill({contentType:'application/javascript',body:connectionJs});
    if(p==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:`<meta name="viewport" content="width=device-width,initial-scale=1"><style>${shell}</style><select id="lang_sel"><option>de</option><option>en</option></select><main>${automation}${editor}</main>`});
    if(p==='/logic/json/save'){saved=JSON.parse(route.request().postData());stored=structuredClone(saved);saveCount++;return route.fulfill({body:'OK'})}
    const data=p==='/logic/list'?{active:0,max_nodes:64,max_links:128,items:[{slot:0,used:true,name:'Capacity'}]}:p==='/logic/json'?stored:{modules:[]};
    return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
   });
   await page.goto('http://ofe.test/');await page.waitForFunction(()=>logicLimits.nodes===64&&['Geladen','Loaded'].includes(document.getElementById('logic_msg').textContent));
   await page.evaluate(lang=>{document.getElementById('lang_sel').value=lang;applyLogicLang();graph.nodes=Array.from({length:63},(_,i)=>({...nodeDefaults('and'),id:'n'+i,x:80+(i%8)*220,y:80+Math.floor(i/8)*170}));graph.links=[];renderLogicCapacity()},lang);
   if(width<=1200)await page.click('[data-logic-tab=blocks]');
   await page.locator(".logic-palette button[onclick=\"addNode('and')\"]").click();
   assert.equal(await page.evaluate(()=>graph.nodes.length),64);assert((await page.textContent('#logic_capacity_nodes')).includes('64 / 64'));
   await page.evaluate(()=>addNode('or'));assert.equal(dialogs.length,1);assert(dialogs[0].includes('64'));assert.equal(await page.evaluate(()=>graph.nodes.length),64);
   await page.evaluate(()=>{sel='n0';duplicateSelected()});assert.equal(dialogs.length,2);assert.equal(await page.evaluate(()=>graph.nodes.length),64);
   await page.evaluate(()=>{deleteSelected();duplicateSelected();sel=graph.nodes[0].id;duplicateSelected()});assert.equal(await page.evaluate(()=>graph.nodes.length),64);
   await page.evaluate(()=>{
    graph.links=Array.from({length:127},(_,i)=>({from:graph.nodes[i%64].id,to:graph.nodes[(i+1+Math.floor(i/64))%64].id,port:Math.floor(i/64)}));
    window.finalLink={from:graph.nodes[63].id,to:graph.nodes[1].id,port:1};addLink(finalLink.from,finalLink.to,finalLink.port);
   });
   assert.equal(await page.evaluate(()=>graph.links.length),128);assert((await page.textContent('#logic_capacity_links')).includes('128 / 128'));
   const before=dialogs.length;await page.evaluate(()=>addLink(finalLink.from,finalLink.to,finalLink.port));assert.equal(dialogs.length,before);assert.equal(await page.evaluate(()=>graph.links.length),128);
   await page.evaluate(()=>addLink(graph.nodes[0].id,graph.nodes[3].id,2));assert.equal(dialogs.length,before+1);assert(dialogs.at(-1).includes('128'));assert.equal(await page.evaluate(()=>graph.links.length),128);
   const drawMs=await page.evaluate(()=>{const start=performance.now();render();return performance.now()-start});
   await page.evaluate(()=>saveGraph());assert(saved&&saved.nodes.length===64&&saved.links.length===128);assert.equal(saveCount,1);
   await page.evaluate(()=>{graph.nodes.push({...nodeDefaults('and'),id:'overflow'});renderLogicCapacity()});
   await page.evaluate(()=>saveGraph());assert.equal(saveCount,1);assert.equal(await page.evaluate(()=>graph.nodes.length),65);
   await page.evaluate(()=>{graph.nodes=Array.from({length:33},(_,i)=>({...nodeDefaults('output'),id:'out'+i,signal:'uni:80:switch:'+i}));graph.links=[];renderLogicCapacity()});
   await page.evaluate(()=>saveGraph());assert.equal(saveCount,1);assert(dialogs.at(-1).includes('32'));
   await page.evaluate(()=>{graph.nodes=graph.nodes.slice(0,1);graph.nodes[0].name='\u00e4'.repeat(13000);graph.links=[]});await page.evaluate(()=>saveGraph());assert.equal(saveCount,1);assert(dialogs.at(-1).includes('24000'));
   await page.evaluate(()=>{graph.nodes[0].name='AND';render();logicFitGraph()});
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);assert.deepEqual(errors,[]);
   await page.screenshot({path:path.join(out,`capacity-${width}-${lang}.png`),fullPage:true});
   console.log(`PASS: ${width}px ${lang}: 64/128 counters, add/duplicate/link guards, duplicate at limit, max save, imported overflow and UTF-8 size guard; full graph draw ${drawMs.toFixed(0)} ms`);
   await page.close();
  }
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
