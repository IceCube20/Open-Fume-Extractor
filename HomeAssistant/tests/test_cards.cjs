/* Browser contract tests with native-card doubles, not a running HA frontend. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {chromium} = require("playwright");
const root = path.resolve(__dirname, "../custom_components/open_fume_extractor");
const artifacts = path.resolve(__dirname, "artifacts");
const master = "0000000012345678", moduleUid = "2000000012345678";
const row = (entity_id, uid, key, group, name, state = "on", extra = {}) => ({entity_id, state, attributes: {
  ofe_master_uid: master, ofe_uid: uid, ofe_key: key, ofe_group: group, ofe_name: name,
  ofe_device_name: uid === master ? "0x01 Open Fume Extractor Master" : "0x20 Fan/IO Pro Testbank", ofe_active: true,
  ofe_device_type: uid === master ? 0 : 3, ofe_device_model: uid === master ? "Open Fume Extractor Master" : "Fan/IO Pro",
  ofe_transport: uid === master ? "wifi" : "rs485", ...extra,
}});
const states = Object.fromEntries([
  row("sensor.ofe_requested", master, "output_power", "status", "Requested power", "55 %"),
  row("select.ofe_level", master, "level", "controls", "Suction level", "High"),
  row("number.ofe_power", master, "custom_power", "controls", "Custom power", "55"),
  row("switch.ofe_continuous", master, "continuous", "controls", "Continuous"),
  row("sensor.ofe_heap", master, "heap_free", "diagnostics", "Free heap", "176 KiB"),
  row("sensor.ofe_led_ofe", master, "led_ofe", "diagnostics", "OFE LED", "Energiesparen",
    {ofe_led_event: 1, ofe_led_color: "#9600ff", ofe_led_effect: "purplewhite", ofe_led_period_ms: 3200, ofe_led_enabled: true}),
  row("sensor.ofe_led_evt", master, "led_evt", "diagnostics", "EVT LED", "Nachlauf",
    {ofe_led_event: 10, ofe_led_color: "#aa00ff", ofe_led_effect: "blink", ofe_led_period_ms: 1300, ofe_led_enabled: true}),
  row("binary_sensor.ofe_eco", master, "power_save_active", "status", "Power saving"),
  row("sensor.ofe_feedback", master, "feedback_rpm", "status", "Fan RPM", "1800", {unit_of_measurement: "rpm", icon: "mdi:fan"}),
  row("sensor.ofe_afterrun", master, "afterrun", "status", "Afterrun", "12s", {icon: "mdi:timer-outline"}),
  row("sensor.ofe_alarms", master, "alarm_count", "status", "Active alarms", "0", {icon: "mdi:bell-alert-outline", alarms: [], alarm_count: 0}),
  row("sensor.ofe_alarm_text", master, "alarm_text", "status", "Alarm Text", "OK", {icon: "mdi:bell-alert-outline", alarms: [], alarm_count: 0}),
  row("binary_sensor.ofe_module_online", moduleUid, "online", "diagnostics", "Connection"),
  row("sensor.ofe_module_led_ofe", moduleUid, "led_ofe", "diagnostics", "OFE LED", "Bus online",
    {ofe_led_event: 1, ofe_led_color: "#00ff00", ofe_led_effect: "greenwhite", ofe_led_period_ms: 3200, ofe_led_enabled: true}),
  row("sensor.ofe_module_led_evt", moduleUid, "led_evt", "diagnostics", "EVT LED", "Device online",
    {ofe_led_event: 6, ofe_led_color: "#ffffff", ofe_led_effect: "whitebreath", ofe_led_period_ms: 3200, ofe_led_enabled: true}),
  row("switch.ofe_output", moduleUid, "entity_20_out", "controls", "Extraction output"),
  row("number.ofe_fan_power", moduleUid, "entity_61_power", "controls", "Power", "55", {unit_of_measurement: "%", icon: "mdi:fan"}),
  row("sensor.ofe_fan_rpm", moduleUid, "entity_62_rpm", "status", "Fan RPM", "1800", {unit_of_measurement: "rpm", icon: "mdi:fan"}),
  row("sensor.ofe_filter_runtime", moduleUid, "filter_operating_time", "filter", "Filter operating time", "1h 16m", {icon: "mdi:timer-outline"}),
  row("sensor.ofe_filter", moduleUid, "filter_remaining", "filter", "Filter remaining time", "1d 7h 10m"),
  row("sensor.ofe_port1", moduleUid, "jbc_p1_temperature", "port_1", "Port 1 Temperature", "350 °C"),
  row("number.ofe_jbc_temp", moduleUid, "jbc_cfg_0", "port_1", "Port 1 Set temperature", "350"),
  row("switch.ofe_jbc_levels", moduleUid, "jbc_cfg_2", "port_1", "Port 1 Temperature levels enabled"),
  row("select.ofe_jbc_level", moduleUid, "jbc_cfg_3", "port_1", "Port 1 Temperature level", "Level 1"),
  row("select.ofe_filter_interval", moduleUid, "filter_interval", "filter", "Filter replacement interval", "6d 16h"),
].map(s => [s.entity_id, s]));
const html = `<!doctype html><html><head><meta charset="utf-8"><style>
  body{margin:0;padding:24px;background:#fafafa;color:#212121;font:14px Arial;--primary-text-color:#212121;
  --secondary-text-color:#666;--card-background-color:white;--divider-color:#ddd;--primary-color:#007d87}
  #host{max-width:1000px;margin:auto}#editor{max-width:450px;margin:24px auto}
</style></head><body><div id="host"></div><div id="editor"></div><script>
  class NativeCard extends HTMLElement {
    constructor(){super();this.attachShadow({mode:'open'})}
    setConfig(config){this.config=config;this.shadowRoot.innerHTML='<style>:host{display:block;background:white;border:1px solid #ddd;border-radius:8px;overflow:hidden}h2{font-size:20px;margin:20px}section{padding:12px 20px 4px;color:#666}.row{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:50px;padding:0 20px}input,select{max-width:45%;min-width:0;font:inherit}span{overflow-wrap:anywhere}</style>';let title=document.createElement('h2');title.textContent=config.title;this.shadowRoot.append(title);for(const item of config.entities){let el=document.createElement(item.type==='section'?'section':'div');if(item.type==='section'){el.textContent=item.label}else{el.className='row';el.dataset.id=item.entity;let label=document.createElement('span');label.textContent=item.name;el.append(label);let control;if(item.entity.startsWith('select.')){control=document.createElement('select');for(const text of ['High','Medium','Low','Custom']){let option=document.createElement('option');option.textContent=text;control.append(option)}}else if(item.entity.startsWith('number.')){control=document.createElement('input');control.type='range'}else if(item.entity.startsWith('switch.')){control=document.createElement('input');control.type='checkbox'}else{control=document.createElement('span');control.className='value'}el.append(control)}this.shadowRoot.append(el)}}
    set hass(hass){this.currentHass=hass;for(const el of this.shadowRoot.querySelectorAll('.row')){const value=el.querySelector('.value');if(value)value.textContent=hass.states[el.dataset.id]?.state||'unavailable'}}
  }
  customElements.define('native-card',NativeCard);window.createdCards=0;
  window.loadCardHelpers=async()=>({createCardElement:config=>{window.createdCards++;const card=document.createElement('native-card');card.setConfig(config);return card}});
</script><script type="module" src="/open_fume_extractor/ofe-cards.js"></script></body></html>`;

(async () => {
  const browser = await chromium.launch({headless: true, ...(process.env.OFE_BROWSER_EXECUTABLE ? {executablePath: process.env.OFE_BROWSER_EXECUTABLE} : {})});
  try {
    const page = await browser.newPage({viewport: {width: 1200, height: 900}});
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    await page.route("http://ofe.test/**", route => {
      const pathname = new URL(route.request().url()).pathname;
      if (pathname.endsWith("ofe-cards.js")) return route.fulfill({contentType: "text/javascript", body: fs.readFileSync(path.join(root, "frontend/ofe-cards.js"))});
      if (pathname.endsWith("logo.png")) return route.fulfill({contentType: "image/png", body: fs.readFileSync(path.join(root, "brand/logo.png"))});
      return route.fulfill({contentType: "text/html", body: html});
    });
    await page.goto("http://ofe.test/");
    await page.waitForFunction(() => customElements.get("ofe-overview-card"));
    await page.evaluate(states => {
      window.hassFixture = {states, language: "de"};
      window.card = document.createElement("ofe-overview-card");
      card.setConfig({type: "custom:ofe-overview-card"}); card.hass = hassFixture;
      document.querySelector("#host").append(card);
    }, states);
    await page.waitForFunction(() => card.shadowRoot.querySelectorAll("native-card").length === 2);
    assert.equal(await page.evaluate(() => window.customCards.length), 4);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector("native-card").config.entities.some(e => e.entity === "sensor.ofe_alarm_text")), true);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll("native-card")[0].config.entities.some(e => e.entity === "sensor.ofe_heap")), false);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll("native-card")[1].config.entities.some(e => e.label === "Port 1")), true);
    for (const id of ["number.ofe_jbc_temp", "switch.ofe_jbc_levels", "select.ofe_jbc_level", "select.ofe_filter_interval"])
      assert.equal(await page.evaluate(id => card.shadowRoot.querySelectorAll("native-card")[1].config.entities.some(e => e.entity === id), id), true);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll("native-card")[1].config.title), undefined);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device-title")[1].textContent), "0x20 Fan/IO Pro Testbank");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll("native-card")[1].config.entities.find(e => e.entity === "switch.ofe_output").name), "Extraction output");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device-head > ha-icon")[0].getAttribute("icon")), "mdi:electric-switch");
    assert.equal(await page.evaluate(() => getComputedStyle(card.shadowRoot.querySelector(".led-word")).fontSize), "17px");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector("img").naturalWidth > 0), true);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device").length), 2);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector(".eco").hidden), false);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector(".transport span").textContent), "Wi-Fi");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device")[1].querySelector(".transport span").textContent), "RS485");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device")[1].querySelector("progress").value), 55);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device")[1].querySelector("progress").parentElement.dataset.entity), "number.ofe_fan_power");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector('.led-word[data-channel="ofe"]').dataset.effect), "purplewhite");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector('.led-word[data-channel="evt"]').dataset.effect), "blink");
    assert.equal(await page.evaluate(() => getComputedStyle(card.shadowRoot.querySelector('.led-word[data-channel="ofe"]')).animationName), "led-color");
    await page.emulateMedia({reducedMotion: "reduce"});
    assert.equal(await page.evaluate(() => getComputedStyle(card.shadowRoot.querySelector('.led-word[data-channel="ofe"]')).animationName), "none");
    await page.emulateMedia({reducedMotion: "no-preference"});
    await page.evaluate(() => {
      hassFixture.states["sensor.ofe_led_ofe"].attributes.ofe_led_enabled = false; card.hass = hassFixture;
    });
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector('.led-word[data-channel="ofe"]').dataset.live), "false");
    assert.equal(await page.evaluate(() => getComputedStyle(card.shadowRoot.querySelector('.led-word[data-channel="ofe"]')).animationName), "none");
    await page.evaluate(() => {
      hassFixture.states["sensor.ofe_led_ofe"].attributes.ofe_led_enabled = true; card.hass = hassFixture;
    });
    await page.evaluate(() => card.addEventListener("hass-more-info", event => window.moreInfo = event.detail.entityId));
    await page.locator('ofe-overview-card [data-entity="sensor.ofe_requested"]').click();
    assert.equal(await page.evaluate(() => moreInfo), "sensor.ofe_requested");
    await page.locator('ofe-overview-card .led-word[data-entity="sensor.ofe_led_ofe"]').click();
    assert.equal(await page.evaluate(() => moreInfo), "sensor.ofe_led_ofe");
    await page.evaluate(() => {
      window.alarmMessages = ["0x20 Fan/IO: Filter wechseln; Drucksensor prüfen. " + "Lange Meldung mit Umlauten: äöü. ".repeat(12), "0x10 Station: <img src=x onerror=alert(1)>"];
      const alarm = hassFixture.states["sensor.ofe_alarms"];
      alarm.state = "2"; alarm.attributes.alarms = alarmMessages; alarm.attributes.alarm_count = 2;
      card.hass = hassFixture;
    });
    assert.deepEqual(await page.evaluate(() => [...card.shadowRoot.querySelectorAll(".alarm-list li")].map(el => el.textContent)), await page.evaluate(() => alarmMessages));
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector(".alarms h3 span").textContent), "Aktive Alarme");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector(".alarm-list img")), null);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device")[1].querySelector(".alarms").hidden), true);
    fs.mkdirSync(artifacts, {recursive: true});
    await page.screenshot({path: path.join(artifacts, "cards-desktop.png"), fullPage: true});
    const before = await page.evaluate(() => createdCards);
    await page.evaluate(() => {
      const select = card.shadowRoot.querySelector("native-card").shadowRoot.querySelector("select");
      window.selectedControl = select; select.value = "Low"; select.focus();
      hassFixture = {...hassFixture, states: {...hassFixture.states, "sensor.ofe_requested": {...hassFixture.states["sensor.ofe_requested"], state: "60 %"}}};
      hassFixture.states["sensor.ofe_alarms"].state = "0";
      hassFixture.states["sensor.ofe_alarms"].attributes.alarms = [];
      hassFixture.states["sensor.ofe_alarms"].attributes.alarm_count = 0;
      card.hass = hassFixture;
    });
    assert.equal(await page.evaluate(() => createdCards), before);
    assert.equal(await page.evaluate(() => selectedControl.value), "Low");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector(".alarms").hidden), true);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".alarm-list li").length), 0);
    assert.equal(await page.evaluate(() => selectedControl === card.shadowRoot.querySelector("native-card").shadowRoot.querySelector("select")), true);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector('[data-entity="sensor.ofe_requested"] .metric-value').textContent), "60 %");
    await page.evaluate(() => {
      for (const state of Object.values(hassFixture.states)) if(state.attributes.ofe_uid !== state.attributes.ofe_master_uid) state.state = "unavailable";
      card.hass = hassFixture;
    });
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll("native-card").length), 2);
    assert.equal(await page.evaluate(() => createdCards), before);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device")[1].querySelector(".link").dataset.state), "offline");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device")[1].querySelector("progress").hidden), true);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll(".device")[1].querySelector('.led-word[data-channel="ofe"]').dataset.live), "false");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector('[data-entity="sensor.ofe_filter"] .metric-value').textContent), "Nicht verfügbar");
    await page.evaluate(() => {
      hassFixture.states["sensor.ofe_port1"].attributes.ofe_active = false; card.hass = hassFixture;
    });
    await page.waitForFunction(() => !card.shadowRoot.querySelectorAll("native-card")[1].config.entities.some(e => e.entity === "sensor.ofe_port1"));
    await page.evaluate(() => {
      window.editor = document.createElement("ofe-card-editor");
      editor.setConfig({type: "custom:ofe-module-card"}); editor.hass = hassFixture;
      document.querySelector("#editor").append(editor);
      editor.addEventListener("config-changed", event => window.edited = event.detail.config);
    });
    await page.locator('ofe-card-editor [data-field="module_uid"] select').selectOption(moduleUid);
    assert.equal(await page.evaluate(() => edited.module_uid), moduleUid);
    await page.evaluate(() => {editor.hass = {...hassFixture};});
    assert.equal(await page.locator('ofe-card-editor [data-field="module_uid"] select').inputValue(), moduleUid);
    await page.evaluate(() => card.setConfig({type: "custom:ofe-overview-card", show_diagnostics: true}));
    await page.waitForFunction(() => card.shadowRoot.querySelector("native-card").config.entities.some(e => e.entity === "sensor.ofe_heap"));
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector("native-card").config.entities.some(e => e.entity === "sensor.ofe_led_ofe")), false);
    await page.evaluate(() => {
      const alarm = hassFixture.states["sensor.ofe_alarms"];
      alarm.attributes.alarms = alarmMessages; alarm.attributes.alarm_count = 2;
      alarm.state = "unavailable"; card.hass = hassFixture;
    });
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector(".alarms").hidden), true);
    await page.evaluate(() => {hassFixture.states["sensor.ofe_alarms"].state = "2"; card.hass = hassFixture;});
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector(".alarms").hidden), false);
    await page.setViewportSize({width: 390, height: 844});
    await page.screenshot({path: path.join(artifacts, "cards-mobile.png"), fullPage: true});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    await page.evaluate(() => {
      editor.setConfig({type: "custom:ofe-master-card"});
      card.setConfig({type: "custom:ofe-master-card"});
    });
    await page.waitForFunction(() => card.shadowRoot.querySelectorAll("native-card").length === 1);
    assert.equal(await page.locator('ofe-card-editor [data-field="module_uid"]').isVisible(), false);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector(".heading").hidden), true);
    await page.evaluate(() => {
      card.hass = {...hassFixture, language: "en"};
    });
    await page.waitForFunction(() => card.shadowRoot.querySelector(".link span").textContent === "Connected");
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector(".alarms h3 span").textContent), "Active alarms");
    await page.evaluate(() => card.setConfig({type: "custom:ofe-master-card", show_summary: false}));
    await page.waitForFunction(() => card.shadowRoot.querySelector(".metrics").hidden);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelector("native-card").config.entities.some(e => e.entity === "sensor.ofe_requested")), true);
    await page.evaluate(original => {
      for (const [id, state] of Object.entries(original)) {
        if (state.attributes.ofe_uid !== state.attributes.ofe_master_uid) hassFixture.states[id].state = state.state;
      }
    }, states);
    await page.evaluate(() => {
      card.setConfig({type: "custom:ofe-overview-card", master_uid: hassFixture.states["sensor.ofe_requested"].attributes.ofe_master_uid});
      const masterUid = hassFixture.states["sensor.ofe_requested"].attributes.ofe_master_uid;
      for (const [uid, type, model, values] of [
        ["JBC", 9, "JBC USB", [["sensor", "station_type", "Station", "DDE"], ["sensor", "jbc_usb_port_count", "Ports", "2"], ["binary_sensor", "jbc_work_mask", "Work", "off"], ["binary_sensor", "jbc_stand_mask", "Stand", "on"]]],
        ["WELLER", 5, "Weller Zero Smog", [["number", "weller_speed", "Power", "60"], ["sensor", "weller_rpm", "RPM", "2200"], ["sensor", "filter_remaining", "Remaining filter time", "6d 12h"], ["sensor", "filter_runtime", "Filter operating time", "1h 30m"]]],
        ["DISPLAY", 6, "Display 800x480", [["number", "brightness", "Brightness", "80"], ["select", "display_language", "Language", "Deutsch"], ["select", "display_theme", "Theme", "Dark"], ["select", "display_screensaver_min", "Screensaver", "5 min"]]],
        ["MODBUS", 8, "Modbus RTU Bridge", [["sensor", "entity_20_pressure", "Pressure", "180"], ["sensor", "entity_21_temperature", "Temperature", "25"], ["binary_sensor", "entity_22_start", "Input", "off"], ["switch", "entity_23_valve", "Valve", "on"]]],
      ]) {
        for (const [domain, key, label, value] of [...values, ["binary_sensor", "online", "Connection", "on"]]) {
          const id = domain + "." + uid.toLowerCase() + "_" + key;
          hassFixture.states[id] = {entity_id: id, state: value, attributes: {ofe_master_uid: masterUid, ofe_uid: uid,
            ofe_key: key, ofe_group: domain === "sensor" || domain === "binary_sensor" ? "status" : "controls",
            ofe_name: label, ofe_device_name: model, ofe_device_model: model, ofe_device_type: type,
            ofe_transport: type === 6 ? "wifi" : "rs485", ofe_active: true}};
        }
      }
      card.hass = {...hassFixture, language: "de"};
    });
    await page.waitForFunction(() => card.shadowRoot.querySelectorAll(".device").length === 6);
    for (const [name, icon] of [["JBC USB", "mdi:usb-port"], ["Weller Zero Smog", "mdi:fan"],
      ["Display 800x480", "mdi:monitor"], ["Modbus RTU Bridge", "mdi:serial-port"]]) {
      assert.equal(await page.evaluate(name => [...card.shadowRoot.querySelectorAll(".device")]
        .find(el => el.querySelector(".device-title").textContent === name)
        .querySelector(".device-head > ha-icon").getAttribute("icon"), name), icon);
    }
    assert.equal(await page.evaluate(() => [...card.shadowRoot.querySelectorAll(".device")].find(el => el.querySelector(".device-title").textContent === "JBC USB").querySelectorAll(".metric").length), 4);
    assert.equal(await page.evaluate(() => [...card.shadowRoot.querySelectorAll(".device")].find(el => el.querySelector(".device-title").textContent === "Display 800x480").querySelectorAll(".metric").length), 4);
    assert.equal(await page.evaluate(() => [...card.shadowRoot.querySelectorAll(".device")].find(el => el.querySelector(".device-title").textContent === "Display 800x480").querySelector(".model").hidden), true);
    await page.setViewportSize({width: 1200, height: 1000});
    await page.screenshot({path: path.join(artifacts, "cards-desktop.png"), fullPage: true});
    await page.setViewportSize({width: 390, height: 844});
    await page.screenshot({path: path.join(artifacts, "cards-mobile.png"), fullPage: true});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    await page.setViewportSize({width: 320, height: 780});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    assert.equal(await page.evaluate(() => [...card.shadowRoot.querySelectorAll('.metric-label,.metric-value,.device-title,.led-pair')].every(el => {
      const parent = el.parentElement.getBoundingClientRect(), box = el.getBoundingClientRect();
      return box.left >= parent.left - 1 && box.right <= parent.right + 1 && el.scrollWidth <= el.clientWidth + 1;
    })), true);
    await page.evaluate(() => {
      document.body.style.background = "#111";
      document.body.style.color = "#eee";
      for (const [key, value] of [["--primary-text-color", "#eee"], ["--secondary-text-color", "#aaa"], ["--card-background-color", "#202020"], ["--divider-color", "#444"]]) document.body.style.setProperty(key, value);
      card.setConfig({type: "custom:ofe-overview-card", master_uid: hassFixture.states["sensor.ofe_requested"].attributes.ofe_master_uid});
    });
    await page.screenshot({path: path.join(artifacts, "cards-dark-mobile.png"), fullPage: true});
    await page.evaluate(() => card.setConfig({type: "custom:ofe-master-card"}));
    await page.evaluate(() => {
      hassFixture.states["sensor.other"] = {...hassFixture.states["sensor.ofe_requested"], entity_id: "sensor.other",
        attributes: {...hassFixture.states["sensor.ofe_requested"].attributes, ofe_master_uid: "OTHER", ofe_uid: "OTHER"}};
      card.hass = hassFixture;
    });
    assert.match(await page.evaluate(() => card.shadowRoot.querySelector(".message").textContent), /Master/);
    assert.equal(await page.evaluate(() => card.shadowRoot.querySelectorAll("native-card").length), 0);
    await page.evaluate(() => {
      delete hassFixture.states["sensor.other"];
      hassFixture.states["sensor.mqtt_alarms"] = {entity_id: "sensor.mqtt_alarms", state: "2",
        attributes: {friendly_name: "Open Fume Extractor Alarm Anzahl", alarm_count: 2, alarms: alarmMessages}};
      window.alarmCard = document.createElement("ofe-alarms-card");
      alarmCard.setConfig({type: "custom:ofe-alarms-card", entity: "sensor.mqtt_alarms"});
      alarmCard.hass = hassFixture;
      document.querySelector("#host").replaceChildren(alarmCard);
      editor.setConfig({type: "custom:ofe-alarms-card", entity: "sensor.mqtt_alarms"}); editor.hass = hassFixture;
    });
    assert.deepEqual(await page.evaluate(() => [...alarmCard.shadowRoot.querySelectorAll("li")].map(el => el.textContent)), await page.evaluate(() => alarmMessages));
    assert.equal(await page.evaluate(() => alarmCard.shadowRoot.querySelector("img")), null);
    assert.equal(await page.locator('ofe-card-editor [data-field="entity"]').isVisible(), true);
    assert.equal(await page.locator('ofe-card-editor [data-field="master_uid"]').isVisible(), false);
    assert.equal(await page.locator('ofe-card-editor [data-field="entity"] select').inputValue(), "sensor.mqtt_alarms");
    await page.setViewportSize({width: 1200, height: 900});
    await page.screenshot({path: path.join(artifacts, "alarms-mqtt-desktop.png"), fullPage: true});
    await page.setViewportSize({width: 390, height: 844});
    await page.screenshot({path: path.join(artifacts, "alarms-mqtt-mobile.png"), fullPage: true});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    const listItem = await page.evaluate(() => {window.alarmItem = alarmCard.shadowRoot.querySelector("li"); alarmCard.hass = {...hassFixture}; return alarmItem === alarmCard.shadowRoot.querySelector("li");});
    assert.equal(listItem, true);
    await page.evaluate(() => {hassFixture.states["sensor.mqtt_alarms"].state = "unavailable"; alarmCard.hass = hassFixture;});
    assert.equal(await page.evaluate(() => alarmCard.shadowRoot.querySelector(".message").textContent), "Nicht verfügbar");
    assert.equal(await page.evaluate(() => alarmCard.shadowRoot.querySelectorAll("li").length), 0);
    await page.evaluate(() => {
      hassFixture.states["sensor.mqtt_alarms"].state = "0"; hassFixture.states["sensor.mqtt_alarms"].attributes.alarms = [];
      alarmCard.hass = {...hassFixture, language: "en"};
    });
    assert.equal(await page.evaluate(() => alarmCard.shadowRoot.querySelector(".message").textContent), "No active alarms");
    await page.locator('ofe-card-editor [data-field="entity"] select').selectOption("sensor.ofe_alarms");
    assert.equal(await page.evaluate(() => edited.entity), "sensor.ofe_alarms");
    await page.evaluate(() => {alarmCard.setConfig(edited); alarmCard.hass = hassFixture;});
    assert.equal(await page.evaluate(() => alarmCard.shadowRoot.querySelectorAll("li").length), 2);
    assert.deepEqual(errors, []);
    console.log("OFE card browser contracts passed; desktop/mobile screenshots saved.");
  } finally { await browser.close(); }
})().catch(error => {console.error(error);process.exitCode = 1;});
