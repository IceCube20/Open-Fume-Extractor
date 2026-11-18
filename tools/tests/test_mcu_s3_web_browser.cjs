const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const src=path.resolve(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const fixture=path.join(__dirname,'build-checks/master-p4-native'),out=path.join(__dirname,'build-checks/mcu-s3-shared-ui');fs.mkdirSync(out,{recursive:true});
const status=fs.readFileSync(path.join(src,'WebStatus.inc.h'),'utf8'),icons=fs.readFileSync(path.join(src,'WebTransportIcons.h'),'utf8');
const header=status.slice(status.indexOf('<header>'),status.indexOf('<main>')).replace(')HTML" OFE_TRANSPORT_HEADER_HTML R"HTML(',icons.match(/R"ICON\(([\s\S]*?)\)ICON"/)[1]);
const css=fs.readFileSync(path.join(src,'WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1]+fs.readFileSync(path.join(src,'WebDiagnostics.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1]+icons.match(/<style>([\s\S]*?)<\/style>/)[1];
const forms=fs.readFileSync(path.join(fixture,'shared-form-style.html'),'utf8');
const logoArray=fs.readFileSync(path.join(src,'WebLogo.h'),'utf8').match(/OFE_LOGO_PNG[^=]*=\s*\{([\s\S]*?)\}/)[1];
const logo=Buffer.from([...logoArray.matchAll(/0x([0-9a-f]{2})/gi)].map(m=>parseInt(m[1],16)));
(async()=>{const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});try{
 for(const lang of ['de','en'])for(const width of [320,390,768,1440]){
  const page=await browser.newPage({viewport:{width,height:1000}}),errors=[],requests=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://ofe.test/**',route=>{const pathname=new URL(route.request().url()).pathname;requests.push(pathname);return route.fulfill(pathname==='/logo.png'?{contentType:'image/png',body:logo}:{contentType:'text/html',body:'<html></html>'});});
  await page.goto('http://ofe.test/update');
  const exports=fs.readFileSync(path.join(fixture,`diagnostic-exports-${lang}.html`),'utf8');
  await page.setContent(`<style>${css}</style>${forms}${header}<main><div class="grid"><section class="panel"><h2>MCU-S3</h2><label for="firmware">Firmware (.bin)</label><input id="firmware" type="file" accept=".bin"><button type="button">${lang==='de'?'MCU aktualisieren':'Update MCU'}</button></section><section class="panel"><h2>Backup</h2><label for="backup">JSON</label><input id="backup" type="file" accept=".json"><button type="button">${lang==='de'?'Importieren':'Import'}</button></section></div><section class="panel"><h2>${lang==='de'?'Bus Diagnose':'Bus Diagnostics'}</h2>${exports}</section></main>`);
  await page.locator('#lang_sel').evaluate((e,lang)=>e.value=lang,lang);
  const file=page.locator('#firmware'),color=()=>file.evaluate(e=>getComputedStyle(e,'::file-selector-button').backgroundColor);
  await file.setInputFiles({name:'OpenFumeExtractorMCU_S3_2.1.22RC-Developer.bin',mimeType:'application/octet-stream',buffer:Buffer.from('test')});
  for(const input of await page.locator('input[type=file]').all()){
   const box=await input.boundingBox(),available=await input.evaluate(e=>e.parentElement.clientWidth-parseFloat(getComputedStyle(e.parentElement).paddingLeft)-parseFloat(getComputedStyle(e.parentElement).paddingRight));
   assert.equal(box.height,44);assert(Math.abs(box.width-available)<1,'S3 filename field must use all available width');
  }
  await page.mouse.move(0,0);const base=await color();await file.hover({position:{x:15,y:20}});await page.waitForTimeout(200);assert.notEqual(await color(),base);
  await file.evaluate(e=>e.disabled=true);await page.waitForTimeout(200);assert.equal(await color(),base);await file.evaluate(e=>e.disabled=false);await page.mouse.move(0,0);
  assert.equal(await page.locator('header [data-i18n="refresh"]').count(),0);
  assert.equal(await page.locator('.diag-export-group').count(),3);
  assert.equal(await page.locator('.ofe-usb-action,#usb-storage,#usb-picker,script[src="/usb/ui.js"]').count(),0);
  assert.equal(requests.filter(p=>p.startsWith('/usb')).length,0);
  assert(await page.locator('.brand-logo').evaluate(e=>e.complete&&e.naturalWidth>0));
  for(const b of await page.locator('.diag-export-group button,.diag-export-group .btn').all())assert.equal((await b.boundingBox()).height,40);
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  await page.screenshot({path:path.join(out,`s3-${lang}-${width}.png`),fullPage:true});
  assert.deepEqual(errors,[]);await page.close();
 }
 console.log('PASS: actual shared C++ form styles and diagnostic exports on S3, no USB scripts/APIs, full-width 44px fields, native browse hover/disabled states, 40px export controls and refresh-free header; DE/EN 320/390/768/1440');
}finally{await browser.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
