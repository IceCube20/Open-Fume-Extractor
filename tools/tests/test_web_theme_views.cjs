'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const src=path.resolve(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'),read=n=>fs.readFileSync(path.join(src,n),'utf8');
const status=read('WebStatus.inc.h'),theme=read('WebTheme.h').match(/R"THEME\(([\s\S]*?)\)THEME"/)[1];
const raw=(file,tag)=>read(file).match(new RegExp('R"'+tag+'\\(([\\s\\S]*?)\\)'+tag+'"'))[1];
const typography=JSON.parse(read('WebTypography.h').match(/#define OFE_WEB_TYPOGRAPHY_STYLE ("[^\n]+?") OFE_WEB_FORM_STYLE/)[1]);
const styles=typography+raw('WebFormStyles.h','FORM')+raw('WebHeaderStyles.h','HEADER')+theme;
const htmls=[...status.matchAll(/static const char html[12]\[\] PROGMEM = R"HTML\(([\s\S]*?)\)HTML";/g)].map(m=>m[1]);
assert.equal(htmls.length,2);
const macros={OFE_WEB_TYPOGRAPHY_STYLE:styles,OFE_TRANSPORT_HEADER_HTML:raw('WebTransportIcons.h','ICON'),MASTER_MCU_MODEL:'MCU-P4'};
const resolve=text=>text.replace(/\)HTML" (\w+) R"HTML\(/g,(_,name)=>{assert(name in macros,name);return macros[name]});
const html=resolve(htmls[0])+raw('WebTransportIcons.h','ICONS')+raw('WebShell.inc.h','JS')+raw('WebAutomationSignals.h','HTML')+resolve(htmls[1]);
const modules=[
 {type:1,addr:16,label:'JBC FAE Bus DDE',station_type:'DDE',caps:1,jbc_link_flags:1,station_addr:1,jbc_work_mask:1},
 {type:9,addr:17,label:'JBC USB DDE',caps:65537,jbc_usb_connected:true,jbc_usb_station:'DDE'},
 {type:2,addr:32,label:'Fan/IO',caps:81956,io_input_mask:1,io_output_mask:1},
 {type:3,addr:33,label:'Fan/IO Pro',caps:82212,io_input_mask:0,io_output_mask:0,fanio_filter_runtime_valid:true,fanio_filter_mode:2,fanio_filter_flags:3,fanio_filter_cal_quality:1},
 {type:5,addr:48,label:'Weller Zero Smog',caps:16420,weller_connected:true},
 {type:6,addr:64,label:'Display P4 800x480',caps:1024},
 {type:7,addr:80,label:'Universal RS232',caps:65536},
 {type:8,addr:96,label:'Modbus RTU',caps:65536}
].map((m,i)=>({uid:'test-'+i,online:true,type_name:m.label,module_fw:'1.0.0RC',telemetry_valid:true,module_cpu_load_pct:12,module_heap_free:130000,module_loop_max_ms:3,module_uptime_s:12500,io_in1_alias:'Werkbank',io_in2_alias:'Freigabe',io_out1_alias:'Licht',io_out2_alias:'Ventil',universal_descriptor:'',...m}));
for(const m of modules.filter(m=>m.type===2||m.type===3)){
 m.universal_descriptor='schema=1\nfan_enabled=1\nfan_output_mode=2\nentities:\n40 switch out1 rw source=profile access=rw role=output_enable en=Licht gpio=23 idx=0 value_on=1 value_off=0\n';
 m.universal_descriptor_valid=true;m.universal_descriptor_crc=m.addr;
 m.universal_entities=[{id:40,value_bool:false,value:'0'}];
}
const state={modules,master_fw:'2.1.49RC-Developer',master_model:'MCU-P4',master_serial_number:'0000ACFFE1A0ED30',uptime_s:3800,datetime:'2026-11-24 17:45:00',heap_free:320000,cpu_load_pct:16,wifi_rssi:-48,wifi_ssid:'Werkstatt',ip:'192.168.20.147',level:1,power:40,afterrun_s:15,afterrun_power:30,output_addr:32,output_power:400,jbc:{suction_level:1,select_flow:400,delay_work_sec:15,delay_stand_sec:15,stand_intakes:true,continuous:false},output_module:{valid:true,power:400,rpm:2100},usb_detected:true,usb_mounted:true,modules_count:8,active_output_addr:32,master_dev:true,network_transport:'ethernet',ethernet_connected:true};
const out=path.resolve(__dirname,'build-checks/theme-views');fs.mkdirSync(out,{recursive:true});
(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 try{
  for(const mode of ['light','dark'])for(const width of [390,1440]){
   const page=await browser.newPage({viewport:{width,height:1000},colorScheme:mode}),errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.addInitScript(()=>{window.setInterval=()=>0});
   await page.route('http://ofe.test/**',route=>{
    const p=new URL(route.request().url()).pathname;
    if(p==='/')return route.fulfill({contentType:'text/html',body:html});
    if(p==='/logo.png'||p==='/favicon.png')return route.fulfill({contentType:'image/png',body:fs.readFileSync(path.resolve(src,'../../..','assets/IceCube20_96.png'))});
    const data=p==='/state'?state:p==='/descriptors'?{modules}:p.includes('logic')?{items:[],nodes:[]}:p==='/display-link/ble'?{active:true,...state}:{};
    return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
   });
   await page.goto('http://ofe.test/');await page.waitForFunction(()=>document.querySelectorAll('#details .module-card').length===8).catch(e=>{throw Error(e.message+' / '+errors.join(';'))});
   await page.screenshot({path:path.join(out,mode+'-'+width+'.png'),fullPage:false});
   await page.evaluate(()=>scrollTo(0,document.getElementById('details').getBoundingClientRect().top+scrollY-document.querySelector('header').getBoundingClientRect().height-10));
   await page.screenshot({path:path.join(out,mode+'-modules-'+width+'.png'),fullPage:false});
   const appearance=await page.evaluate(()=>{
    const style=e=>{const s=getComputedStyle(e);return {bg:s.backgroundColor,color:s.color,radius:s.borderRadius,border:s.borderTopWidth}};
    return {pills:[...document.querySelectorAll('#details .module-statuses .pill')].map(style),
      icons:[...document.querySelectorAll('#details .module-icon')].map(style),
      controls:[...document.querySelectorAll('#details .inline-alias')].map(style),field:style(document.querySelector('#jbc_suction')),
      moduleTypes:[...document.querySelectorAll('.module-list-icon')].map(e=>({type:e.dataset.moduleType,...style(e)}))};
   });
   if(mode==='light'){
    await page.waitForFunction(()=>document.querySelector('.transport-usb.is-on'));
    const headerColors=await page.evaluate(()=>Object.fromEntries(['ble','network','usb'].map(k=>[k,getComputedStyle(document.querySelector('.transport-'+k)).color])));
    assert.equal(headerColors.ble,'rgb(36, 86, 127)');assert.equal(headerColors.network,'rgb(32, 95, 60)');assert.equal(headerColors.usb,'rgb(32, 95, 60)');
    const work=await page.locator('#jbc_workbox_16').evaluate(e=>({class:e.className,bg:getComputedStyle(e).backgroundColor,color:getComputedStyle(e).color}));
    assert.equal(work.bg,'rgb(196, 219, 206)',JSON.stringify(work));
    const badge=page.locator('#io_cal_badge_33');await badge.waitFor({state:'visible'});
    assert.match(await badge.textContent(),/unvollst/i);assert.match(await badge.getAttribute('class'),/is-incomplete/);
    assert.equal(await badge.evaluate(e=>getComputedStyle(e).borderTopWidth),'1px');
    assert.notEqual(await badge.evaluate(e=>getComputedStyle(e).backgroundColor),await page.locator('#io_filter_cal_wrap_33').evaluate(e=>getComputedStyle(e).backgroundColor));
    for(const addr of [32,33]){
     const main=page.locator('#io_fan_'+addr),entity=page.locator('#uni_ent_sw_'+addr+'_40');await entity.waitFor();
     for(const on of [false,true]){
      await page.evaluate(({addr,on})=>{document.getElementById('io_fan_'+addr).className='switch '+(on?'on':'off');document.getElementById('uni_ent_sw_'+addr+'_40').className='switch uni-switch '+(on?'on':'off')},{addr,on});
      await page.waitForTimeout(200);
      const bg=loc=>loc.evaluate(e=>getComputedStyle(e).backgroundColor);
      assert.equal(await bg(main),await bg(entity),'Same Fan/IO main and entity switch color '+addr+'/'+on);
      for(const loc of [main,entity]){await loc.hover();assert.equal(await loc.evaluate(e=>getComputedStyle(e).filter),'brightness(1.14)','Enabled switch lightens on hover');await loc.evaluate(e=>e.disabled=true);assert.equal(await loc.evaluate(e=>getComputedStyle(e).filter),'none','Disabled switch has no hover effect');await loc.evaluate(e=>e.disabled=false)}
     }
    }
    await page.locator('#io_filter_cal_wrap_33').evaluate(e=>e.open=true);
    await page.locator('#io_cal_badge_33').scrollIntoViewIfNeeded();
    await page.screenshot({path:path.join(out,'light-calibration-'+width+'.png'),fullPage:false});
    for(const [flags,online,expected] of [[19,true,'is-ready'],[0,true,'is-disabled'],[3,false,'is-offline']]){
     await page.evaluate(({flags,online})=>{const d=structuredClone(window.lastState);const m=d.modules.find(m=>m.addr===33);m.fanio_filter_flags=flags;m.online=online;updateDetails(d)},{flags,online});
     assert.match(await badge.getAttribute('class'),new RegExp(expected));
     assert.equal(await badge.evaluate(e=>getComputedStyle(e).borderTopWidth),'1px');
    }
    await page.evaluate(()=>updateDetails(window.lastState));
    for(const pill of appearance.pills){assert(parseFloat(pill.radius)>=99,'Capsule geometry');assert.equal(pill.border,'1px');assert.notEqual(pill.bg,'rgb(225, 230, 235)','Pills remain distinct from cards')}
    for(const input of appearance.controls)assert.equal(input.bg,'rgba(0, 0, 0, 0)','Editable aliases are unframed at rest');
    assert.notEqual(appearance.field.bg,'rgb(255, 255, 255)','No white control surfaces');
    assert(new Set(appearance.icons.map(x=>x.color)).size>=6,'Module icon identities retained');
    for(const type of [1,2,3,5,6,7,8,9]){const icon=appearance.moduleTypes.find(m=>Number(m.type)===type);assert(icon,'Module table type '+type);assert.notEqual(icon.color,'rgb(34, 49, 63)','Icons must not inherit body text')}
   }
   if(mode==='light'){
    const pill=page.locator('#details .module-statuses .pill').first();
    const variants=[];
    for(const name of ['','on','warn','critical']){await pill.evaluate((e,n)=>e.className='pill '+n,name);variants.push(await pill.evaluate(e=>({bg:getComputedStyle(e).backgroundColor,color:getComputedStyle(e).color,radius:getComputedStyle(e).borderRadius})))}
    assert.equal(new Set(variants.map(s=>s.bg)).size,4,'Each status keeps its own capsule color');
    assert(variants.every(s=>parseFloat(s.radius)>=99));
   }
   assert.deepEqual(errors,[],mode+'/'+width);
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);
   await page.close();
  }
  console.log('PASS: complete streamed status page, all eight module types, light/dark, desktop/mobile');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
