const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const source = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/WebStatus.inc.h'), 'utf8');
const selector = { value: '0' };
const status = { title: '', closest: () => null };
const displayed = {};
const requests = [];
const context = {
  uiLang: 'de', wellerFilterPending: {}, wellerFilterHoldUntil: {},
  document: { activeElement: null, getElementById: id => id === 'wf_sel_48' ? selector : id === 'wfstat_48' ? status : null },
  t: key => key, set: (key, value) => { displayed[key] = value; }, dhm: value => String(value),
  fetch: async (...args) => { requests.push(args); return { ok: true }; },
  clearFieldDirty() {}, load() {}, setTimeout() {},
};
vm.createContext(context);
for (const name of ['wellerUiFilter', 'wellerFilterOptions', 'wellerFilterTouch', 'wellerFilter']) {
  const fn = source.match(new RegExp('^(?:async )?function ' + name + '\\([^\\n]*$', 'm'));
  assert(fn, name);
  vm.runInContext(fn[0], context);
}
const start = source.indexOf('function syncWellerFilterCard(m){');
const end = source.indexOf('const updateDetailsFilterTextCore', start);
assert(start >= 0 && end > start);
vm.runInContext(source.slice(start, end), context);

(async () => {
  for (const language of ['de', 'en']) {
    context.uiLang = language;
    const options = context.wellerFilterOptions(0);
    assert(options.includes(`<option value="0" selected>${language === 'de' ? 'Aus' : 'Off'}</option>`));
    const values = [...options.matchAll(/value="(\d+)"/g)].map(match => Number(match[1]));
    assert.deepEqual(values, [0, ...Array.from({length: 23}, (_, i) => (i + 1) * 60), ...Array.from({length: 6}, (_, i) => (i + 1) * 1440), 9600]);
  }
  for (const value of ['0', '60', '9600']) {
    selector.value = value;
    await context.wellerFilter(48);
    assert.equal(requests.at(-1)[0], '/weller/set?addr=48&filter_minutes=' + value);
    assert.equal(requests.at(-1)[1].method, 'POST');
    assert.equal(context.wellerFilterPending[48], Number(value));
  }
  for (const value of ['', '-1', 'bad', '1.5']) {
    selector.value = value;
    assert.equal(context.wellerFilterTouch(48), null);
    const before = requests.length;
    await context.wellerFilter(48);
    assert.equal(requests.length, before);
  }
  delete context.wellerFilterHoldUntil[48];
  for (const language of ['de', 'en']) {
    context.uiLang = language;
    const module = { addr: 48, online: true, weller_uart_age_sec: 65534, weller_filter_status: 10, weller_programmed_filter_minutes: 0, weller_filter_runtime_minutes: 150 };
    context.syncWellerFilterCard(module);
    assert.equal(displayed.wfprog_48, language === 'de' ? 'Aus' : 'Off');
    assert.equal(displayed.wfremain_48, language === 'de' ? 'Aus' : 'Off');
    assert.equal(status.title, language === 'de' ? 'Zuletzt empfangener Filterzustand (Weller getrennt)' : 'Last received filter status (Weller disconnected)');
    module.weller_uart_age_sec = 0;
    module.weller_programmed_filter_minutes = 480;
    context.syncWellerFilterCard(module);
    assert.equal(displayed.wfprog_48, '480');
    assert.equal(displayed.wfremain_48, '330');
    assert.equal(status.title, '');
  }
  console.log('PASS: production Weller web controls, zero-value POST, positive presets, invalid inputs and DE/EN cached filter display');
})().catch(error => { console.error(error); process.exitCode = 1; });
