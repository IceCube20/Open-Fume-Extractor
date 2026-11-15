const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const {chromium}=require('playwright');
const src=path.join(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const source=fs.readFileSync(path.join(src,'WebConfig.inc.h'),'utf8');
const shell=fs.readFileSync(path.join(src,'WebShell.inc.h'),'utf8');
const css=shell.match(/<style>([\s\S]*?)<\/style>/)[1];
const start=source.indexOf('function toggleStaticIp(){');
let depth=0,end=start;
for(let i=source.indexOf('{',start);i<source.length;i++){
 if(source[i]==='{')depth++;
 if(source[i]==='}' && --depth===0){end=i+1;break;}
}
const toggle=source.slice(start,end);
const out=path.join(__dirname,'build-checks/master-p4-native');
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
 try{
  for(const lang of ['de','en'])for(const width of [320,390,768,1440]){
   const page=await browser.newPage({viewport:{width,height:1000}});
   const errors=[];page.on('pageerror',error=>errors.push(error.message));
   const form=fs.readFileSync(path.join(out,`ipv4-${lang}.html`),'utf8');
   await page.setContent(`<html lang="${lang}"><style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><main><form class="panel">${form}</form></main><script>${toggle};toggleStaticIp();</script></html>`);
   assert.equal(await page.locator('#ip_mode').inputValue(),'static');
   assert.equal(await page.locator('#eth_ip_mode').inputValue(),'dhcp');
   assert(await page.locator('#static_ip_fields').isVisible());
   assert(!(await page.locator('#eth_static_ip_fields').isVisible()));
   await page.locator('#eth_ip_mode').selectOption('static');
   assert(await page.locator('#eth_static_ip_fields').isVisible());
   await page.locator('#eth_ip').fill('192.0.2.20');
   await page.locator('#eth_gateway').fill('192.0.2.1');
   let values=await page.evaluate(()=>Object.fromEntries(new FormData(document.querySelector('form'))));
   assert.equal(values.ip,'192.0.2.10');assert.equal(values.eth_ip,'192.0.2.20');
   assert.equal(values.gateway,'192.0.2.1');assert.equal(values.eth_gateway,'192.0.2.1');
   for(const prefix of ['','eth_'])for(const name of ['ip','gateway','subnet','dns1','dns2'])assert(Object.hasOwn(values,prefix+name));
   await page.locator('#ip_mode').selectOption('dhcp');
   values=await page.evaluate(()=>Object.fromEntries(new FormData(document.querySelector('form'))));
   assert(!Object.hasOwn(values,'ip'));assert.equal(values.eth_ip,'192.0.2.20');
   await page.locator('#ip_mode').selectOption('static');
   assert.equal(await page.locator('#ip').inputValue(),'192.0.2.10');
   for(const prefix of ['','eth_'])assert(await page.locator('#'+prefix+'ip').evaluate(e=>e.required));
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`page overflow ${lang}/${width}`);
   assert(await page.locator('input,select').evaluateAll(nodes=>nodes.every(e=>{const r=e.getBoundingClientRect();return r.left>=0&&r.right<=innerWidth})),`field overflow ${lang}/${width}`);
   assert.deepEqual(errors,[]);
   await page.screenshot({path:path.join(out,`ipv4-${lang}-${width}.png`),fullPage:true});
   await page.close();
  }
  console.log('PASS: actual C++ IPv4 forms + production toggle JS, independent LAN/WLAN, DHCP omission, retained values, DE/EN and 320/390/768/1440 layouts');
 }finally{await browser.close()}
})().catch(error=>{console.error(error);process.exitCode=1});
