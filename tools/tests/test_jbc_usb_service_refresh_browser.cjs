const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const dir=path.resolve(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const source=fs.readFileSync(path.join(dir,'WebStatus.inc.h'),'utf8');
const css=source.match(/<style>([\s\S]*?)<\/style>/)[1]+fs.readFileSync(path.join(dir,'WebFormStyles.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const original=source.slice(source.indexOf('function jbcUsbSoldStationDiagHtml(m){'),source.indexOf('function jbcUsbHaDetailsHtml('));
const oneLine=prefix=>{const line=source.split(/\r?\n/).find(line=>line.startsWith(prefix));assert(line,prefix);return line;};
const functions=[oneLine('function jbcUsbWithoutDuplicateConnectionMode('),oneLine('function jbcUsbStationDetailBody('),oneLine('jbcUsbStationCardHtml=function('),oneLine('jbcUsbSoldStationDiagHtml=m=>')].join('\n');
const service=source.slice(source.indexOf('const jbcUsbServiceSelection='),source.indexOf('function jbcUsbRawTempC('));
const out=path.join(__dirname,'build-checks/jbc-usb-service-refresh');fs.mkdirSync(out,{recursive:true});
(async()=>{const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});try{
 for(const lang of ['de','en'])for(const width of [320,768,1440]){
  const page=await browser.newPage({viewport:{width,height:850}}),requests=[],errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  await page.route('http://ofe.test/**',route=>{
   if(new URL(route.request().url()).pathname==='/jbc-usb/config'){
    requests.push(Object.fromEntries(new URLSearchParams(route.request().postData())));
    return route.fulfill({status:202,body:'Read queued; response not yet confirmed'});
   }
   return route.fulfill({contentType:'text/html',body:'<html></html>'});
  });
  await page.goto('http://ofe.test/');
  await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}body{margin:0;padding:12px}main{max-width:640px;margin:auto}h1{font-size:18px}</style><body class="dev-mode"><main><h1>JBC USB</h1><div id="jbu_sold_stationdiag_17"></div></main></body>`);
  await page.addScriptTag({content:`var uiLang=${JSON.stringify(lang)},devMode=true;window.load=()=>{};function jbcUsbStationKind(m){return 'SOLD'}function jbcUsbKindLabel(){return 'SOLD'}function jbcUsbTemp(v){return String(v)+' UTI'}function jbcUsbSoldStationDiagIsOpen(){return false}function escHtml(v){return String(v).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;')}${original}\nconst jbcUsbSoldStationDiagRawHtml=jbcUsbSoldStationDiagHtml;\n${functions}\n${service}\n${oneLine('function moduleSig(')}\nwindow.lastState={modules:[{addr:17,type:9,fw:'1.1.85RC',online:true,jbc_link_flags:1,jbc_usb_model:'DDE',jbc_usb_model_type:'CAP26',jbc_usb_model_version:7,jbc_usb_command_protocol:2,jbc_usb_sold_station_diag_flags:1,jbc_usb_sold_trafo_temp:1000}]};jbcUsbRefreshSoldStation(lastState.modules[0],'SOLD');`});
  const select=page.locator('#jbc_service_17');
  await select.focus();
  await page.keyboard.press('Alt+ArrowDown');
  await page.evaluate(()=>{
   window.originalSelect=document.getElementById('jbc_service_17');window.originalButton=originalSelect.parentElement.querySelector('button');
   const signature=moduleSig(lastState);
   for(let i=0;i<24;i++){lastState.modules[0].jbc_usb_sold_trafo_temp=1000+i;jbcUsbRefreshSoldStation(lastState.modules[0],'SOLD');if(moduleSig(lastState)!==signature)throw Error('Live values changed outer module signature');}
  });
  assert(await page.evaluate(()=>originalSelect.isConnected&&document.getElementById('jbc_service_17')===originalSelect&&document.activeElement===originalSelect),'polling must preserve the open select and focus');
  await page.keyboard.press('Escape');
  assert((await page.locator('.jbu-station-live').textContent()).includes('1023 UTI'),'live values must still update');
  assert.equal(requests.length,0,'refresh cannot issue service requests');
  await select.selectOption('160');
  await page.evaluate(()=>{for(let i=0;i<24;i++)jbcUsbRefreshSoldStation(lastState.modules[0],'SOLD');});
  assert.equal(await select.inputValue(),'160');
  await page.locator('.jbu-service-actions button').click();
  await page.waitForFunction(()=>document.querySelector('[role="status"]').textContent.includes('queued'));
  await page.evaluate(()=>{for(let i=0;i<24;i++)jbcUsbRefreshSoldStation(lastState.modules[0],'SOLD');});
  assert(await page.evaluate(()=>document.getElementById('jbc_service_17')===originalSelect&&originalSelect.parentElement.querySelector('button')===originalButton));
  assert(await select.isDisabled());
  assert.deepEqual(requests,[{addr:'17',action:'service_read',command:'160'}]);
  await page.waitForFunction(()=>!document.getElementById('jbc_service_17').disabled);
  await page.evaluate(()=>{lastState.modules[0].jbc_link_flags=0;jbcUsbRefreshSoldStation(lastState.modules[0],'SOLD');});
  assert(await select.isDisabled());assert(await page.locator('.jbu-service-actions button').isDisabled());
  await page.evaluate(()=>{lastState.modules[0].jbc_link_flags=1;jbcUsbRefreshSoldStation(lastState.modules[0],'SOLD');});
  assert(!(await select.isDisabled()));assert.equal(await select.inputValue(),'160');
  assert((await page.locator('[role="status"]').textContent()).includes('not yet confirmed'));
  await page.screenshot({path:path.join(out,`${lang}-${width}.png`),fullPage:true});
  await page.evaluate(()=>{lastState.modules[0].jbc_usb_model='ALE';jbcUsbRefreshSoldStation(lastState.modules[0],'SOLD');});
  assert(await page.evaluate(()=>document.getElementById('jbc_service_17')!==originalSelect),'station identity changes must rebuild the options');
  assert.equal(await select.locator('option[value="160"]').count(),0);assert.equal(await select.locator('option[value="190"]').count(),1);
  await page.evaluate(()=>jbcUsbRefreshSoldStation(lastState.modules[0],'HA'));assert.equal(await select.count(),0);
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));assert.deepEqual(errors,[]);await page.close();
 }
 console.log('PASS: production SOLD renderer/select survive live refresh with native picker focus, selection, live values, messages and cooldown; disconnect and station changes; DE/EN 320/768/1440');
}finally{await browser.close();}})().catch(error=>{console.error(error);process.exitCode=1;});
