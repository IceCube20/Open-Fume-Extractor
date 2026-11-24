'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const read=file=>fs.readFileSync(path.join(src,file),'utf8'),source=read('WebStatus.inc.h');
const start=source.indexOf('<div class="overview-group"><div class="section-title master-section-title">');
const panel=(source.slice(start,source.indexOf('\n</div></div>',start))+'</div></div>').replace(')HTML" MASTER_MCU_MODEL R"HTML(','MCU-P4');
const script=source.slice(source.indexOf('function statusPixelLine('),source.indexOf('function t(k){'));
const css=source.match(/<style>([\s\S]*?)<\/style>/)[1]+read('WebTypography.h').match(/<style>([\s\S]*?)<\/style>/)[1]+read('WebHeaderStyles.h').match(/<style>([\s\S]*?)<\/style>/)[1];
const out=path.join(__dirname,'build-checks/mcu-system-layout');fs.mkdirSync(out,{recursive:true});
const settle=page=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));

(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.OFE_TEST_BROWSER_CHANNEL||(process.platform==='win32'?'msedge':undefined)});
 let cases=0;
 try{
  for(const lang of ['de','en'])for(const dev of [false,true])for(const ethernet of [false,true]){
   const page=await browser.newPage({viewport:{width:1920,height:1080}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.setContent(`<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,sans-serif}body{font-family:var(--ofe-font-ui)}${css}</style><main>${panel}<section class="status-modules"></section></main>`);
   await page.evaluate(({lang,dev,ethernet})=>{
    const values={master_fw:dev?'2.1.43RC-Developer':'2.1.43RC',master_serial_number:'0000ACFFE1A0ED30',master_ip:'192.168.20.147',uptime:'12h 32m',datetime:'2026-11-24 10:42:53',heap:'312 KiB',heap_diag:'Internal 312 KiB',heap_diag_low:'Minimum 182 KiB',heap_diag_block:'Largest 174 KiB',cpu_load:'CPU 16%',cpu_cores:'Core 0: 12% / Core 1: 20%',loop_max_dev:'Loop max 21 ms',mods:'8',wifi_db:'-44 dBm',wifi_ssid:'Homeassistant Devices',mqtt_perf:'Connected / 0 retries',mqtt_perf_build:'Build avg 0.3 / max 4.2 ms',mqtt_perf_publish:'Publish 9 messages / 860 B',http_perf:'HTTP 1 client / JSON 24 KiB',http_perf_status:'Status avg 2.3 / max 14.8 ms',http_perf_ha:'HA avg 1.2 / max 3.7 ms',runtime_profile:'Local 0.3 ms / Scheduler 0.8 ms / Logic 0.2 ms / Maintenance 0.1 ms'};
    Object.entries(values).forEach(([id,value])=>document.getElementById(id).textContent=value);
    document.body.classList.toggle('dev-mode',dev);document.getElementById('network_mode').hidden=!ethernet;document.getElementById('wifi_meter').style.display=ethernet?'none':'';document.getElementById('wifi_ssid').style.display=ethernet?'none':'';
    const labels=lang==='de'?{master_system:'MCU-System',master_serial_number:'Seriennummer',uptime:'Laufzeit',datetime:'Datum/Zeit',modules:'Module',heap_free:'Freier Heap',cpu_load:'CPU'}:{};
    document.querySelectorAll('[data-i18n]').forEach(e=>{if(labels[e.dataset.i18n])e.textContent=labels[e.dataset.i18n]});
    document.getElementById('network_title').textContent=ethernet?(lang==='de'?'Netzwerk':'Network'):'WLAN';
   },{lang,dev,ethernet});
   await page.addScriptTag({content:script});
   for(const width of [1920,320,390,520,521,760,844,1050,1051,1366,1440,1600,1640,2560,1366,1920]){
    await page.setViewportSize({width,height:1080});await settle(page);
    const result=await page.evaluate(()=>{
     const grid=document.querySelector('.master-stats'),g=grid.getBoundingClientRect(),cells=[...grid.children].filter(e=>e.matches('.stat-cell')&&e.getBoundingClientRect().width);
     return {overflow:document.documentElement.scrollWidth>innerWidth+1,grid:g.toJSON(),columns:Number(grid.style.getPropertyValue('--master-columns')),cells:cells.map(e=>{
      const b=e.getBoundingClientRect(),divider=e.querySelector(':scope > .status-pixel-divider'),rule=e.querySelector(':scope > .status-pixel-rule'),s=getComputedStyle(e);
      const next=cells.some(other=>{const r=other.getBoundingClientRect();return other!==e&&Math.abs(r.left-b.right)<1&&Math.min(r.bottom,b.bottom)-Math.max(r.top,b.top)>1});
      return {box:b.toJSON(),next,divider:divider&&getComputedStyle(divider).display!=='none',rule:rule&&getComputedStyle(rule).display!=='none',borders:[s.borderLeftWidth,s.borderRightWidth,s.borderTopWidth,s.borderBottomWidth],clipped:[...e.querySelectorAll('.k,.v,.mini,.ssid')].filter(el=>el.getBoundingClientRect().width&&el.scrollWidth>el.clientWidth+1).map(el=>el.id),svgCount:e.querySelectorAll(':scope > .status-pixel-divider').length};
     })};
    });
    const label=JSON.stringify({width,lang,dev,ethernet});assert.equal(result.overflow,false,label+' page overflow');
    for(const c of result.cells){
     assert.deepEqual(c.borders,['0px','0px','0px','0px'],label+' old borders double the separators');
     assert.equal(c.divider,width>520&&c.next,label+' divider only between adjacent visible cells');
     assert.equal(c.rule,c.box.top>result.grid.top+1,label+' horizontal separator exactly once below first row');
     assert.equal(c.svgCount,1,label+' duplicate divider');assert.deepEqual(c.clipped,[],label+' clipped text');
     assert(c.box.left>=result.grid.left-.1&&c.box.right<=result.grid.right+.1,label+' cell outside grid');
    }
    if(width<=520)assert.equal(result.columns,1,label+' mobile column');
    if(!dev&&width>=1640){assert.equal(result.columns,8,label+' full-width normal system row');assert(result.cells.every(c=>Math.abs(c.box.top-result.grid.top)<1),label+' eight fields must share one row')}
    if(!dev&&width===1366)assert.equal(result.columns,4,label+' balanced laptop rows');
    if(dev&&width===1920)assert.equal(result.columns,7,label+' two-column diagnostics participate in balancing');
    if(lang==='de'&&ethernet&&[390,1366,1920].includes(width))await page.screenshot({path:path.join(out,`${width}-${dev?'developer':'normal'}.png`),fullPage:true});
    assert.deepEqual(errors,[],label+' JS errors');cases++;
   }
   await page.close();
  }
  console.log(`PASS: ${cases} real MCU-System layouts; responsive balanced rows, single interior dividers, no outer-edge/double lines, real values, DE/EN, WiFi/Ethernet, developer/community and repeated resizing`);
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
