'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const read=file=>fs.readFileSync(path.join(src,file),'utf8');
const status=read('WebStatus.inc.h'),shell=read('WebShell.inc.h'),transport=read('WebTransportIcons.h');
const css=file=>read(file).match(/<style>([\s\S]*?)<\/style>/)[1];
const shared=css('WebHeaderStyles.h'),form=css('WebFormStyles.h');
assert(read('WebTypography.h').includes('OFE_WEB_HEADER_STYLE'));
assert(status.includes('OFE_WEB_TYPOGRAPHY_STYLE')&&shell.includes('OFE_WEB_TYPOGRAPHY_STYLE'));
const statusCss=status.match(/<style>([\s\S]*?)<\/style>/)[1];
const shellCss=JSON.parse(shell.match(/html \+= F\(("<\/title><style>[^\n]+?") OFE_WEB_TYPOGRAPHY_STYLE/)[1]).match(/<style>([\s\S]*?)<\/style>/)[1];
const icons=transport.match(/R"ICON\(([\s\S]*?)\)ICON"/)[1];
const script=transport.match(/R"ICONS\(([\s\S]*?)\)ICONS"/)[1];
const header=status.match(/<body>(<header>[\s\S]*?<\/header>)/)[1]
 .replace(/\)HTML" OFE_TRANSPORT_HEADER_HTML R"HTML\(/,icons)
 .replace('/logo.png','data:image/png;base64,'+fs.readFileSync(path.join(root,'assets/IceCube20_96.png')).toString('base64'));
const out=path.join(__dirname,'build-checks/mobile-header');fs.mkdirSync(out,{recursive:true});

(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.OFE_TEST_BROWSER_CHANNEL||(process.platform==='win32'?'msedge':undefined)});
 try{
  const results=[];
  for(const width of [320,360,390,430,700,1440])for(const de of [true,false])for(const dev of [true,false])for(const usb of [true,false])for(const [kind,base] of [['status',statusCss],['shell',shellCss]]){
   const page=await browser.newPage({viewport:{width,height:800}});
   await page.setContent(`<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,Helvetica,sans-serif}body{font-family:var(--ofe-font-ui)}${base}${form}</style>${header}<main><h1>Firmware-Updates</h1><section class="panel"><h2>MCU OTA</h2><input type="file"><button>MCU aktualisieren</button></section></main>`);
   await page.evaluate(({de,dev,usb})=>{
    const button=document.getElementById('dev_btn');button.textContent=de?'Entwicklermodus':'Developer mode';if(!dev)button.remove();
    document.getElementById('lang_sel').value=de?'de':'en';
    const labels=de?['Status','Netzwerk/System','Updates','Bus Diagnose','Logik Designer']:['Status','Network/System','Updates','Bus Diagnostics','Logic Designer'];
    document.querySelectorAll('.nav a').forEach((e,i)=>{e.textContent=labels[i];e.classList.toggle('active',i===3)});
    document.querySelector('.nav button').textContent=de?'Neustart':'Restart';
    if(usb){const a=document.createElement('a');a.href='/usb';a.className='btn secondary';a.textContent='USB';document.querySelector('.nav').append(a)}
    window.fetch=async()=>({ok:true,json:async()=>({active:true,network_transport:'ethernet',usb_detected:usb,usb_mounted:usb})});
   },{de,dev,usb});
   await page.addStyleTag({content:shared});
   await page.locator('body').evaluate((e,script)=>{const wrap=document.createElement('div');wrap.innerHTML=script;for(const style of wrap.querySelectorAll('style'))document.head.append(style);},script);
   await page.addScriptTag({content:script.match(/<script>([\s\S]*?)<\/script>/)[1]});
   await page.waitForFunction(usb=>!!document.querySelector('.transport-usb')&&!document.querySelector('.transport-usb').hidden===usb,usb);
   const before=await page.evaluate(shared=>{const style=[...document.styleSheets].find(s=>s.ownerNode.textContent===shared);style.disabled=true;const height=document.querySelector('header').getBoundingClientRect().height;style.disabled=false;return height},shared);
   const measure=await page.evaluate(()=>{
    const box=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom}};
    return {height:document.querySelector('header').getBoundingClientRect().height,overflow:document.documentElement.scrollWidth>innerWidth,
     brand:[...document.querySelector('.brand').children].map(box),nav:box(document.querySelector('.nav')),active:box(document.querySelector('.nav .active')),
     rows:[...document.querySelector('.nav').children].map(e=>Math.round(e.getBoundingClientRect().y)),
     clipped:[...document.querySelectorAll('.brand-text,.brand-dev,.nav>*')].filter(e=>e.scrollWidth>e.clientWidth+1).map(e=>e.className)};
   });
   const label=JSON.stringify({width,de,dev,usb,kind});
   assert.equal(measure.overflow,false,label+' page overflow');assert.deepEqual(measure.clipped,[],label+' clipped text');
   if(width<=700){
    assert(measure.height<=120,label+' header too tall: '+measure.height);
    assert.equal(new Set(measure.rows).size,1,label+' nav wrapped');
    for(const r of measure.brand)assert(r.x>=0&&r.right<=width+1,label+' brand outside viewport');
    for(let i=1;i<measure.brand.length;i++)assert(measure.brand[i].x>=measure.brand[i-1].right-1,label+' brand overlap');
    assert(measure.active.x>=measure.nav.x-1&&measure.active.right<=measure.nav.right+1,label+' active page not visible');
    await page.locator('.nav').evaluate(e=>{e.scrollLeft=e.scrollWidth});
    assert(await page.locator('.nav a').last().evaluate(e=>{const r=e.getBoundingClientRect();return r.x>=0&&r.right<=innerWidth}),label+' last destination unreachable');
   }
   if(dev){
    assert.equal(await page.locator('.brand-dev').getAttribute('title'),de?'Entwicklermodus':'Developer mode');
    await page.locator('.brand-dev').evaluate(e=>{e.textContent='Developer mode enabled'});
    await page.waitForFunction(()=>document.querySelector('.brand-dev').title==='Developer mode enabled');
   }
   if(de&&dev&&usb&&kind==='status'&&[320,390,1440].includes(width)){
    await page.locator('.nav').evaluate(e=>{const a=e.querySelector('.active');e.scrollLeft=Math.max(0,a.offsetLeft-e.offsetLeft-6)});
    await page.screenshot({path:path.join(out,width+'.png')});
   }
   results.push({width,de,dev,usb,kind,height:measure.height,before});
   await page.close();
  }
  console.log('PASS: compact shared mobile header; '+results.length+' status/shell, DE/EN, developer/community, USB/no USB and mobile/desktop cases; no clipping or page overflow');
  console.log('Mobile header heights: '+[...new Set(results.filter(r=>r.width<=700).map(r=>r.height))].join(', ')+' px');
  console.log('390px DE developer/USB: '+JSON.stringify(results.find(r=>r.width===390&&r.de&&r.dev&&r.usb&&r.kind==='status')));
  console.log('Screenshots: '+out);
 }finally{await browser.close()}
})().catch(error=>{console.error(error);process.exitCode=1});
