const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),out=path.join(__dirname,'build-checks/master-p4-native');
const css=fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1]+fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebDiagnostics.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
(async()=>{const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});try{
 for(const lang of ['de','en'])for(const width of [320,390,1051,1440]){
  const page=await browser.newPage({viewport:{width,height:1000}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('**/*',route=>{assert.equal(route.request().method(),'GET');return route.fulfill({body:'<html></html>',contentType:'text/html'});});
  await page.goto('http://ofe.test/diagnostics');
  const form=fs.readFileSync(path.join(out,`quality-${lang}.html`),'utf8');
  await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><select id='lang_sel'><option value='${lang}'>${lang}</option></select><main>${form}</main>`);
  const d={sample_ms:1000,ofe_baud:250000,modules:[{addr:32,type:3,name:'Fan/IO Pro <img src=x onerror=alert(1)>',transport:'rs485',requests:100,responses:100,timeouts:0},{addr:64,type:6,name:'Display WiFi',transport:'wifi',requests:1,responses:0,timeouts:1}]};
  await page.evaluate(d=>window.ofeBusQuality.feed(d),d);await page.selectOption('#rq_duration','30');await page.click('#rq_start');
  assert(await page.locator('#rq_stop').isEnabled());assert(await page.locator('#rq_export').isDisabled());
  for(let n=0;n<30;n++){d.sample_ms+=1000;d.modules[0].requests++;d.modules[0].responses++;d.modules[0].latency_last_ms=3;await page.evaluate(d=>window.ofeBusQuality.feed(d),d);}
  assert((await page.locator('#rq_state').textContent()).includes(lang==='de'?'Keine Fehler':'No errors'));
  assert.equal(await page.locator('#rq_rows tr').count(),1);assert.equal(await page.locator('#rq_rows img').count(),0);
  assert(await page.locator('#rq_export').isEnabled());
  const [download]=await Promise.all([page.waitForEvent('download'),page.click('#rq_export')]);
  const report=JSON.parse(fs.readFileSync(await download.path(),'utf8'));assert.equal(report.result,'ok');assert.equal(report.modules[0].requests,30);assert.equal(report.mode,'passive');
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`${lang}/${width} overflow`);
  await page.screenshot({path:path.join(out,`quality-${lang}-${width}.png`),fullPage:true});
  await page.click('#rq_start');await page.click('#rq_stop');assert((await page.locator('#rq_state').textContent()).includes(lang==='de'?'nicht vollständig':'inconclusive'));
  assert.deepEqual(errors,[]);await page.close();
 }
 console.log('PASS: actual C++ RS485 quality panel, DE/EN, 320/390/1051/1440, passive completion/cancel, JSON export, transport filtering and escaped names');
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
