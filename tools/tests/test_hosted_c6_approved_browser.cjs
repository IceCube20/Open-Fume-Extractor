'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),out=path.join(__dirname,'build-checks/master-p4-native');
const css=fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const catalog=JSON.parse(fs.readFileSync(path.join(root,'updates/hosted-c6-v2.json'),'utf8'));
(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 try{for(const lang of ['de','en'])for(const width of [320,1440]){
  const page=await browser.newPage({viewport:{width,height:1050}}),errors=[],posts=[];
  let token=0,state={phase:0,error:'OK',current:0x020c0d,host:0x020c0d,expected:0x020d01,size:1244160,received:0,written:0,lan:true,other_busy:false,verification:0,trial_allowed:false,trial:false};
  page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
  await page.route('**/*',async route=>{
   const req=route.request(),u=new URL(req.url()),reply=body=>route.fulfill({contentType:'application/json',body:JSON.stringify(body)});
   if(u.hostname==='raw.githubusercontent.com')return reply(catalog);
   if(u.hostname==='espressif.github.io'){assert(u.pathname.includes('2.13.1'));return route.fulfill({body:Buffer.alloc(state.size)})}
   if(u.pathname==='/update/c6/status')return reply({...state,owned:token&&Number(u.searchParams.get('token'))===token});
   if(u.pathname.startsWith('/update/c6/')){
    assert.equal(req.method(),'POST');assert.equal(req.headers()['x-csrf-token'],'0123456789abcdef');posts.push(u.pathname);
    assert(!u.searchParams.has('trial'),'approved path must not require trial mode');
    if(u.pathname.endsWith('/begin')){assert.equal(Number(u.searchParams.get('size')),1244160);state.phase=1;token=123;return reply({ok:true,token})}
    assert.equal(Number(u.searchParams.get('token')),token);
    if(u.pathname.endsWith('/chunk')){assert.equal(Number(u.searchParams.get('offset')),state.received);state.received=Math.min(state.received+16384,state.size);return reply({ok:true,received:state.received})}
    if(u.pathname.endsWith('/seal')){state.phase=2;return reply({ok:true})}
    if(u.pathname.endsWith('/install')){state.phase=3;return reply({ok:true})}
   }
   return route.fulfill({contentType:'text/html',body:'<html></html>'});
  });
  await page.goto('http://ofe.test/update');await page.setContent('<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial}'+css+'</style><main>'+fs.readFileSync(path.join(out,'c6-approved-'+lang+'.html'),'utf8')+'</main>');
  await page.waitForFunction(()=>!document.getElementById('c6_download').disabled);
  assert.equal(posts.length,0);assert.equal(await page.locator('#c6_target').count(),0);
  assert.equal(await page.evaluate(()=>window.OFE_C6.hostExpected),0x020c0d);
  assert.equal(await page.evaluate(()=>window.OFE_C6.expected),0x020d01);
  await page.waitForFunction(()=>document.getElementById('c6_catalog_state').textContent.includes(window.OFE_C6.de?'verfügbar':'available'));
  await page.locator('#c6_download').click();await page.waitForFunction(()=>!document.getElementById('c6_cancel').disabled);
  assert.equal(state.phase,2);assert(!posts.some(p=>p.endsWith('/install')));
  assert(await page.locator('#c6_install').isDisabled());await page.locator('#c6_recovery').check();
  assert(await page.locator('#c6_install').isEnabled());
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  if(width===320&&lang==='de')await page.screenshot({path:path.join(out,'c6-approved-mobile.png'),fullPage:true});
  await page.locator('#c6_install').click();assert.equal(state.phase,3);assert.deepEqual(errors,[]);await page.close();
 }console.log('PASS: approved PoE V3.2 C6 2.13.1 with host 2.12.13: DE/EN mobile/desktop, no developer mode, pinned staging, catalog and separate installation');}
 finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
