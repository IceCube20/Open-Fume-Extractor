// Preview the production LVGL coordinates emitted by test_pairing_layout.cpp.
const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const {chromium}=require('playwright');
const output=path.join(__dirname,'build-checks/display-pairing');
const escape=s=>s.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;');
(async()=>{
  const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try {
    for(const width of [480,800]) for(const lang of [0,1]) for(const light of [false,true]) {
      const height=width===800 ? 480 : 320;
      const nodes=JSON.parse(fs.readFileSync(path.join(output,`${width}-${lang}.json`),'utf8').replace(/^\uFEFF/,''));
      const page=await browser.newPage({viewport:{width,height}});
      function render(n) {
        const title=n.text==='Display connection' || n.text==='Display-Verbindung';
        const heading=['WiFi','WLAN','Master'].includes(n.text);
        const primary=n.text==='Apply' || n.text==='Übernehmen';
        const danger=n.text==='Forget master pairing' || n.text==='Master-Kopplung löschen';
        return `<div class="node ${n.kind}${title?' title':''}${heading?' heading':''}${primary?' primary':''}${danger?' danger':''}" style="left:${n.x}px;top:${n.y}px;width:${n.width}px;height:${n.height}px"><span>${escape(n.text)}</span>${n.kind==='select'?'<b>&#9662;</b>':''}</div>`;
      }
      const bottom=Math.max(...nodes.filter(n=>n.form).map(n=>n.y+n.height));
      await page.setContent(`<style>
        *{box-sizing:border-box}body{margin:0;font:14px Arial,sans-serif;background:${light?'#f5f7fa':'#121820'};color:${light?'#17212b':'#f3f6fa'}}
        header{height:42px;border-bottom:1px solid #647889;padding:10px 12px;font-size:13px}header span{float:right;color:#58b8ff}
        .node{position:absolute;display:flex;align-items:center}.label{white-space:nowrap}.title{font-size:18px}.heading{color:${light?'#14628f':'#58b8ff'}}
        .button,.input,.select{background:${light?'#fff':'#22303d'};border:1px solid ${light?'#ccd8e3':'#364959'};border-radius:6px;padding:0 12px;white-space:nowrap}
        .button{justify-content:center}.select b{font-size:11px;margin-left:auto}.node span{max-width:100%}.input span{overflow:hidden;white-space:nowrap}
        .primary{background:#176792;color:white}.danger{color:${light?'#af3434':'#ffaaa4'}}
        .scroll{position:absolute;left:12px;top:92px;width:${width-24}px;height:${height-96}px;overflow:auto;scrollbar-width:thin}
        .content{position:relative;height:${bottom}px}
      </style><header>Open Fume Extractor <span>12:34 &nbsp; ECO &nbsp; BT &nbsp; WiFi</span></header>
      ${nodes.filter(n=>!n.form).map(render).join('')}<div class="scroll"><div class="content">${nodes.filter(n=>n.form).map(render).join('')}</div></div>`);
      const clipped=await page.locator('.button span,.select span,.label span').evaluateAll(items=>items.filter(e=>e.scrollWidth>e.clientWidth+1).map(e=>e.textContent));
      assert.deepEqual(clipped,[],'Clipped pairing text: '+clipped.join(', '));
      if(width===800) assert(bottom<=height-96,'Wide pairing form must fit without scrolling');
      const name=`${width}-${lang}-${light?'light':'dark'}`;
      await page.screenshot({path:path.join(output,name+'.png')});
      if(width===480) {
        await page.locator('.scroll').evaluate(e=>{e.scrollTop=180});
        await page.screenshot({path:path.join(output,name+'-wifi.png')});
        await page.locator('.scroll').evaluate(e=>{e.scrollTop=e.scrollHeight});
        await page.screenshot({path:path.join(output,name+'-master.png')});
      }
      await page.close();
    }
  } finally { await browser.close(); }
  console.log('PASS: production pairing geometry, DE/EN, dark/light, both sizes, no clipped labels/buttons; wide form fits');
})().catch(e=>{console.error(e);process.exitCode=1});
