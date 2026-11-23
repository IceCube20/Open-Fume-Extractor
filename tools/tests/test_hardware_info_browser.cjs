const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),out=path.join(__dirname,'build-checks/master-p4-native');
const css=fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
(async()=>{const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});try{
 for(const chip of ['s3','p4','p4-poe'])for(const lang of ['de','en'])for(const width of [320,390,1440]){
  const p4=chip!=='s3',poe=chip==='p4-poe';
  const page=await browser.newPage({viewport:{width,height:1000}}),form=fs.readFileSync(path.join(out,`hardware-${chip}-${lang}.html`),'utf8');
  await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><main>${form}</main>`);
  const entries=await page.locator('#master_hardware dd').allTextContents();
  const labels=await page.locator('#master_hardware dt').allTextContents(),values=Object.fromEntries(labels.map((label,index)=>[label,entries[index]]));
  assert.equal(values[lang==='de'?'Prozessor':'Processor'],p4?'ESP32-P4':'ESP32-S3');
  assert.equal(values.Board,poe?'Waveshare ESP32-P4-WIFI6-POE-ETH':p4?'Waveshare Module DEV KIT':'ESP32-S3 (MCU-S3)');
  assert.equal(values[lang==='de'?'Arduino-Boardprofil':'Arduino board profile'],poe?'OFE_MCU_P4_POE':p4?'OFE_MCU_P4':'ESP32S3_DEV');
  assert.equal(values[lang==='de'?'Siliziumrevision':'Silicon revision'],p4?'1.0':'0.2');
  assert.equal(values[lang==='de'?'CPU-Kerne':'CPU cores'],'2');
  assert(entries.includes(p4?'360 MHz':'240 MHz'));assert(entries.includes('16.0 MiB'));
  assert.equal(entries.includes('ESP32-C6 (SDIO)'),p4);
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`${chip}/${lang}/${width} overflow`);
  await page.screenshot({path:path.join(out,`hardware-${chip}-${lang}-${width}.png`),fullPage:true});await page.close();
 }
 console.log('PASS: actual hardware C++ renderer: S3/P4 revisions, core count, memory/clocks, separate C6 firmware, DE/EN and mobile/desktop');
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
