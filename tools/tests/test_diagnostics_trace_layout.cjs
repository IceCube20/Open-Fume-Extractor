const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const {chromium} = require('playwright');
const root = path.join(__dirname, '../../OpenFumeExtractorMaster/src');
const source = fs.readFileSync(path.join(root, 'WebDiagnostics.inc.h'), 'utf8');
const shell = fs.readFileSync(path.join(root, 'WebShell.inc.h'), 'utf8');
const typography = fs.readFileSync(path.join(root, 'WebTypography.h'), 'utf8');
const css = shell.match(/<style>([\s\S]*?)<\/style>/)[1] +
  source.match(/R"CSS\(<style>([\s\S]*?)<\/style>\)CSS"/)[1] +
  typography.match(/<style>([\s\S]*?)<\/style>/)[1];

(async () => {
  const browser = await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try {
    const output = path.join(__dirname, 'build-checks/diagnostics-trace-layout');
    fs.mkdirSync(output, {recursive:true});
    for (const width of [320,360,390,430,760,1024,1440]) {
      for (const de of [true,false]) {
        const page = await browser.newPage({viewport:{width,height:850}});
        const panels = ['ofe','local'].map(kind => {
          const title = kind === 'ofe' ? 'OFE Bus Trace' : de ? 'Gerätebus Trace' : 'Device bus trace';
          const description = kind === 'ofe'
            ? de ? 'Requests und Responses als zusammenhängende Transaktionen oder vollständig als Roh-Trace.' : 'Requests and responses as combined transactions or as the complete raw trace.'
            : de ? 'JBC-, Weller-, Universal- und Modbus-Frames werden protokoll- bzw. descriptorbasiert zu Transaktionen zusammengeführt. Rohframes bleiben jederzeit verfügbar.' : 'JBC, Weller, Universal and Modbus frames are combined into protocol- or descriptor-aware transactions. Raw frames remain available at all times.';
          return `<section class="panel diag-log-panel"><div class="diag-trace-head"><div><div class="diag-title-row"><h2>${title}</h2><span class="diag-badge">${kind.toUpperCase()}</span></div><p class="muted">${description}</p></div><div class="diag-trace-switch"><button class="active">${de?'Transaktionen':'Transactions'}</button><button>${kind==='ofe'?'Raw':de?'Rohframes':'Raw frames'}</button></div></div></section>`;
        }).join('');
        await page.setContent(`<style>${css}</style><main>${panels}</main>`);
        for (const active of [0,1]) {
          await page.locator('.diag-trace-switch').evaluateAll((switches,index) => switches.forEach(s => {
            [...s.children].forEach((button,i) => button.classList.toggle('active',i===index));
          }), active);
          const issues = await page.locator('.diag-trace-switch').evaluateAll(switches => switches.flatMap(s => {
            const box=s.getBoundingClientRect(),panel=s.closest('.panel').getBoundingClientRect(),fail=[];
            if(box.left<panel.left+17||box.right>panel.right-17)fail.push('switch outside panel padding');
            for(const button of s.children){
              const b=button.getBoundingClientRect(),range=document.createRange();range.selectNodeContents(button);
              const text=range.getBoundingClientRect();
              if(b.left<box.left||b.right>box.right)fail.push('button outside switch');
              if(text.left<b.left||text.right>b.right)fail.push('label outside button');
            }
            return fail;
          }));
          assert.deepEqual(issues, [], `${width}/${de?'de':'en'}/${active}`);
          assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'page overflow');
        }
        await page.screenshot({path:path.join(output,`${width}-${de?'de':'en'}.png`),fullPage:true});
        await page.close();
      }
    }
    console.log('PASS: OFE/local trace switches stay inside their panels in DE/EN, both selections, 320-1440 px');
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
