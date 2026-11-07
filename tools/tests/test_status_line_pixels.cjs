const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const {chromium}=require('playwright');
const {PNG}=require('pngjs');
const src=fs.readFileSync(path.join(__dirname,'../../OpenFumeExtractorMaster/src/WebStatus.inc.h'),'utf8');
const typography=fs.readFileSync(path.join(__dirname,'../../OpenFumeExtractorMaster/src/WebTypography.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const css=src.match(/<style>([\s\S]*?)<\/style>/)[1]+typography;
const metaSource=src.split('\n').find(line=>line.startsWith('function moduleMeta('));
const renderMeta=Function('m','t','ups','kb','commTransport','commClass','commText','return ('+metaSource+')(m)');
const moduleMeta=i=>renderMeta({addr:16+i,online:true,telemetry_valid:true,module_cpu_load_pct:12,module_heap_free:120000,module_loop_max_ms:3},
  key=>({uptime:'Uptime',offline_events:'Offline',miss_counter:'Miss'}[key]||key),
  ()=>'1h',()=>'120 KiB',()=>'RS485',()=>'on',()=>'OK');
const start=src.indexOf('<div class="overview-group"><div class="section-title master-section-title">');
const master=src.slice(start,src.indexOf('\n</div></div>',start))+'</div></div>';
const script=src.slice(src.indexOf('function statusPixelLine('),src.indexOf('function t(k){'));
const settle=page=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));

function profile(png,left,y,bg,color,dpr){
  const channel=color.map((v,i)=>Math.abs(v-bg[i])).indexOf(Math.max(...color.map((v,i)=>Math.abs(v-bg[i]))));
  const values=[];
  for(let x=Math.floor(left*dpr);x<Math.ceil((left+9)*dpr);x++){
    const value=(png.data[(Math.round(y*dpr)*png.width+x)*4+channel]-bg[channel])/(color[channel]-bg[channel]);
    if(value>0.04&&value<=1.04)values.push(value);
  }
  return values;
}
function horizontalPixels(png,x,top,color,dpr){
  let count=0;
  for(let y=Math.floor((top-3)*dpr);y<Math.ceil((top+3)*dpr);y++){
    const offset=(y*png.width+Math.round(x*dpr))*4;
    if(color.every((value,c)=>Math.abs(png.data[offset+c]-value)<2))++count;
  }
  return count;
}

(async()=>{
  const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try {
    const output=path.join(__dirname,'build-checks/status-line-pixels');fs.mkdirSync(output,{recursive:true});
    for(const dpr of [1,0.8,1.25,1.5,2])for(const width of [390,1366,1441,1920]){
      const page=await browser.newPage({viewport:{width,height:4000},deviceScaleFactor:dpr});
      const card=i=>'<div class="tile module-card jbc-card"><div class="module-head"><div class="module-title">Module '+i+'</div></div><div style="height:80px"></div>'+moduleMeta(i)+'</div>';
      await page.setContent('<style>'+css+'</style><main>'+master+'<div id="details" class="grid">'+Array.from({length:8},(_,i)=>card(i)).join('')+'</div></main>');
      await page.evaluate(code=>(0,eval)(code),script);
      for(const dev of [false,true]){
        await page.evaluate(dev=>document.body.classList.toggle('dev-mode',dev),dev);
        await settle(page);
        await page.waitForTimeout(100);
        const rects=await page.evaluate(()=>({
          stripes:Array.from(document.querySelectorAll('.module-head')).map(e=>{const r=e.getBoundingClientRect(),s=getComputedStyle(e,'::before');return {x:r.left,y:r.top,width:s.width,left:s.left,inline:e.style.cssText,dpr:devicePixelRatio}}),
          cells:Array.from(document.querySelectorAll('.master-stats>.stat-cell')).filter(e=>e.getBoundingClientRect().width).map(e=>{
            const r=e.getBoundingClientRect(),style=getComputedStyle(e.querySelector('.status-pixel-divider'));
            return {x:r.right,y:r.top+32,show:style.display!=='none',width:style.width};
          }),
          moduleCells:Array.from(document.querySelectorAll('#details .module-system>span')).filter(e=>e.getBoundingClientRect().width).map(e=>{
            const r=e.getBoundingClientRect(),style=getComputedStyle(e.querySelector('.status-pixel-divider'));
            return {x:r.right,y:r.top+r.height-7,show:style.display!=='none',border:getComputedStyle(e).borderLeftWidth};
          }),
          rules:Array.from(document.querySelectorAll('#details .module-system,#details .module-footer')).map(e=>{
            const r=e.getBoundingClientRect();return {x:r.left+8,y:r.top,color:getComputedStyle(e).borderTopColor};
          })
        }));
        assert.equal(rects.stripes.length,8);
        assert(rects.cells.length>=8,'Use the complete real Master System panel');
        const png=PNG.sync.read(await page.screenshot());
        for(const b of rects.stripes){
          const pixels=profile(png,b.x+2,b.y+40,[28,32,38],[57,199,121],dpr);
          assert.equal(pixels.length,Math.round(3*dpr),`Uniform accent pixels at ${width}/${dpr}/dev=${dev}: ${pixels} ${JSON.stringify(b)}`);
          assert(pixels.every(v=>Math.abs(v-1)<0.04),`No blurred stripe edges at ${width}/${dpr}/dev=${dev}: ${pixels} ${JSON.stringify(b)}`);
        }
        for(const b of rects.cells){
          const pixels=profile(png,b.x-3,b.y,[23,26,31],[41,47,55],dpr);
          if(b.show){
            assert.equal(pixels.length,1,`One-device-pixel Master divider at ${width}/${dpr}/dev=${dev}: ${pixels}`);
            assert(pixels.every(v=>Math.abs(v-1)<0.04),'Uniform divider intensity');
          }
          if(width<=520)assert(!b.show,'No vertical dividers in single-column mobile layout');
        }
        assert.equal(rects.moduleCells.length,dev?32:16,'Developer mode controls visible CPU/Heap/Loop/transport cells');
        assert.equal(rects.moduleCells.filter(b=>b.show).length,dev?24:0,'No divider at the outer card edge or before a hidden value');
        for(const b of rects.moduleCells){
          assert.equal(b.border,'0px','Remove the old border so the line is not drawn twice');
          if(!b.show)continue;
          const pixels=profile(png,b.x-3,b.y,[19,22,27],[43,49,57],dpr);
          assert.equal(pixels.length,1,`One-device-pixel CPU/Heap divider at ${width}/${dpr}: ${pixels}`);
          assert(pixels.every(v=>Math.abs(v-1)<0.04),'No blurred CPU/Heap divider');
        }
        assert.equal(rects.rules.length,16);
        for(const r of rects.rules){
          assert.equal(r.color,'rgba(0, 0, 0, 0)','Old horizontal border is transparent');
          assert.equal(horizontalPixels(png,r.x,r.y,[43,49,57],dpr),1,`One-device-pixel system/footer rule at ${width}/${dpr}`);
        }
        assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'No page overflow');
        if(dpr===0.8&&width===1441&&dev){
          const height=await page.locator('#details').evaluate(e=>Math.ceil(e.getBoundingClientRect().bottom+25));
          await page.screenshot({path:path.join(output,'status-80-percent-dev.png'),clip:{x:0,y:0,width,height}});
        }
      }
      // A rebuilt module list must be aligned without waiting for status polling.
      await page.locator('#details').evaluate((grid,html)=>grid.innerHTML=html,Array.from({length:8},(_,i)=>card(i)).join(''));
      await settle(page);
      assert(Math.abs(parseFloat(await page.locator('.module-head').last().evaluate(e=>e.style.getPropertyValue('--module-accent-width')))-Math.max(1,Math.round(3*dpr))/dpr)<0.0001);
      await page.close();
    }
    console.log('PASS: eight status cards, real module CPU/Heap metadata and Master System dividers: one-pixel vertical/horizontal lines at 80-200% DPR, mobile/desktop, developer mode and rebuilt cards');
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
