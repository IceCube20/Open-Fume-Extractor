const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.join(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'),out=path.join(__dirname,'build-checks/master-p4-native');
const css=fs.readFileSync(path.join(root,'WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const size=1226976,expected=0x020c0d;
const catalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../../updates/hosted-c6.json'),'utf8'));
(async()=>{const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
try{for(const lang of ['de','en'])for(const width of [320,390,768,1440]){
 const page=await browser.newPage({viewport:{width,height:1050}}),errors=[],posts=[];
 const state={phase:0,error:'OK',current:expected-1,host:expected,expected,received:0,written:0,size,lan:true,other_busy:false,verification:0};
 let serverToken=0,failSeal=false,unauthorized=false;
 let catalogValue=catalog,catalogStatus=200,catalogRequests=0;
 page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
 await page.route('**/*',async route=>{
  const req=route.request(),url=new URL(req.url());
  const reply=(body,status=200)=>route.fulfill({status,contentType:'application/json',body:JSON.stringify(body)});
  if(url.hostname==='raw.githubusercontent.com'){
   assert(!req.headers()['x-csrf-token']);assert(!req.headers()['authorization']);catalogRequests++;
   return reply(catalogValue,catalogStatus);
  }
  if(url.hostname==='espressif.github.io'){
   assert(!req.headers()['x-csrf-token']);return route.fulfill({headers:{'Access-Control-Allow-Origin':'*'},body:Buffer.alloc(size)});
  }
  if(url.pathname==='/update/c6/status')return unauthorized?reply({},401):reply({...state,owned:!!serverToken&&Number(url.searchParams.get('token'))===serverToken});
  if(url.pathname.startsWith('/update/c6/')){
   assert.equal(req.method(),'POST');assert.equal(req.headers()['x-csrf-token'],'0123456789abcdef');posts.push(url.pathname);
   if(url.pathname.endsWith('/begin')){assert.equal(Number(url.searchParams.get('size')),size);state.phase=1;state.received=0;serverToken=123;return reply({ok:true,token:serverToken});}
   assert.equal(Number(url.searchParams.get('token')),serverToken);
   if(url.pathname.endsWith('/chunk')){const offset=Number(url.searchParams.get('offset'));assert.equal(offset,state.received);state.received=Math.min(offset+16384,size);return reply({ok:true,received:state.received});}
   if(url.pathname.endsWith('/seal')){if(failSeal){state.phase=6;state.error='SHA256';return reply({ok:false,error:'SHA256'},409);}state.phase=2;return reply({ok:true});}
   if(url.pathname.endsWith('/cancel')){state.phase=0;state.error='OK';serverToken=0;return reply({ok:true});}
   if(url.pathname.endsWith('/install')){assert.equal(url.searchParams.get('recovery'),'1');state.phase=3;return reply({ok:true});}
   throw Error(url.pathname);
  }
  return route.fulfill({body:'<html></html>',contentType:'text/html'});
 });
 await page.goto('http://ofe.test/update');
 const form=fs.readFileSync(path.join(out,`c6-${lang}.html`),'utf8');
 const render=async()=>{await page.goto('http://ofe.test/update');await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><main>${form}</main>`);};
 await render();await page.waitForFunction(()=>!document.getElementById('c6_download').disabled);
 assert.equal(await page.locator('#c6_current').textContent(),'2.12.12');assert(await page.locator('#c6_install').isDisabled());
 await page.waitForFunction(()=>document.getElementById('c6_catalog_state').textContent.includes(window.OFE_C6.de?'Update verfügbar':'update available'));
 assert.equal(posts.length,0);assert.equal(catalogRequests,1);
 if(width===390){
  catalogStatus=404;await page.locator('#c6_catalog_check').click();
  await page.waitForFunction(()=>document.getElementById('c6_catalog_state').textContent.includes(window.OFE_C6.de?'nicht verfügbar':'unavailable'));
  assert(await page.locator('#c6_download').isEnabled());assert.equal(posts.length,0);
  catalogStatus=200;catalogValue={schema:1,releases:[]};await page.locator('#c6_catalog_check').click();
  await page.waitForFunction(()=>document.getElementById('c6_catalog_state').textContent.includes(window.OFE_C6.de?'Keine passende':'No matching'));
  catalogValue=catalog;await page.locator('#c6_catalog_check').click();
  await page.waitForFunction(()=>document.getElementById('c6_catalog_state').textContent.includes(window.OFE_C6.de?'Update verfügbar':'update available'));
  const future=JSON.parse(JSON.stringify(catalog.releases[0]));future.id='future-test-only';future.host.esp_hosted='2.13.0';future.host.arduino_esp32='3.4.0';future.host.esp_idf='5.6.0';future.coprocessor.version='2.13.0';future.coprocessor.url='https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.13.0.bin';
  catalogValue={schema:1,releases:[future]};await page.locator('#c6_catalog_check').click();
  await page.waitForFunction(()=>document.getElementById('c6_catalog_state').textContent.includes(window.OFE_C6.de?'aktualisiertes MCU-SDK':'updated MCU SDK'));
  assert.equal(await page.evaluate(()=>window.OFE_C6.expected),expected);assert.equal(posts.length,0);
  catalogValue=catalog;await page.locator('#c6_catalog_check').click();
  state.current=0;
  await page.waitForFunction(()=>document.getElementById('c6_catalog_state').textContent.includes(window.OFE_C6.de?'noch nicht bestätigt':'not yet confirmed'));
  state.phase=6;state.error='C6_VERSION_UNAVAILABLE';
  await page.waitForFunction(()=>document.getElementById('c6_state').textContent.includes(window.OFE_C6.de?'keine Firmwaredaten geschrieben':'no firmware data written'));
  assert(!(await page.locator('#c6_state').textContent()).includes(lang==='de'?'nicht erreichbar':'unavailable'));
  assert.equal(posts.length,0);
  state.phase=0;state.error='OK';
  state.current=expected-1;
  await page.waitForFunction(()=>document.getElementById('c6_catalog_state').textContent.includes(window.OFE_C6.de?'Update verfügbar':'update available'));
 }
 await page.locator('#c6_download').click();
 await page.waitForFunction(()=>!document.getElementById('c6_cancel').disabled,{timeout:30000});
 assert.equal(state.phase,2);assert.equal(state.received,size);assert(!posts.some(p=>p.endsWith('/install')));
 assert(await page.locator('#c6_install').isDisabled());
 await render();await page.waitForFunction(()=>!document.getElementById('c6_cancel').disabled); // Resume owned staging after reload.
 await page.locator('#c6_recovery').check();assert(await page.locator('#c6_install').isEnabled());
 const bounds=await page.locator('input,button').evaluateAll(nodes=>nodes.every(e=>{const r=e.getBoundingClientRect();return r.left>=0&&r.right<=innerWidth}));
 assert(bounds,`overflow ${lang}/${width}`);assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 await page.screenshot({path:path.join(out,`c6-${lang}-${width}.png`),fullPage:true});
 unauthorized=true;await page.waitForFunction(()=>document.getElementById('c6_state').textContent.includes('401'));assert(await page.locator('#c6_install').isDisabled());unauthorized=false;
 await page.waitForFunction(()=>!document.getElementById('c6_install').disabled);
 await page.locator('#c6_install').click();await page.waitForFunction(()=>document.getElementById('c6_state').textContent.includes(window.OFE_C6.de?'vorbereitet':'Preparing'));
 assert.equal(state.phase,3);state.phase=5;state.written=size;
 await page.waitForFunction(()=>document.getElementById('c6_state').textContent.includes(window.OFE_C6.de?'startet neu':'restarting'));
 state.phase=0;serverToken=0;state.current=expected;state.verification=1;
 await page.waitForFunction(()=>document.getElementById('c6_state').textContent.includes(window.OFE_C6.de?'bestätigt':'confirmed'));
 await page.waitForFunction(()=>document.getElementById('c6_catalog_state').textContent.includes(window.OFE_C6.de?'SDK aktuell':'SDK:'));
 // A bad image can only fail staging, never trigger installation.
 state.current=expected-1;state.verification=0;failSeal=true;
 await page.waitForFunction(()=>!document.getElementById('c6_download').disabled);
 const installed=posts.filter(p=>p.endsWith('/install')).length;
 await page.locator('#c6_download').click();
 await page.waitForFunction(()=>document.getElementById('c6_state').textContent.includes(window.OFE_C6.de?'Prüfsumme':'checksum'));
 assert.equal(posts.filter(p=>p.endsWith('/install')).length,installed);
 state.lan=false;await page.waitForFunction(()=>document.getElementById('c6_download').disabled);
 assert.deepEqual(errors,[]);await page.close();
}console.log('PASS: actual C6 C++ form and JS: DE/EN responsive layouts, advisory online catalog/offline fallback, staged-only download, CSRF, reload ownership, recovery confirmation, install, post-reboot verification, SHA error and LAN/auth guards');
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
