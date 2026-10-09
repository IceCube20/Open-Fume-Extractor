// Verify the firmware's actual CSS and Weller synchronizer in Chromium.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');
const {chromium} = require('playwright');
const root = path.join(__dirname, '../../OpenFumeExtractorMaster/src');
const source = fs.readFileSync(path.join(root, 'WebStatus.inc.h'), 'utf8');
const header = fs.readFileSync(path.join(root, 'WebTypography.h'), 'utf8');
const typography = JSON.parse(header.match(/#define OFE_WEB_TYPOGRAPHY_STYLE (".*")/)[1]);
const first = source.slice(source.indexOf('static const char html1[]'), source.indexOf('static const char html2[]'));
const html = [...first.matchAll(/R"HTML\(([\s\S]*?)\)HTML"|OFE_WEB_TYPOGRAPHY_STYLE/g)]
  .map(match => match[1] === undefined ? typography : match[1]).join('');
const styles = [...html.matchAll(/<style>([\s\S]*?)<\/style>/g)].map(match => '<style>' + match[1] + '</style>').join('');
assert.equal(html.match(/--ofe-font-ui:Arial/g).length, 1);
const sync = source.slice(source.indexOf('function syncWellerFilterCard('), source.indexOf('const updateDetailsFilterTextCore='));
const shell = fs.readFileSync(path.join(root, 'WebShell.inc.h'), 'utf8');
const shellStrings = shell.match(/html \+= F\(("(?:\\.|[^"\\])*") OFE_WEB_TYPOGRAPHY_STYLE ("(?:\\.|[^"\\])*")\);/);
assert(shellStrings, 'Shared typography must be included in the shell head');
const shellHtml = JSON.parse(shellStrings[1]) + typography + JSON.parse(shellStrings[2]);
const shellStyles = [...shellHtml.matchAll(/<style>([\s\S]*?)<\/style>/g)].map(match => '<style>' + match[1] + '</style>').join('');
for (const file of fs.readdirSync(root).filter(file => /^Web.*\.h$/.test(file) && file !== 'WebTypography.h')) {
  const text = fs.readFileSync(path.join(root, file), 'utf8');
  assert(!/Consolas|ui-monospace|SFMono-Regular|font-family:Arial|fontFamily/.test(text), file + ': unexpected separate font');
}

const markup = `<div id="details" class="grid">
<div class="tile module-card fan-card core-style" id="fan"><div class="module-head"><strong>Fan/IO Pro</strong></div>
<div class="jbu-facts"><div class="jbu-fact"><div class="k">Filterbetriebszeit</div><div class="v">1d 16h 30m</div></div></div>
<div class="filter-actions"><button class="secondary" onclick="fanioResetRuntime(32)">Filterbetriebszeit zurücksetzen</button></div></div>
<div class="tile module-card weller-card core-style"><div class="module-head"><strong>Weller Zero Smog</strong></div>
<div class="jbu-facts"><div class="jbu-fact"><div class="k">SW</div><div id="wver_48" class="v">V0.13</div></div></div>
<div class="jbu-facts"><div class="jbu-fact"><div class="k">Filter</div><div id="wfstat_48" class="v">OK</div></div>
<div class="jbu-fact"><div class="k">Intervall</div><div id="wfprog_48" class="v">6d 16h</div></div>
<div class="jbu-fact"><div class="k">Laufzeit</div><div id="wfrun_48" class="v">1d 16h</div></div></div>
<div class="filter-actions"><select><option>6 Tage 16 Stunden</option></select><button class="secondary" onclick="wellerReset(48)">Reset Filter</button></div></div></div>
<div class="module-card"><div class="module-address" data-font>0x20</div><div class="jbu-fact"><span class="mono" data-font>Serial ABCD0123</span></div></div>
<span class="mb-row-num" data-font>1</span><pre data-font>profile=Test</pre><code data-font>/state</code><textarea data-font>TEST</textarea>
<svg><text x="0" y="15" font-family="inherit" data-font>TON</text></svg>`;

(async () => {
  const browser = await chromium.launch({headless: true, channel: process.env.OFE_TEST_BROWSER_CHANNEL || (process.platform === 'win32' ? 'msedge' : undefined)});
  const page = await browser.newPage();
  const screenshots = path.join(os.tmpdir(), 'ofe-web-typography');
  fs.mkdirSync(screenshots, {recursive: true});
  try {
    for (const width of [1280, 390, 320]) {
      await page.setViewportSize({width, height: 900});
      for (const de of [true, false]) {
        await page.setContent('<!doctype html><html><head>' + styles + '</head><body><main>' + markup + '</main></body></html>');
        await page.evaluate(({sync, de}) => {
          window.uiLang = de ? 'de' : 'en';
          window.t = key => key === 'fan_reset_filter_runtime' ? (de ? 'Filterbetriebszeit zurücksetzen' : 'Reset filter operating time') : (de ? 'Filter-Wechselintervall' : 'Filter replacement interval');
          window.dhm = value => String(value);
          window.set = (id, text) => {document.getElementById(id).textContent = text;};
          window.syncWellerFilterCard = new Function(sync + ';return syncWellerFilterCard;')();
          document.querySelector('.fan-card .filter-actions button').textContent = t('fan_reset_filter_runtime');
          syncWellerFilterCard({addr: 48, online: true, weller_filter_runtime_minutes: 2430, weller_programmed_filter_minutes: 9600});
        }, {sync, de});
        const measure = () => page.evaluate(() => {
          const props = ['fontFamily', 'fontSize', 'fontWeight', 'lineHeight', 'minHeight', 'paddingTop', 'paddingBottom', 'paddingLeft', 'paddingRight'];
          const buttons = [...document.querySelectorAll('.filter-actions button')];
          return {styles: buttons.map(button => Object.fromEntries(props.map(key => [key, getComputedStyle(button)[key]]))),
            family: getComputedStyle(document.body).fontFamily,
            others: [...document.querySelectorAll('[data-font]')].map(el => getComputedStyle(el).fontFamily),
            overflow: buttons.some(button => button.scrollWidth > button.clientWidth || button.scrollHeight > button.clientHeight),
            width: buttons.map(button => button.getBoundingClientRect().width)};
        });
        const measured = await measure();
        assert.deepEqual(measured.styles[0], measured.styles[1], `reset styles: ${width}px, ${de ? 'DE' : 'EN'}`);
        assert.equal(measured.styles[0].fontFamily, measured.family);
        assert(measured.others.every(family => family === measured.family));
        assert.equal(measured.overflow, false);
        assert(Math.abs(measured.width[0] - measured.width[1]) < 1);
        await page.screenshot({path: path.join(screenshots, `${width}-${de ? 'de' : 'en'}.png`), fullPage: true});
        await page.evaluate(() => {document.getElementById('fan').remove();syncWellerFilterCard({addr: 48, online: true});});
        const alone = await measure();
        assert.deepEqual(alone.styles[0], measured.styles[1], 'Weller-only setup must not depend on Fan/IO presence');
      }
    }
    await page.setContent('<!doctype html><head>' + shellStyles + '</head><body><button>Neustart</button><textarea>PEM</textarea><pre>Trace</pre></body>');
    const families = await page.evaluate(() => [...document.querySelectorAll('body,button,textarea,pre')].map(el => getComputedStyle(el).fontFamily));
    assert(families.every(family => family === families[0]));
    console.log('PASS: one web font; identical filter reset styles with/without Fan/IO; DE/EN at 1280/390/320px; no button overflow');
    console.log('Screenshots: ' + screenshots);
  } finally {
    await browser.close();
  }
})().catch(error => {console.error(error);process.exitCode = 1;});
