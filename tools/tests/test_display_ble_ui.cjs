const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..');
const source=fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/src/WebDisplayBle.inc.h'),'utf8').split('static void append_ble_pairing_ui')[1];
const header=fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/src/WebTransportIcons.h'),'utf8');
const icons=header.match(/R"ICON\(([\s\S]*?)\)ICON"/)[1];
const iconScript=header.match(/R"ICONS\(([\s\S]*?)\)ICONS"/)[1];
const shell=fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/src/WebShell.inc.h'),'utf8');
const css=shell.match(/<style>([\s\S]*?)<\/style>/)[1];
const decode=s=>JSON.parse('"'+s+'"');
const typography=decode(fs.readFileSync(path.join(root,'OpenFumeExtractorMaster/src/WebTypography.h'),'utf8').match(/#define OFE_WEB_TYPOGRAPHY_STYLE "((?:\\.|[^"\\])*)"/)[1]);
function pairing(de){
  const re=/html\+=F\("((?:\\.|[^"\\])*)"\);|html\+=web_text\("((?:\\.|[^"\\])*)","((?:\\.|[^"\\])*)"\);|html\+=web_is_german\(\)[^\n]*;|html\+=R"JS\(([\s\S]*?)\)JS";/g;
  return [...source.matchAll(re)].map(m=>m[1]!==undefined?decode(m[1]):m[2]!==undefined?decode(de?m[2]:m[3]):m[4]!==undefined?m[4]:'const BLE_DE='+de+';').join('');
}
(async()=>{
  const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  const output=path.join(__dirname,'build-checks/display-ble');fs.mkdirSync(output,{recursive:true});
  try{
    for(const de of [false,true])for(const width of [360,1280]){
      const page=await browser.newPage({viewport:{width,height:800}});
      let state={active:false,phase:0,wifi:true,token:12,pin:0,devices:[]};
      const posted=[];
      await page.route('http://ofe.test/display-link/ble**',async route=>{
        if(route.request().method()==='POST'){
          const p=Object.fromEntries(new URLSearchParams(route.request().postData()));posted.push(p);
          if(p.action==='start')state={...state,active:true,phase:2};
          if(p.action==='select')state={...state,active:true,phase:6,token:345,pin:123};
          if(p.action==='confirm')state={...state,phase:7};
          if(p.action==='cancel')state={...state,active:false,phase:11};
          await route.fulfill({json:{ok:true}});
        }else await route.fulfill({json:{...state,searching:state.active&&[1,2,3].includes(state.phase),
          search_elapsed_ms:state.search_elapsed_ms||0,search_timeout_ms:120000}});
      });
      await page.route('http://ofe.test/',route=>route.fulfill({contentType:'text/html',body:
        '<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><style>'+css+'</style>'+typography+'<header><div class="brand"><div class="brand-text">Open Fume Extractor<span>by IceCube20</span></div><button>Developer mode</button>'+icons+'</div><nav class="nav"><a class="btn">Status</a><a class="btn">Network/System</a><a class="btn">Updates</a></nav></header><main>'+pairing(de)+'</main>'+iconScript}));
      await page.goto('http://ofe.test/');
      await page.locator('#ble .actions button').first().click();
      assert.equal(posted.at(-1).action,'start');
      state={...state,phase:3,search_elapsed_ms:23000,devices:[{kind:1,serial:'400000000000002A',rssi:-44,paired:false},
        {kind:0,serial:'400000000000002B',rssi:-48,paired:false}]};
      await page.evaluate(()=>bleRefresh());
      assert.equal(await page.locator('#ble_search_timer').isVisible(),true);
      assert.equal(await page.locator('#ble_search_timer').textContent(),
        (de?'Suche läuft: ':'Search running: ')+'00:23 / 02:00');
      await page.screenshot({path:path.join(output,`search-${width}-${de?'de':'en'}.png`)});
      state={...state,phase:2,search_elapsed_ms:31000};await page.evaluate(()=>bleRefresh());
      assert((await page.locator('#ble_search_timer').textContent()).includes('00:31'));
      await page.locator('#ble_devices button').first().click();
      assert.deepEqual(posted.at(-1),{action:'select',index:'0',token:'12'});
      await page.waitForFunction(()=>document.getElementById('ble_pin').textContent==='000123');
      assert.equal(await page.locator('#ble_pin').textContent(),'000123');
      assert.equal(await page.locator('#ble_confirm').isVisible(),true);
      await page.screenshot({path:path.join(output,`${width}-${de?'de':'en'}.png`)});
      await page.locator('#ble_confirm button').first().click();
      assert.deepEqual(posted.at(-1),{action:'confirm',token:'345',pin:'123',accept:'1'});
      await page.waitForFunction(()=>document.getElementById('ble_confirm').hidden);
      assert.equal(await page.locator('#ble_confirm').isVisible(),false);
      state={...state,active:false,phase:9,token:456,devices:state.devices.map((d,i)=>({...d,paired:i===0}))};
      await page.evaluate(()=>bleRefresh());
      assert.equal(await page.locator('#ble_search_timer').isVisible(),false);
      assert.equal(await page.locator('#ble_state').textContent(),de?'Display gekoppelt':'Display paired');
      assert.equal(await page.locator('#ble_devices button').first().isDisabled(),true);
      assert.equal(await page.locator('#ble_devices button').first().textContent(),de?'Gekoppelt':'Paired');
      assert.equal(await page.locator('#ble_devices button').last().isDisabled(),false);
      await page.locator('#ble_devices button').last().click();
      assert.deepEqual(posted.at(-1),{action:'select',index:'1',token:'456'});
      assert.equal(posted.filter(p=>p.action==='start').length,1);
      assert(await page.locator('.transport-wifi').evaluate(e=>e.classList.contains('is-on')));
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
      for(const selector of ['#ble button','.transport-icons a']){
        const clipped=await page.locator(selector).evaluateAll(es=>es.filter(e=>e.scrollWidth>e.clientWidth+1).map(e=>e.textContent));
        assert.deepEqual(clipped,[]);
      }
      state={...state,active:false,phase:13,free_internal:18*1024,largest_internal:16*1024,memory_stage:3};
      await page.evaluate(()=>bleRefresh());
      const memory=await page.locator('#ble_state').textContent();
      assert(memory.includes(de?'nach Bluetooth-Start':'after Bluetooth startup'));
      assert(memory.includes('18 KiB')&&memory.includes('16 KiB'));
      state={...state,active:false,phase:10,error_phase:7,error_code:520};
      await page.evaluate(()=>bleRefresh());
      const failure=await page.locator('#ble_state').textContent();
      assert(failure.includes('BLE 520')&&!failure.includes(de?'Zeit abgelaufen':'timed out'));
      state={...state,active:true,phase:6,pin:999999,token:777};await page.evaluate(()=>bleRefresh());
      await page.locator('#ble_confirm button').last().click();
      assert.equal(posted.at(-1).accept,'0');
      await page.close();
    }
  }finally{await browser.close()}
  console.log('PASS: actual BLE pairing UI, DE/EN, desktop/mobile, confirmation/rejection, stale-state disabled controls and header icons');
})().catch(e=>{console.error(e);process.exitCode=1});
