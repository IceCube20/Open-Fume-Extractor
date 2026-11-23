const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebStatus.inc.h'), 'utf8');
const script = source.split('<script>')[1].split('</script>')[0];
new vm.Script(script);
const save = script.split('\n').find(line => line.startsWith('async function saveConnectionWarnings('));
const render = script.split('\n').find(line => line.startsWith("let rows='';(d.modules"));
assert(save && render);
const messages = [], writes = [];
const context = {
  URLSearchParams, connectionWarningSaving: false, load() {}, setTimeout() {},
  alert: text => messages.push(text), uiLang: 'de',
  moduleRoleText: () => '', kb: n => n, ups: n => n, commText: () => 'RS485 OK',
  commClass: () => 'on', commTitle: () => '', hx: n => n, t: n => n,
  moduleListType: m => m.name,
  fetch: async (url, request) => { writes.push({url, body: request.body}); return {ok: true}; }
};
vm.createContext(context);
vm.runInContext(save, context);

(async () => {
  const element = {checked: false, disabled: false, dataset: {uid: '0000ECF7384AEC24'}};
  await context.saveConnectionWarnings(element);
  assert.equal(writes[0].url, '/module/connection_warnings');
  assert.equal(writes[0].body.get('uid'), element.dataset.uid);
  assert.equal(writes[0].body.get('enabled'), '0');
  assert(!element.disabled && !context.connectionWarningSaving && !element.checked);
  element.checked = true;
  await context.saveConnectionWarnings(element);
  assert.equal(writes[1].body.get('enabled'), '1');
  context.fetch = async () => ({ok: false, text: async () => 'save failed'});
  element.checked = false;
  await context.saveConnectionWarnings(element);
  assert(element.checked && !element.disabled && !context.connectionWarningSaving);
  assert.equal(messages.pop(), 'save failed');
  context.fetch = async () => {throw new Error('offline');};
  element.checked = false;
  await context.saveConnectionWarnings(element);
  assert(element.checked && !element.disabled && !context.connectionWarningSaving);
  assert.equal(messages.pop(), 'offline');
  context.connectionWarningSaving = true;
  const before = writes.length;
  await context.saveConnectionWarnings(element);
  assert.equal(writes.length, before);
  context.connectionWarningSaving = false;
  context.d = {modules: [
    {addr: 16, uid: '0000ECF7384AEC24', name: 'JBC', online: true, connection_warnings_supported: true, connection_warnings_enabled: true},
    {addr: 48, uid: '0000ACFFE1A0ED30', name: 'Weller', online: true, connection_warnings_supported: true, connection_warnings_enabled: false},
    {addr: 32, uid: '0000000000000001', name: 'Fan/IO', online: true, connection_warnings_supported: false},
    {addr: 80, uid: '0000000000000002', name: 'Universal', online: true, connection_warnings_supported: true, connection_warnings_enabled: false},
    {addr: 96, uid: '0000000000000003', name: 'Modbus', online: true, connection_warnings_supported: true, connection_warnings_enabled: true}
  ]};
  for (const lang of ['de', 'en']) {
    context.uiLang = lang;
    const rows = vm.runInContext(`(()=>{${render}\nreturn rows;})()`, context);
    assert.equal((rows.match(/type="checkbox"/g) || []).length, 4);
    assert.equal((rows.match(/checked onchange/g) || []).length, 2);
    assert(rows.includes(lang === 'de' ? 'Verbindungswarnungen anzeigen' : 'Show connection warnings'));
    for (const row of rows.matchAll(/<tr>(.*?)<\/tr>/g)) {
      const cells = [...row[1].matchAll(/<td\b[^>]*>(.*?)<\/td>/g)].map(match => match[1]);
      assert.equal(cells.length, 15);
      assert(!cells[13].includes('<button'));
      assert(!cells[14].includes('checkbox'));
      assert(cells[14].includes('rebootModule('));
    }
  }
  console.log('PASS: real status JS and module-warning checkboxes: enabled/muted, UID, save, retry, rollback, busy and both languages');
  if (process.argv.includes('--layout')) {
    const {chromium} = require('playwright');
    const typographyHeader = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebTypography.h'), 'utf8');
    const typography = JSON.parse(typographyHeader.match(/#define OFE_WEB_TYPOGRAPHY_STYLE (".*")/)[1]);
    const first = source.slice(source.indexOf('static const char html1[]'), source.indexOf('static const char html2[]'));
    const html = [...first.matchAll(/R"HTML\(([\s\S]*?)\)HTML"|OFE_WEB_TYPOGRAPHY_STYLE/g)]
      .map(match => match[1] === undefined ? typography : match[1]).join('');
    const styles = [...html.matchAll(/<style>([\s\S]*?)<\/style>/g)].map(match => `<style>${match[1]}</style>`).join('');
    const heading = source.match(/<section class="row table-wrap"><table><thead>(.*?)<\/thead>/)[1];
    const rows = vm.runInContext(`(()=>{${render}\nreturn rows;})()`, context);
    const out = path.join(__dirname, 'build-checks/display-module-cards');
    const browser = await chromium.launch({executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true});
    try {
      for (const width of [1440, 390]) {
        const page = await browser.newPage({viewport: {width, height: 750}});
        await page.setContent(`${styles}<main><section class="row table-wrap"><table><thead>${heading}</thead><tbody>${rows}</tbody></table></section></main>`);
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
        const boxes = await page.locator('tbody tr').first().evaluate(row => {
          const toggle = row.cells[13].querySelector('input').getBoundingClientRect();
          const button = row.cells[14].querySelector('button').getBoundingClientRect();
          return {toggleY: toggle.y + toggle.height / 2, buttonY: button.y + button.height / 2, toggleRight: toggle.right, buttonLeft: button.left};
        });
        assert(Math.abs(boxes.toggleY - boxes.buttonY) < 4);
        assert(boxes.toggleRight < boxes.buttonLeft);
        await page.locator('.table-wrap').evaluate(el => {el.scrollLeft = el.scrollWidth;});
        await page.screenshot({path: path.join(out, `connection-monitoring-table-${width}.png`)});
        await page.close();
      }
    } finally { await browser.close(); }
    console.log('PASS: real table CSS at 1440/390 px: separate aligned monitoring and reboot columns; page stays within viewport');
  }
})().catch(error => {console.error(error); process.exitCode = 1;});
