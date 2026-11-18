const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const src=path.resolve(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const read=name=>fs.readFileSync(path.join(src,name),'utf8'),source=read('WebStatus.inc.h');
const css=source.match(/<style>([\s\S]*?)<\/style>/)[1]+read('WebFormStyles.h').match(/<style>([\s\S]*?)<\/style>/)[1];
const usb=read('WebUsbStorageUi.h').match(/R"USBJS\(([\s\S]*?)\)USBJS"/)[1];
const services=source.slice(source.indexOf('const jbcUsbServiceSelection='),source.indexOf('function jbcUsbRawTempC('));
function actions(className,language){
 const html=source.match(new RegExp('<div class="'+className+'">([\\s\\S]*?)<\\/div>'))[0];
 return new Function('uiLang','addr','a','return `'+html+'`;')(language,80,96);
}
const out=path.join(__dirname,'build-checks/profile-editor-actions');fs.mkdirSync(out,{recursive:true});
(async()=>{const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});try{
 for(const usbEnabled of [false,true])for(const lang of ['de','en'])for(const width of [320,390,768,1440]){
  const page=await browser.newPage({viewport:{width,height:1050}}),errors=[],requests=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://ofe.test/**',route=>{
   const req=route.request(),url=new URL(req.url());
   if(url.pathname==='/jbc-usb/config'){requests.push(Object.fromEntries(new URLSearchParams(req.postData())));return route.fulfill({status:202,body:lang==='de'?'Abfrage vorgemerkt; Antwort noch nicht bestaetigt':'Read queued; response not yet confirmed'});}
   if(url.pathname==='/usb/status')return route.fulfill({contentType:'application/json',body:JSON.stringify({detected:false,mounted:false,generation:1,drive:0,drive_instance:1,drives:[],directories:[],files:[],phase:'idle'})});
   return route.fulfill({contentType:'text/html',body:'<html></html>'});
  });
  await page.goto('http://ofe.test/');
  await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}body{margin:0}main{width:100%;max-width:1100px;margin:auto;padding:16px;box-sizing:border-box}.test-editor{padding:12px;border:1px solid #414854;margin-bottom:16px;min-width:0}.test-expert{max-width:350px}h2{font-size:16px}</style><body class="dev-mode"><header><select id="lang_sel"><option value="${lang}">${lang}</option></select></header><main><section class="test-editor upb-editor"><h2>Universal RS232</h2>${actions('upb-actions',lang)}</section><section class="test-editor"><h2>Modbus RTU</h2>${actions('mb-builder-actions',lang)}</section><section class="test-editor test-expert"><h2>${lang==='de'?'Profiltext':'Profile text'}</h2><div class="uni-dual-action"><button class="secondary" onclick="universalReload(80)">${lang==='de'?'Status neu laden':'Reload status'}</button><button class="secondary" onclick="universalLoadFromModule(80)">${lang==='de'?'Aus Modul laden':'Load from module'}</button></div><div class="uni-dual-action"><button class="secondary" onclick="universalExport(80)">Export</button><button onclick="universalSave(80)">${lang==='de'?'Ins Modul speichern':'Save to module'}</button></div></section><section class="test-editor" id="service"></section></main></body>`);
  await page.addScriptTag({content:`var uiLang=${JSON.stringify(lang)},devMode=true;window.load=()=>{};function jbcUsbStationKind(m){return m.kind||'SOLD'}function escHtml(v){return String(v).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;')}window.lastState={modules:[{addr:17,online:true,jbc_link_flags:1,jbc_usb_command_protocol:2,jbc_usb_model:'DDE'}]};${services}document.getElementById('service').innerHTML=jbcUsbServiceReadHtml(lastState.modules[0]);`});
  if(usbEnabled){await page.addScriptTag({content:usb});await page.waitForFunction(()=>document.querySelectorAll('.ofe-export-actions').length===3);}
  assert.equal(await page.locator('.ofe-usb-action').count(),usbEnabled?3:0);
  for(const bar of await page.locator('.upb-actions,.mb-builder-actions,.uni-dual-action').all()){
   const parent=await bar.boundingBox();assert(await bar.evaluate(e=>e.scrollWidth<=e.clientWidth+1),'action bar overflow');
   for(const button of await bar.locator('button').all()){
    const box=await button.boundingBox();assert.equal(box.height,44);assert(box.x>=parent.x-1&&box.x+box.width<=parent.x+parent.width+1,'button outside action bar');
    assert(await button.evaluate(e=>e.scrollHeight<=e.clientHeight+1),'button label clipped');
   }
   const boxes=await bar.locator('button').evaluateAll(elements=>elements.map(e=>{let r=e.getBoundingClientRect();return {x:r.x,y:r.y,right:r.right,bottom:r.bottom}}));
   for(let i=0;i<boxes.length;i++)for(let j=i+1;j<boxes.length;j++)assert(!(boxes[i].x<boxes[j].right-1&&boxes[i].right>boxes[j].x+1&&boxes[i].y<boxes[j].bottom-1&&boxes[i].bottom>boxes[j].y+1),'buttons overlap');
  }
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'page overflow');
  assert(await page.evaluate(()=>!jbcUsbServiceOptions({...lastState.modules[0],jbc_usb_command_protocol:1}).length));
  const values=await page.locator('#jbc_service_17 option').evaluateAll(es=>es.map(e=>Number(e.value)));assert(!values.includes(187)&&!values.includes(135));
  if(width===390&&usbEnabled){
   await page.selectOption('#jbc_service_17','160');await page.locator('#service button').click();
   assert.deepEqual(requests,[{addr:'17',action:'service_read',command:'160'}]);
   await page.waitForFunction(()=>document.getElementById('jbc_service_msg_17').textContent.length>0);
   assert.equal(await page.locator('#service button').isDisabled(),true);
   await page.evaluate(()=>document.getElementById('service').innerHTML=jbcUsbServiceReadHtml(lastState.modules[0]));
   await page.waitForFunction(()=>!document.querySelector('#service button').disabled);
   assert.equal(await page.locator('#jbc_service_17').inputValue(),'160');
   await page.evaluate(()=>devMode=false);await page.locator('#service button').click();assert.equal(requests.length,1);
  }
  await page.screenshot({path:path.join(out,`${usbEnabled?'p4':'s3'}-${lang}-${width}.png`),fullPage:true});
  assert.deepEqual(errors,[]);await page.close();
 }
 console.log('PASS: production builder action markup/CSS with real USB enhancement, 44px buttons, no overlap/clipping, plus manual service request/cooldown/rerender; S3/P4 DE/EN 320/390/768/1440');
}finally{await browser.close();}})().catch(error=>{console.error(error);process.exitCode=1;});
