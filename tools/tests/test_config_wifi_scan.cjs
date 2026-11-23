const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.join(__dirname, '../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const source = fs.readFileSync(path.join(root, 'WebConfig.inc.h'), 'utf8');
const server = fs.readFileSync(path.join(root, 'WebServer.inc.h'), 'utf8');
const shell = fs.readFileSync(path.join(root, 'WebShell.inc.h'), 'utf8');
const status = fs.readFileSync(path.join(root, 'WebStatus.inc.h'), 'utf8');
const icons = fs.readFileSync(path.join(root, 'WebTransportIcons.h'), 'utf8');
const logoArray=fs.readFileSync(path.join(root,'WebLogo.h'),'utf8').match(/OFE_LOGO_PNG\[\] PROGMEM\s*=\s*\{([\s\S]*?)\};/)[1];
const logo=Buffer.from(Array.from(logoArray.matchAll(/0x([\da-f]{2})/gi),m=>parseInt(m[1],16)));
const script = source.match(/R"JS\(([\s\S]*?)\)JS"/)[1];
assert(!source.includes('WiFi.scanNetworks(false'));
assert(source.includes('WiFi.scanNetworks(true, true)'));
assert(source.includes('web.method() == HTTP_POST && n != WIFI_SCAN_RUNNING'));
assert(source.includes('json_escape(ssid.c_str())'));
assert(source.includes('web_append_wifi_scan_controls(html, n < 0 && !web_password_change_required)'));
for (const method of ['GET', 'POST']) {
  assert(server.includes(`web.on("/config/wifi-scan", HTTP_${method}, [](){ if (web_require_config_auth()) web_handle_wifi_scan(); });`));
}
for (const page of [shell, status]) {
  assert(page.includes('Netzwerk/System') && page.includes('Network/System'));
  assert(!page.includes('Netzwerk Setup') && !page.includes('Network Setup'));
}

function fixture(results, selected = 'Saved WiFi', german = true) {
  const requests = [], nodes = {
    wifi_scan_button: {disabled: false}, wifi_scan_status: {textContent: ''},
    wifi_networks: {value: selected, options: [], replaceChildren(...options) {this.options = options; this.value = options[0]?.value || '';}}
  };
  const context = vm.createContext({
    configWifiScan: {csrf: 'test-csrf', german}, document: {getElementById: id => nodes[id]},
    Option: function(text, value) {this.text = text; this.value = value;},
    setTimeout: callback => callback(), AbortSignal: {timeout: ms => ({milliseconds:ms})},
    fetch: async (url, options) => {
      requests.push({url, ...options});
      const result = results.shift();
      if (result instanceof Error) throw result;
      assert(result, 'unexpected polling request');
      return {ok: !result.http||result.http===200, status:result.http||200, json: async () => result};
    }
  });
  vm.runInContext(script, context);
  return {context, nodes, requests};
}

async function run() {
  let f = fixture([{status:'scanning'}, {status:'complete', networks:[
    {ssid:'New WiFi', rssi:-40}, {ssid:'New WiFi', rssi:-60}, {ssid:'<unsafe>', rssi:-80}
  ]}]);
  await f.context.scanConfigWifi();
  assert.deepEqual(f.requests.map(r => r.method), ['POST', 'GET']);
  assert.equal(f.requests[0].headers['X-CSRF-Token'], 'test-csrf');
  assert.equal(f.requests[1].headers['X-CSRF-Token'], undefined);
  assert.equal(f.requests[0].signal.milliseconds,20000);
  assert.equal(f.requests[1].signal.milliseconds,5000);
  assert.equal(f.nodes.wifi_networks.value, 'Saved WiFi');
  assert.deepEqual(f.nodes.wifi_networks.options.map(o => o.value), ['Saved WiFi', 'New WiFi', '<unsafe>']);
  assert.equal(f.nodes.wifi_scan_status.textContent, 'Suche abgeschlossen');
  assert.equal(f.nodes.wifi_scan_button.disabled, false);

  f = fixture([{status:'complete', networks:[]}], '', false);
  await f.context.scanConfigWifi();
  assert.equal(f.nodes.wifi_networks.value, '');
  assert.equal(f.nodes.wifi_networks.options[0].value, '');
  assert(f.nodes.wifi_networks.options[0].text.includes('enter SSID manually'));

  for (const failure of [new Error('network'), {status:'failed'}]) {
    f = fixture([failure, {status:'complete', networks:[{ssid:'Saved WiFi', rssi:-45}]}]);
    await f.context.scanConfigWifi();
    assert.equal(f.nodes.wifi_networks.value, 'Saved WiFi');
    assert.equal(f.nodes.wifi_scan_button.disabled, false);
    assert(f.nodes.wifi_scan_status.textContent.includes('fehlgeschlagen'));
    await f.context.scanConfigWifi();
    assert.equal(f.nodes.wifi_scan_status.textContent, 'Suche abgeschlossen');
  }
  f=fixture([{http:409},{status:'scanning'},{http:409},{status:'complete',networks:[]}]);
  await f.context.scanConfigWifi();
  assert.deepEqual(f.requests.map(r=>r.method),['POST','POST','GET','GET']);
  assert(f.nodes.wifi_scan_status.textContent.includes('abgeschlossen'));
  f=fixture([{status:'failed',code:-2}]);await f.context.scanConfigWifi();
  assert(f.nodes.wifi_scan_status.textContent.includes('scan -2'));
  f=fixture([{http:401}]);await f.context.scanConfigWifi();
  assert.equal(f.requests.length,1);assert(f.nodes.wifi_scan_status.textContent.includes('HTTP 401'));
  f = fixture([]); f.nodes.wifi_scan_button.disabled = true;
  await f.context.scanConfigWifi(); assert.equal(f.requests.length, 0);
  console.log('Config WiFi: nonblocking render, authenticated scan, POST/GET polling, SSID preservation, deduplication, empty results, retry and navigation passed');
}

async function browserCheck() {
  const {chromium} = require('playwright');
  const browser = await chromium.launch({headless:true, executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try {
    const iconStyle=icons.match(/R"ICONS\(<style>([\s\S]*?)<\/style>/)[1];
    const css = shell.match(/<style>([\s\S]*?)<\/style>/)[1]+iconStyle;
    const iconHtml=icons.match(/#define OFE_TRANSPORT_HEADER_HTML R"ICON\(([\s\S]*?)\)ICON"/)[1];
    const header = status.slice(status.indexOf('<header>'), status.indexOf('<main>')).replace(')HTML" OFE_TRANSPORT_HEADER_HTML R"HTML(',iconHtml);
    assert(!header.includes(')HTML"'),'C++ macro boundary leaked into the HTML fixture');
    const output = path.join(__dirname, 'build-checks/config-wifi-ui'); fs.mkdirSync(output, {recursive:true});
    for (const [label, width, height] of [['desktop',1440,900], ['mobile',390,844]]) {
      const page = await browser.newPage({viewport:{width,height}});
      await page.route('http://ofe.test/', route => route.fulfill({contentType:'text/html',body:'<html></html>'}));
      await page.route('http://ofe.test/logo.png',route=>route.fulfill({contentType:'image/png',body:logo}));
      await page.goto('http://ofe.test/');
      await page.setContent(`<style>${css}:root{--ofe-font-ui:Arial,sans-serif}</style>${header}<main><h1>Netzwerk/System</h1><form class="panel"><label>Gefundene Netzwerke</label><select id="wifi_networks"><option>Saved WiFi</option></select><div class="actions" style="margin-top:10px"><button id="wifi_scan_button" type="button" class="secondary" onclick="scanConfigWifi()">Netzwerke neu scannen</button><span id="wifi_scan_status" class="muted" role="status" aria-live="polite"></span></div><label>SSID manuell eingeben</label><input id="manual" value="Manual WiFi"><label>WLAN-Passwort</label><input type="password" value="example"></form></main>`);
      await page.locator('[data-i18n="wifi"]').evaluate(e => e.textContent = 'Netzwerk/System');
      await page.route('**/config/wifi-scan', route => route.fulfill({contentType:'application/json',body:JSON.stringify({status:'complete',networks:[{ssid:'Saved WiFi',rssi:-42},{ssid:'<img src=x onerror=alert(1)>',rssi:-60}]})}));
      await page.addScriptTag({content:`const configWifiScan={csrf:'test-csrf',german:true};${script}`});
      await page.locator('#wifi_scan_button').click();
      await page.waitForFunction(() => document.getElementById('wifi_scan_status').textContent === 'Suche abgeschlossen');
      assert.equal(await page.locator('#wifi_networks').inputValue(), 'Saved WiFi');
      assert.equal(await page.locator('#manual').inputValue(), 'Manual WiFi');
      assert.equal(await page.locator('#wifi_networks img').count(), 0);
      assert(await page.locator('.brand-logo').evaluate(e=>e.complete&&e.naturalWidth===96));
      await page.screenshot({path:path.join(output, `${label}.png`),fullPage:true});
      const overflow=await page.evaluate(()=>Array.from(document.querySelectorAll('body *')).filter(e=>e.getBoundingClientRect().right>innerWidth+1).map(e=>({tag:e.tagName,id:e.id,text:e.textContent.slice(0,80)})));
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${label}: horizontal overflow ${JSON.stringify(overflow.slice(0,8))}`);
      await page.close();
    }
    console.log('Config WiFi UI: desktop/mobile screenshots, navigation, preserved form fields and safe option text passed');
  } finally {await browser.close();}
}
run().then(() => process.argv.includes('--browser') ? browserCheck() : undefined).catch(error => {console.error(error);process.exitCode=1;});
