const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const {chromium} = require('playwright');
const root = path.join(__dirname, '../../OpenFumeExtractorMaster/src');
const read = name => fs.readFileSync(path.join(root, name), 'utf8');
const typography = read('WebTypography.h').match(/<style>([\s\S]*?)<\/style>/)[1];
const statusCss = read('WebStatus.inc.h').match(/<style>([\s\S]*?)<\/style>/)[1] + typography;
const statusSource = read('WebStatus.inc.h');
const lineScript = statusSource.slice(statusSource.indexOf('function statusPixelLine('),statusSource.indexOf('function t(k){'));
const diagCss = read('WebShell.inc.h').match(/<style>([\s\S]*?)<\/style>/)[1] +
  read('WebDiagnostics.inc.h').match(/R"CSS\(<style>([\s\S]*?)<\/style>\)CSS"/)[1] + typography;
const kinds = ['jbc', 'jbc', 'fan', 'fan', 'weller', 'display', 'universal', 'modbus'];
const colors = ['rgb(57, 199, 121)', 'rgb(57, 199, 121)', 'rgb(74, 144, 217)', 'rgb(74, 144, 217)', 'rgb(85, 185, 202)',
  'rgb(181, 140, 255)', 'rgb(241, 184, 75)', 'rgb(226, 139, 82)'];

function cards(diag, de) {
  return kinds.map((kind, i) => {
    const name = ['JBC FAE Bus DDE', 'JBC USB CLMU', 'Fan/IO', 'Fan/IO Pro Testbank', 'Weller Zero Smog Bus',
      'Display ST7796 320x480', 'Universal RS232 Bridge', 'Modbus RTU Bridge'][i];
    const title = de ? name : name + ' connected device';
    const c = diag ? 'diag-module' : 'module';
    const typeClass = diag ? kind : kind === 'modbus' ? 'universal-card modbus-card' : kind + '-card';
    const copy = diag ? 'diag-module-copy' : 'module-head-copy';
    const statuses = diag ? 'diag-module-statuses' : 'module-statuses';
    return `<article class="${diag ? '' : 'tile '} ${c}-card ${typeClass} core-style">
      <div class="${c}-head"><div class="${c}-title">${title}</div>
        <div class="${c}-head-main"><div class="${c}-identity">
          <span class="${c}-icon">OFE</span><div class="${copy}">
            <div class="${c}-type">${name}</div><div class="${c}-address">0x${(16+i).toString(16)}</div>
          </div></div><div class="${statuses}"><span class="pill">RS485</span><span class="pill">online</span></div></div>
      </div><div class="led-pair">OFE · EVT</div><div class="test-body" style="padding:16px;min-height:${130+i*20}px">
        <div class="k">${de ? 'Leistung' : 'Power'}</div><div class="v">30 %</div>
        <button class="test-action">${de ? 'Aktualisieren' : 'Refresh'}</button>
      </div></article>`;
  }).join('');
}

(async () => {
  const browser = await chromium.launch({headless:true, executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try {
    const output = path.join(__dirname, 'build-checks/module-card-accents');
    fs.mkdirSync(output, {recursive:true});
    for (const diag of [false, true]) for (const de of [true, false]) {
      const css = diag ? diagCss : statusCss;
      for (const width of [320,390,760,1440]) {
        const page = await browser.newPage({viewport:{width,height:1000}});
        const grid = diag ? 'class="diag-module-grid"' : 'id="details" class="grid"';
        await page.setContent(`<style>${css}</style><main><div ${grid}>${cards(diag,de)}</div></main>`);
        if(!diag){
          await page.evaluate(code=>(0,eval)(code),lineScript);
          await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
        }
        const selector = diag ? '.diag-module-card' : '.module-card';
        const head = diag ? '.diag-module-head' : '.module-head';
        const inspect = () => page.locator(selector).evaluateAll((cards,head) => cards.map(card => {
          const box=card.getBoundingClientRect(), header=card.querySelector(head);
          const line=header.querySelector('.status-pixel-accent');
          const stripe=line?getComputedStyle(line):getComputedStyle(header,'::before'),headerBox=header.getBoundingClientRect();
          return {color:line?stripe.color:stripe.backgroundColor,width:stripe.width,left:stripe.left,top:stripe.top,
            bottom:stripe.bottom,height:parseFloat(stripe.height),radius:stripe.borderRadius,
            pointer:stripe.pointerEvents,position:stripe.position,headerHeight:headerBox.height,
            outside:getComputedStyle(card,'::after').content,
            shadow:getComputedStyle(card.querySelector(head)).boxShadow,
            body:card.querySelector('.test-body').getBoundingClientRect().toJSON(),
            contentLeft:card.querySelector('.test-body .k').getBoundingClientRect().left,
            card:box.toJSON()};
        }),head);
        const results = await inspect();
        results.forEach((r,i) => {
          assert.equal(r.color,colors[i]);
          assert.equal(r.width,'3px');
          assert(Math.abs(parseFloat(r.left)-4)<=0.5,'Pixel snapping stays within half a pixel');
          assert.equal(r.top,'12px');
          assert(r.bottom==='12px'||r.bottom==='auto');
          assert.equal(r.radius,'0px');
          assert.equal(r.pointer,'none');
          assert.equal(r.position,'absolute');
          assert.equal(r.shadow,'none','No second stripe left on the header');
          assert(Math.abs(r.height-(r.headerHeight-25))<1,'Stripe stays inside the header, not the full card');
          assert.equal(r.outside,'none','No full-height card stripe');
          assert(r.contentLeft>=r.card.left+10,'Content stays clear of the stripe');
        });
        assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'No horizontal overflow');
        await page.locator('.test-action').first().evaluate(button=>button.onclick=()=>{button.dataset.clicked='yes';});
        await page.locator('.test-action').first().click();
        assert.equal(await page.locator('.test-action').first().getAttribute('data-clicked'),'yes');
        await page.locator('.test-body').first().evaluate(body=>body.style.minHeight='520px');
        const expanded = await inspect();
        assert.equal(expanded[0].height,results[0].height,'Expanded body must not stretch the header stripe');
        // Decoration must not introduce any layout changes.
        await page.addStyleTag({content:`${selector}>${head}::before{content:none!important}.status-pixel-accent{visibility:hidden}`});
        const without = await inspect();
        assert.deepEqual(without.map(r=>r.card),expanded.map(r=>r.card));
        assert.deepEqual(without.map(r=>r.body),expanded.map(r=>r.body));
        await page.addStyleTag({content:`${selector}>${head}::before{content:""!important}.status-pixel-accent{visibility:visible}`});
        await page.locator('.test-body').first().evaluate(body=>body.style.minHeight='130px');
        if (width===390||width===1440) {
          await page.screenshot({path:path.join(output,`${diag?'diagnostics':'status'}-${width}-${de?'de':'en'}.png`),fullPage:true});
        }
        await page.close();
      }
    }
    console.log('PASS: Status/diagnostics header-only stripes, six module colors, DE/EN, 320-1440px, expanded cards and unaffected controls/layout');
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
