const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebStatus.inc.h'), 'utf8');
const mqttSource = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterMqtt.inc.h'), 'utf8');
const config = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterBuildConfig.h'), 'utf8');
const shellSource = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebShell.inc.h'), 'utf8');
const runtimeHelper = 'function runtimeProfileHtml(' + shellSource.split('function runtimeProfileHtml(')[1].split('</script>')[0];
const helper = source.slice(source.indexOf('function updateMqttPerf('), source.indexOf('\nfunction ', source.indexOf('function updateMqttPerf(') + 1));
const httpHelper = source.slice(source.indexOf('function updateHttpPerf('), source.indexOf('\nfunction ', source.indexOf('function updateHttpPerf(') + 1));
assert(helper.startsWith('function updateMqttPerf('));
const values = {};
const nodes = Object.fromEntries(['mqtt_perf','mqtt_perf_build','mqtt_perf_master','mqtt_perf_modules','mqtt_perf_publish','mqtt_perf_socket','mqtt_perf_loop','http_perf','http_perf_status','http_perf_ha','http_perf_diagnostics'].map(id => [id, {}]));
const context = vm.createContext({uiLang:'de', set:(id, text) => {values[id] = text;}, document:{getElementById:id => nodes[id]}});
vm.runInContext(helper, context);
vm.runInContext(httpHelper, context);
const sample = {enabled:true, window_ms:5000, payload_bytes:10240, publish_failed:2,
  build:{calls:10, avg_us:300, max_us:900, total_us:3000},
  publish:{calls:20, avg_us:1200, max_us:20000, total_us:24000},
  client_loop:{calls:300, avg_us:10, max_us:300, total_us:3000}};
context.updateMqttPerf(sample, true);
assert.equal(values.mqtt_perf,'4.0 msg/s · 2.0 KiB/s · Fehlgeschlagen: 2');
assert.equal(values.mqtt_perf_build,'Live-Datenaufbereitung: 0.30 / 0.90 ms · 0.6 ms/s');
assert.equal(values.mqtt_perf_publish,'Publish: 1.20 / 20.00 ms · 4.8 ms/s');
assert(nodes.mqtt_perf.title.includes('nicht den Empfang'));
context.updateMqttPerf({enabled:false}, true);
assert.equal(values.mqtt_perf,'Deaktiviert');
assert.equal(values.mqtt_perf_build,'');
context.updateMqttPerf({enabled:true, window_ms:0}, true);
assert.equal(values.mqtt_perf,'Messung läuft');
context.updateMqttPerf(undefined, true);
assert.equal(values.mqtt_perf,'-');
context.updateMqttPerf(sample, false);
assert.equal(values.mqtt_perf_publish,'');
context.uiLang='en';
context.updateMqttPerf(sample,true);
assert(values.mqtt_perf.includes('Failed: 2'));
assert(values.mqtt_perf_build.includes('Live data preparation'));
assert(!Object.values(values).some(value => /NaN|Infinity/.test(value)));
assert.equal((mqttSource.match(/mqtt_client\.publish\(/g)||[]).length,2);
assert.equal((mqttSource.match(/mqtt_client\.loop\(/g)||[]).length,1);
assert.equal((mqttSource.match(/MqttPerfScope prepare\(MqttPerfPhase::(?:MasterBuild|ModuleBuild)\)/g)||[]).length,2);
assert.equal((mqttSource.match(/prepare\.finish\(\);\s*\n\s*(?:if\()?mqtt_publish_measured/g)||[]).length,2);
for(const [name,interval] of [['SERVICE',10],['STATE_PUBLISH',350],['MODULE_PUBLISH_GAP',25]])
  assert(config.includes(`#define MASTER_MQTT_${name}_MS ${interval}UL`));
assert(source.includes('if (developer_mode_enabled) {\n    const MqttPerfSnapshot perf'));
const httpSample={window_ms:5000,payload_bytes:51200,status_build:{calls:10,avg_us:20000,max_us:30000,total_us:200000},status_send:{calls:10,avg_us:1000,max_us:3000,total_us:10000},ha_build:{calls:5,avg_us:25000,max_us:30000,total_us:125000}};
context.uiLang='de';context.updateHttpPerf(httpSample,true);
assert.equal(values.http_perf,'10.0 KiB/s JSON · Aufbereitung / Versand');
assert(values.http_perf_status.includes('Status (2.0/s): 20.00 / 30.00 ms · 40.0 ms/s | 1.00 / 3.00 ms · 2.0 ms/s'));
assert(values.http_perf_ha.startsWith('Native HA (1.0/s)'));
assert(nodes.http_perf_status.title.includes('keine CPU-Prozente'));
context.updateHttpPerf(undefined,false);assert.equal(values.http_perf_status,'');
context.uiLang='en';context.updateHttpPerf(httpSample,true);assert(values.http_perf.includes('Build / send'));
assert(!Object.values(values).some(value=>/NaN|Infinity/.test(value)));
console.log('MQTT UI/source: rate units, phase units, dev/off clearing, language, wrapped sends and unchanged intervals passed');

async function browserCheck() {
  const {chromium} = require('playwright');
  const browser = await chromium.launch({headless:true, executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try {
    const css = source.match(/<style>([\s\S]*?)<\/style>/)[1];
    const html = source.slice(source.indexOf('<div class="stat-strip master-stats">'), source.indexOf('<div class="overview-group"><div class="section-title" data-i18n="extraction_status">'));
    assert(html.includes('id="mqtt_perf"'));
    const output = path.join(__dirname,'build-checks/mqtt-performance-ui');
    fs.mkdirSync(output,{recursive:true});
    for(const [label,width,height] of [['desktop',1440,900],['tablet',768,1024],['mobile',390,844],['small-mobile',320,844]]) {
      const page = await browser.newPage({viewport:{width,height}});
      await page.setContent(`<style>${css}:root{--ofe-font-ui:Arial,sans-serif}</style><body class="dev-mode"><main><div class="overview-group"><div class="section-title">Master-System</div>${html}</main></body>`);
      await page.addScriptTag({content:`let uiLang='de';function set(id,text){const e=document.getElementById(id);if(e)e.textContent=text;}\n${helper}\n${httpHelper}\n${runtimeHelper}`});
      await page.evaluate(({sample,httpSample})=>{
        updateMqttPerf(sample,true);updateHttpPerf(httpSample,true);
        document.getElementById('runtime_profile').innerHTML=runtimeProfileHtml({active:true,window_ms:1000,age_ms:10,
          tasks:[{id:'loop',core:1,cpu_pct:34},{id:'http',core:1,cpu_pct:21},{id:'mqtt',core:1,cpu_pct:8},
            {id:'heap',core:1,cpu_pct:1},{id:'mqtt_send',core:0,cpu_pct:9}]},true);
      },{sample,httpSample});
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${label}: page overflow`);
      const overflow = await page.locator('.mqtt-perf-cell:not(.http-perf-cell)').evaluate(e => e.scrollWidth > e.clientWidth);
      assert(!overflow, `${label}: MQTT cell overflow`);
      assert(!await page.locator('.http-perf-cell').evaluate(e=>e.scrollWidth>e.clientWidth),`${label}: HTTP cell overflow`);
      assert(!await page.locator('.runtime-profile-cell').evaluate(e=>e.scrollWidth>e.clientWidth),`${label}: runtime cell overflow`);
      assert((await page.locator('#runtime_profile').innerText()).includes('MQTT-Versand (Core 0): 9%'));
      const intersect = await page.locator('.mqtt-perf-cell:not(.http-perf-cell)').evaluate(e => {
        const own=e.getBoundingClientRect();
        return [...e.parentElement.children].some(other => {
          if(other===e||!other.getClientRects().length)return false;
          const r=other.getBoundingClientRect();
          return own.left<r.right&&own.right>r.left&&own.top<r.bottom&&own.bottom>r.top;
        });
      });
      assert(!intersect,`${label}: overlapping cells`);
      await page.screenshot({path:path.join(output,`${label}.png`),fullPage:true});
      await page.evaluate(() => {document.body.classList.remove('dev-mode');updateMqttPerf(undefined,false);updateHttpPerf(undefined,false);});
      assert.equal(await page.locator('.mqtt-perf-cell:not(.http-perf-cell)').isVisible(),false);
      assert.equal(await page.locator('.http-perf-cell').isVisible(),false);
      assert.equal(await page.locator('.runtime-profile-cell').isVisible(),false);
      await page.close();
    }
    console.log('MQTT status strip: desktop, tablet and mobile layout/screenshots passed; dev-only visibility passed');
  } finally {await browser.close();}
}
if(process.argv.includes('--browser'))browserCheck().catch(error => {console.error(error);process.exitCode=1;});
