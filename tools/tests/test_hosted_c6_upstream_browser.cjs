'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),out=path.join(__dirname,'build-checks/master-p4-native');
const css=fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
let commit='a'.repeat(40);const blob='b'.repeat(40),base=0x020c0d;
(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 try{for(const form of (process.env.OFE_APPROVED_TRIAL_UI?['c6-approved-trial']:['c6-trial','c6-approved-trial']))for(const width of [320,1440]){
  const page=await browser.newPage({viewport:{width,height:1000}}),errors=[],posts=[];
  let token=0,apiFails=false,state={phase:0,error:'OK',current:0x020d01,host:base,expected:base,size:512,received:0,written:0,lan:true,other_busy:false,trial_allowed:true,trial:false,developer_image:false,verification:0};
  page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
  await page.route('**/*',async route=>{
   const req=route.request(),url=new URL(req.url()),reply=(data,status=200)=>route.fulfill({status,contentType:'application/json',body:JSON.stringify(data)});
   if(url.hostname==='api.github.com'){
    if(apiFails)return reply({},403);
    if(url.pathname.includes('/git/ref/'))return reply({object:{sha:commit,type:'commit'}});
    return reply(['2.14.0','2.13.1','2.12.13','2.12.11'].map(v=>({name:`esp32c6-v${v}.bin`,path:`hosted/esp32c6-v${v}.bin`,type:'file',size:512,sha:blob})));
   }
   if(url.hostname==='raw.githubusercontent.com')return url.pathname.endsWith('.bin')?route.fulfill({body:Buffer.alloc(512)}):reply({schema:1,releases:[]});
   if(url.pathname==='/update/c6/status')return reply({...state,owned:!!token&&Number(url.searchParams.get('token'))===token});
   if(url.pathname.startsWith('/update/c6/')){
    assert.equal(req.method(),'POST');assert.equal(req.headers()['x-csrf-token'],'0123456789abcdef');posts.push({path:url.pathname,args:Object.fromEntries(url.searchParams)});
    if(url.pathname.endsWith('/begin')){assert.equal(url.searchParams.get('target'),'upstream');assert.equal(url.searchParams.get('blob'),blob);assert.equal(url.searchParams.get('commit'),commit);assert.equal(url.searchParams.get('risk'),'1');state={...state,expected:Number(url.searchParams.get('version')),phase:1,trial:true,developer_image:true,size:512,received:0,commit};token=123;return reply({ok:true,token})}
    assert.equal(Number(url.searchParams.get('token')),token);
    if(url.pathname.endsWith('/chunk')){state.received=512;return reply({ok:true,received:512})}
    if(url.pathname.endsWith('/seal')){state.phase=2;state.sha256='c'.repeat(64);return reply({ok:true})}
    if(url.pathname.endsWith('/install')){assert.equal(url.searchParams.get('risk'),'1');assert.equal(url.searchParams.get('trial'),'1');state.phase=3;return reply({ok:true})}
    if(url.pathname.endsWith('/cancel')){token=0;state.phase=0;state.trial=false;state.developer_image=false;return reply({ok:true})}
   }
   return route.fulfill({contentType:'text/html',body:'<html></html>'});
  });
  const render=async()=>{await page.goto('http://ofe.test/update');await page.setContent('<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,sans-serif}'+css+'</style><main>'+fs.readFileSync(path.join(out,form+'-de.html'),'utf8')+'</main>')};
  await render();
  assert.deepEqual(await page.locator('#c6_target option').allTextContents(),[form==='c6-approved-trial'?'2.13.1':'2.12.13']);
  const gap=await page.evaluate(()=>document.getElementById('c6_upstream_check').getBoundingClientRect().top-document.getElementById('c6_target').getBoundingClientRect().bottom);
  assert(gap>=16,'Espressif button must have at least 16px spacing');
  await page.locator('#c6_upstream_check').click();
  await page.waitForFunction(()=>document.querySelectorAll('#c6_target option').length===4);
  const labels=await page.locator('#c6_target option').allTextContents();
  assert(labels.every(text=>/^\d+\.\d+\.\d+$/.test(text)),'only Espressif version numbers');assert.equal(new Set(labels).size,labels.length);
  for(const target of [0x020e00,0x020c0b]){
   const key='upstream:'+target+':'+commit;await page.locator('#c6_target').selectOption(key);
   await page.waitForFunction(()=>!document.getElementById('c6_download').disabled);
   assert(await page.locator('#c6_catalog_check').isDisabled());await page.locator('#c6_download').click();await page.waitForFunction(()=>!document.getElementById('c6_cancel').disabled);
   assert.equal(state.phase,2);assert(await page.locator('#c6_install').isDisabled());assert(!posts.some(p=>p.path.endsWith('/install')));
   await render();await page.waitForFunction(()=>!document.getElementById('c6_cancel').disabled);assert.equal(await page.locator('#c6_target').inputValue(),key);
   await page.locator('#c6_recovery').check();assert(await page.locator('#c6_install').isEnabled());
   state.trial_allowed=false;await page.waitForFunction(()=>document.getElementById('c6_install').disabled);state.trial_allowed=true;
   await page.waitForFunction(()=>!document.getElementById('c6_install').disabled);
   state.lan=false;await page.waitForFunction(()=>document.getElementById('c6_install').disabled);state.lan=true;
   const installed=state.current;state.current=0;await page.waitForFunction(()=>document.getElementById('c6_install').disabled);state.current=installed;
   await page.waitForFunction(()=>!document.getElementById('c6_install').disabled);
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'mobile overflow');
   if(width===320)await page.screenshot({path:path.join(out,form+'-upstream-mobile.png'),fullPage:true});
   if(target===0x020c0b){await page.locator('#c6_install').click();assert.equal(state.phase,3)}else await page.locator('#c6_cancel').click();
   token=0;state.phase=0;state.trial=false;state.developer_image=false;await render();
   await page.locator('#c6_upstream_check').click();await page.waitForFunction(()=>document.querySelectorAll('#c6_target option').length===4);
  }
  const selected='upstream:'+0x020c0b+':'+commit;await page.locator('#c6_target').selectOption(selected);
  commit=(commit[0]==='a'?'d':'a').repeat(40);await page.locator('#c6_upstream_check').click();
  await page.waitForFunction(value=>document.getElementById('c6_target').value===value,'upstream:'+0x020c0b+':'+commit);
  assert.equal(await page.locator('#c6_target option').count(),4,'refresh must not duplicate versions');
  apiFails=true;await page.locator('#c6_upstream_check').click();await page.waitForFunction(()=>document.getElementById('c6_upstream_state').textContent.includes('403'));
  assert.deepEqual(errors,[]);await page.close();
 }console.log('PASS: Pre-V3/V3 developer UI, mobile/desktop, dynamic upstream upgrade/downgrade, staged ownership/reload, fresh consent, revoked authorization, API failure, no automatic flashing');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
