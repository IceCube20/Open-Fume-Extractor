// Execute the firmware's device/FAE serializers and status markup in a browser.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const {chromium} = require('playwright');
const root = path.join(__dirname, '../../OpenFumeExtractorMaster/src');
const mqtt = fs.readFileSync(path.join(root, 'MasterMqtt.inc.h'), 'utf8');
const status = fs.readFileSync(path.join(root, 'WebStatus.inc.h'), 'utf8');
const typography = JSON.parse(fs.readFileSync(path.join(root, 'WebTypography.h'), 'utf8')
  .match(/#define OFE_WEB_TYPOGRAPHY_STYLE (".*")/)[1]);
const uidHex = value => BigInt(value).toString(16).toUpperCase().padStart(16, '0');
const translate = source => source
  .replace(/F\(("(?:[^"\\]|\\.)*")\)/g, '$1')
  .replace(/\bString (\w+)\s*=/g, 'let $1 =')
  .replace(/const bool /g, 'const ')
  .replace(/\.c_str\(\)/g, '')
  .replace(/\.length\(\)/g, '.length')
  .replace('station_device_id.trim();', 'station_device_id = station_device_id.trim();')
  .replace('String(m.addr, HEX)', 'm.addr.toString(16)')
  .replace(/char station_address\[6\];\s*snprintf\([^;]*;/, 'let station_address = " 0x" + m.station_addr.toString(16).toUpperCase().padStart(2, "0");')
  .replace('hxAddr.toUpperCase();', 'hxAddr = hxAddr.toUpperCase();');
const context = vm.createContext({
  master_device_id: 'existing-master-id', MASTER_FW_VERSION: '2.0.34RC-Developer',
  ESP: {getEfuseMac: () => 0x384AEC24n}, uid_hex: uidHex,
  json_escape: value => JSON.stringify(value).slice(1, -1),
  mqtt_config_url: () => 'http://ofe.test/', mqtt_fw_string: () => '1.1.70RC',
  module_display_name: m => m.alias, module_type_name_for: () => 'Fan/IO Pro',
  FAST_FLAG_CONNECTED: 1, station_type_name: () => 'DDE',
  bytes_ascii: () => 'station-id',
});
for (const name of ['mqtt_device_json', 'mqtt_module_device_json']) {
  const source = mqtt.match(new RegExp(`static String ${name}\\(.*?\\n}`, 's'))[0];
  vm.runInContext(translate(source.replace(/^static String .*?\{/, `function ${name}(${name.includes('module') ? 'm' : ''}) {`)), context);
}
const master = JSON.parse(context.mqtt_device_json());
assert.equal(master.sn, '00000000384AEC24');
assert.equal(master.ids[0], 'existing-master-id');
for (const addr of [0x10, 0x2A, 0x40, 0x60]) {
  const module = JSON.parse(context.mqtt_module_device_json({addr, uid: 0x2000000012345678n, alias: 'Fan "Test"'}));
  assert.equal(module.sn, '2000000012345678');
  assert.equal(module.name, `0x${addr.toString(16).toUpperCase()} Fan "Test"`);
  assert.equal(module.ids[0], 'existing-master-id_mod_' + addr.toString(16).toUpperCase());
}
assert.equal(JSON.parse(context.mqtt_module_device_json({addr: 32, uid: 0n, alias: 'Unknown'})).sn, undefined);

const start = mqtt.indexOf('    const bool linked = m.station_addr &&');
const end = mqtt.indexOf('\n  }', start);
const fae = translate(mqtt.slice(start, end)).replace('String(m.station_addr, HEX)', 'm.station_addr.toString(16)');
const webStand = status.split(/\r?\n/).find(line => line.startsWith('function jbcFaeStandActive('));
vm.runInContext(webStand, context);
for (const connected of [false, true]) for (const address of [0, 0x12]) for (const work of [0, 1]) {
  context.json = '{"probe":0';
  context.m = {online: true, station_addr: address, jbc_link_flags: connected ? 1 : 0,
    jbc_addr: 16, jbc_work_mask: work, jbc_stand_mask: 1,
    jbc_filter_life: 1, jbc_filter_sat: 2, jbc_stat_error: 0, jbc_device_id: [], jbc_device_id_len: 0};
  vm.runInContext('{' + fae + '}', context);
  const state = JSON.parse(context.json + '}');
  assert.equal(state.stand_active, context.jbcFaeStandActive(context.m));
  assert.equal(state.work_active, !!(connected && address && work));
}

const first = status.slice(status.indexOf('static const char html1[]'), status.indexOf('static const char html2[]'));
const html = [...first.matchAll(/R"HTML\(([\s\S]*?)\)HTML"|OFE_WEB_TYPOGRAPHY_STYLE/g)]
  .map(match => match[1] === undefined ? typography : match[1]).join('');
const styles = [...html.matchAll(/<style>([\s\S]*?)<\/style>/g)].map(match => '<style>' + match[1] + '</style>').join('');
const markupStart = status.indexOf('<div class="overview-group"><div class="section-title master-section-title">');
const markupEnd = status.indexOf('<div class="overview-group"><div class="section-title" data-i18n="extraction_status">', markupStart);
const markup = status.slice(markupStart, markupEnd);
const i18nStart = status.indexOf('const I18N=');
const i18nEnd = status.indexOf("let uiLang='de';", i18nStart);
assert(i18nStart >= 0 && i18nEnd > i18nStart);
const language = status.slice(i18nStart, i18nEnd);
assert(!status.includes("serial_number:'UID'"));
assert(!status.includes('data-i18n="serial_number">UID<'));
assert(!status.includes('OFE Modul UID'));
const update = status.split(/\r?\n/).find(line => line.startsWith("set('master_fw',d.master_fw"));

(async () => {
  const browser = await chromium.launch({headless: true,
    channel: process.env.OFE_TEST_BROWSER_CHANNEL || (process.platform === 'win32' ? 'msedge' : undefined)});
  const screenshots = path.join(os.tmpdir(), 'ofe-ha-identity');
  fs.mkdirSync(screenshots, {recursive: true});
  try {
    const page = await browser.newPage();
    for (const width of [1280, 390, 320]) for (const lang of ['de', 'en']) {
      await page.setViewportSize({width, height: 900});
      await page.setContent('<!doctype html><head>' + styles + '</head><body><main>' + markup + '</main></body>');
      await page.evaluate(({language, update, lang}) => {
        new Function('lang', language + ';document.querySelectorAll("[data-i18n]").forEach(el=>{el.textContent=I18N[lang][el.dataset.i18n]||el.textContent});')(lang);
        const set = (id, text) => { const el = document.getElementById(id); if (el) el.textContent = text; };
        const render = new Function('set', 'd', update);
        render(set, {master_fw: '2.0.34RC-Developer', master_uid: '00000000384AEC24', master_ip: '192.168.20.12'});
      }, {language, update, lang});
      const result = await page.locator('#master_serial_number').evaluate(el => ({
        text: el.textContent, label: el.previousElementSibling.textContent,
        overflow: el.scrollWidth > el.clientWidth || el.scrollHeight > el.clientHeight,
      }));
      assert.equal(result.text, master.sn);
      assert.equal(result.label, lang === 'de' ? 'Seriennummer' : 'Serial number');
      assert.equal(result.overflow, false);
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
      await page.screenshot({path: path.join(screenshots, `${width}-${lang}.png`), fullPage: true});
      await page.evaluate(update => new Function('set', 'd', update)((id, text) => {
        const el = document.getElementById(id); if (el) el.textContent = text;
      }, {}), update);
      assert.equal(await page.locator('#master_serial_number').textContent(), '-');
    }
    console.log('PASS: MQTT device serial/IDs/escaping; FAE Stand web/MQTT parity; DE/EN master serial at 1280/390/320px');
    console.log('Screenshots: ' + screenshots);
  } finally { await browser.close(); }
})().catch(error => {console.error(error);process.exitCode = 1;});
