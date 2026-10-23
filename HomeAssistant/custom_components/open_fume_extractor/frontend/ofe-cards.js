/* OFE cards delegate all controls to Home Assistant's native entities card. */
const VERSION = "0.1.0rc25";
let helpersPromise;
const words = {
  en: {master: "Master", module: "Module", overview: "Overview", auto: "Automatic",
    status: "Status", controls: "Controls", filter: "Filter", diagnostics: "Diagnostics", alarms: "Active alarms",
    thermocouples: "Thermocouples", port: "Port", title: "Title", diagnostic: "Show diagnostics",
    summary: "Show summary", online: "Connected", offline: "Disconnected", unknown: "Unknown", unavailable: "Unavailable", on: "On", off: "Off", eco: "Power saving",
    noAlarms: "No active alarms", alarmSensor: "Alarm count sensor (MQTT or native)", chooseAlarm: "Select an alarm count sensor in the card editor.",
    empty: "No Open Fume Extractor entities found.", chooseMaster: "Select a master in the card editor.",
    chooseModule: "Select a module in the card editor.", missing: "The selected device is not available."},
  de: {master: "Master", module: "Modul", overview: "Übersicht", auto: "Automatisch",
    status: "Status", controls: "Bedienung", filter: "Filter", diagnostics: "Diagnose", alarms: "Aktive Alarme",
    thermocouples: "Thermoelemente", port: "Port", title: "Titel", diagnostic: "Diagnose anzeigen",
    summary: "Zusammenfassung anzeigen", online: "Verbunden", offline: "Getrennt", unknown: "Unbekannt", unavailable: "Nicht verfügbar", on: "Ein", off: "Aus", eco: "Energiesparen",
    noAlarms: "Keine aktiven Alarme", alarmSensor: "Alarm-Anzahl-Sensor (MQTT oder nativ)", chooseAlarm: "Alarm-Anzahl-Sensor im Karteneditor auswählen.",
    empty: "Keine Open-Fume-Extractor-Entitäten gefunden.", chooseMaster: "Master im Karteneditor auswählen.",
    chooseModule: "Modul im Karteneditor auswählen.", missing: "Das ausgewählte Gerät ist nicht verfügbar."},
};
const language = hass => String(hass?.locale?.language || hass?.language || "en").startsWith("de") ? "de" : "en";
const entities = hass => Object.values(hass?.states || {}).filter(s =>
  s.attributes?.ofe_master_uid && s.attributes.ofe_uid && s.attributes.ofe_active === true);
const masters = rows => [...new Set(rows.map(s => s.attributes.ofe_master_uid))].sort();
const devices = (rows, master) => [...new Map(rows.filter(s => s.attributes.ofe_master_uid === master)
  .map(s => [s.attributes.ofe_uid, s.attributes.ofe_device_name])).entries()].sort((a, b) => a[0].localeCompare(b[0]));
const groupRank = group => ({status: 0, controls: 1, filter: 2, thermocouples: 4, diagnostics: 5}[group] ?? 3);
const leafKey = state => String(state.attributes.ofe_key || "").replace(/^entity_\d+_/, "");
const hasValue = state => state && !["unknown", "unavailable"].includes(state.state);
const alarmRows = state => hasValue(state) && Array.isArray(state.attributes?.alarms) ?
  state.attributes.alarms.filter(alarm => typeof alarm === "string" && alarm) : [];
function updateAlarmList(list, rows) {
  const signature = JSON.stringify(rows);
  if (list._alarmSignature === signature) return;
  list._alarmSignature = signature;
  list.replaceChildren(...rows.map(alarm => {
    const item = document.createElement("li"); item.textContent = alarm; return item;
  }));
}
function summaryRows(rows, uid) {
  const selected = rows.filter(s => s.attributes.ofe_uid === uid), attr = selected[0]?.attributes || {};
  const master = uid === attr.ofe_master_uid;
  const find = (...keys) => selected.find(s => keys.includes(leafKey(s)));
  let candidates;
  if (master) candidates = [find("output_power"), find("feedback_rpm"), find("afterrun"), find("alarm_count")];
  else if ([1, 9].includes(attr.ofe_device_type) || find("jbc_link")) {
    candidates = [find("station_type", "jbc_usb_model"), find("jbc_usb_port_count"), find("jbc_work_mask"), find("jbc_stand_mask")];
  } else if (attr.ofe_device_type === 6 || find("brightness")) {
    candidates = [find("brightness"), find("display_language"), find("display_theme"), find("display_screensaver_min")];
  } else {
    candidates = [find("power", "main_power", "weller_speed"), find("rpm", "weller_rpm"),
      find("filter_remaining"), find("filter_operating_time", "filter_runtime")];
  }
  if (!candidates.some(Boolean)) candidates = selected.filter(s => s.attributes.ofe_group !== "diagnostics" &&
    ["sensor", "binary_sensor"].includes(s.entity_id.split(".")[0]) && !["online", "eco", "power_save_active"].includes(leafKey(s)));
  return [...new Map(candidates.filter(Boolean).map(s => [s.entity_id, s])).values()].slice(0, 4);
}
function formattedState(hass, state, text) {
  if (!state || state.state === "unavailable") return text.unavailable;
  if (state.state === "unknown") return text.unknown;
  if (hass.formatEntityState) return hass.formatEntityState(state);
  if (state.state === "on" || state.state === "off") return text[state.state];
  const unit = state.attributes.unit_of_measurement;
  return `${state.state}${unit && !String(state.state).endsWith(unit) ? " " + unit : ""}`;
}
function deviceConfig(rows, uid, diagnostic, text, title, summary) {
  const summarized = new Set(summary ? summaryRows(rows, uid).filter(s =>
    ["sensor", "binary_sensor"].includes(s.entity_id.split(".")[0])).map(s => s.entity_id) : []);
  const selected = rows.filter(s => s.attributes.ofe_uid === uid && !["led_ofe", "led_evt"].includes(leafKey(s)) &&
    !summarized.has(s.entity_id) && (diagnostic || s.attributes.ofe_group !== "diagnostics"));
  selected.sort((a, b) => groupRank(a.attributes.ofe_group) - groupRank(b.attributes.ofe_group) ||
    String(a.attributes.ofe_group).localeCompare(String(b.attributes.ofe_group), "en", {numeric: true}) ||
    a.entity_id.localeCompare(b.entity_id, "en", {numeric: true}));
  const result = [];
  let previous;
  for (const state of selected) {
    const attr = state.attributes, group = attr.ofe_group || "status";
    if (previous !== group) {
      const label = group.startsWith("port_") ? `${text.port} ${group.slice(5)}` : text[group] || group;
      result.push({type: "section", label});
      previous = group;
    }
    result.push({entity: state.entity_id, name: attr.ofe_name || attr.friendly_name});
  }
  return {type: "entities", title: title || rows.find(s => s.attributes.ofe_uid === uid)?.attributes.ofe_device_name || "Open Fume Extractor",
    show_header_toggle: false, state_color: true, entities: result};
}

class OfeCard extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({mode: "open"});
    this.shadowRoot.innerHTML = `<style>
      :host{display:block;min-width:0;container-type:inline-size;font-family:var(--paper-font-body1_-_font-family,inherit);letter-spacing:0}
      *{box-sizing:border-box}.cards{display:grid;grid-template-columns:minmax(0,1fr);gap:16px}
      .overview{grid-template-columns:repeat(auto-fit,minmax(min(340px,100%),1fr));align-items:start}
      .heading{display:flex;align-items:center;gap:12px;margin:0 0 20px;font-size:20px;font-weight:600}
      .heading img{width:40px;height:40px;object-fit:contain;flex-shrink:0}.heading span{min-width:0;overflow-wrap:anywhere}
      .message{padding:16px;color:var(--secondary-text-color)}
      .heading[hidden],.message:empty{display:none}
      .device{display:block;min-width:0;border-radius:8px;overflow:hidden;background:var(--ha-card-background,var(--card-background-color));
        border:1px solid var(--ha-card-border-color,var(--divider-color));box-shadow:var(--ha-card-box-shadow,none)}
      .device-head{display:grid;grid-template-columns:40px minmax(0,1fr) 104px;align-items:center;gap:12px;padding:20px 20px 12px;min-width:0}
      .device-head>img{width:40px;height:40px;object-fit:contain}.device-head>ha-icon{--mdc-icon-size:24px;width:40px;height:40px;
        display:flex;align-items:center;justify-content:center;background:var(--secondary-background-color);border-radius:8px;color:var(--primary-color)}
      .identity{min-width:0}.device-title{font-size:17px;font-weight:600;line-height:1.4;overflow-wrap:anywhere}
      .model{font-size:12px;color:var(--secondary-text-color);margin-top:4px;overflow-wrap:anywhere}
      .indicators{display:flex;flex-wrap:wrap;gap:8px 16px;padding:0 20px 18px;font-size:12px;color:var(--secondary-text-color)}
      .indicator{display:inline-flex;align-items:center;gap:5px}.indicator ha-icon{--mdc-icon-size:16px}
      .link[data-state="online"]{color:var(--success-color,#168369)}.link[data-state="offline"]{color:var(--error-color,#b43c3c)}
      .eco{color:var(--success-color,#168369)}.indicator[hidden]{display:none}
      .metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));border-block:1px solid var(--divider-color);padding:0 12px}
      .metric{font:inherit;text-align:left;min-width:0;min-height:112px;border:0;border-radius:0;padding:16px 8px;background:transparent;
        color:var(--primary-text-color);cursor:pointer}.metric:hover{background:var(--secondary-background-color)}
      .metric:nth-child(n+3){border-top:1px solid var(--divider-color)}
      .metric:nth-child(odd){padding-right:16px}.metric:nth-child(even){padding-left:16px;border-left:1px solid var(--divider-color)}
      .metric:focus-visible{outline:2px solid var(--primary-color);outline-offset:-2px}.metric-label{display:flex;align-items:center;gap:6px;
        font-size:12px;line-height:1.4;color:var(--secondary-text-color);overflow-wrap:anywhere}.metric ha-icon{--mdc-icon-size:16px;flex-shrink:0}
      .metric-value{display:block;font-size:22px;font-weight:600;line-height:1.35;min-height:1.35em;margin-top:9px;overflow-wrap:anywhere;font-variant-numeric:tabular-nums}
      .metric[data-tone="active"] .metric-value{color:var(--success-color,#168369)}
      .metric[data-tone="alert"] .metric-value{color:var(--error-color,#b43c3c)}
      .metric[data-tone="muted"] .metric-value{color:var(--secondary-text-color);font-size:16px}
      .meter{width:100%;height:4px;margin-top:10px;display:block;appearance:none;border:0;border-radius:2px;overflow:hidden;background:var(--divider-color);accent-color:var(--primary-color)}
      .meter::-webkit-progress-bar{background:var(--divider-color)}.meter::-webkit-progress-value{background:var(--primary-color)}
      .meter::-moz-progress-bar{background:var(--primary-color)}.meter[hidden],.metrics[hidden]{display:none}
      .alarms{padding:16px 20px;border-bottom:1px solid var(--divider-color)}.alarms[hidden]{display:none}
      .alarms h3{display:flex;align-items:center;gap:8px;font-size:14px;font-weight:600;margin:0 0 8px;color:var(--error-color,#b43c3c)}
      .alarms h3 ha-icon{--mdc-icon-size:18px}.alarm-list{margin:0;padding-left:20px;font-size:14px;line-height:1.5}
      .alarm-list li{padding:4px 0;overflow-wrap:anywhere}
      .led-pair{display:flex;align-items:center;justify-content:flex-end;gap:8px;min-width:0}
      .led-word{font:600 17px var(--paper-font-body1_-_font-family,Arial,sans-serif);letter-spacing:0;border:0;background:none;padding:8px 2px;
        min-width:48px;min-height:44px;color:var(--secondary-text-color);cursor:pointer}
      .led-word:disabled{cursor:default}.led-word:focus-visible{outline:2px solid var(--primary-color);border-radius:4px}
      .led-word[data-live="true"]{color:var(--led-color);text-shadow:0 0 3px var(--led-color),0 0 8px var(--led-color),0 1px 1px #555;
        animation-duration:var(--led-period);animation-iteration-count:infinite;animation-timing-function:linear}
      .led-word[data-effect="breath"],.led-word[data-effect="whitebreath"]{animation-name:led-breath}
      .led-word[data-effect="greenwhite"],.led-word[data-effect="bluewhite"],.led-word[data-effect="purplewhite"]{animation-name:led-color}
      .led-word[data-effect="blink"]{animation-name:led-blink;animation-timing-function:steps(1,end)}
      .led-word[data-effect="double"]{animation-name:led-double;animation-timing-function:steps(1,end)}
      @keyframes led-breath{0%,100%{opacity:.2}50%{opacity:1}}
      @keyframes led-color{0%,100%{color:var(--led-color)}50%{color:#fff}}
      @keyframes led-blink{0%,49.99%{opacity:.15}50%,100%{opacity:1}}
      @keyframes led-double{0%,10%,20%,30%,100%{opacity:.15}0.01%,20.01%{opacity:1}}
      @media(prefers-reduced-motion:reduce){.led-word{animation:none!important}}
      @container(max-width:360px){.device-head{grid-template-columns:36px minmax(0,1fr);padding:16px 16px 8px;gap:8px 12px}
        .device-head>img,.device-head>ha-icon{width:36px;height:36px}.led-pair{grid-column:2;justify-content:flex-start}
        .indicators{padding:0 16px 14px}.metrics{padding:0 8px}}
    </style><div class="heading" hidden><img src="/open_fume_extractor/logo.png" alt=""><span></span></div>
    <div class="message" role="status"></div><div class="cards"></div>`;
    this._children = [];
    this._panels = [];
    this._generation = 0;
  }
  static getConfigElement() { return document.createElement("ofe-card-editor"); }
  static getStubConfig() { return {show_diagnostics: false, show_summary: true}; }
  setConfig(config) {
    if (!config?.type || !["custom:ofe-master-card", "custom:ofe-module-card", "custom:ofe-overview-card"].includes(config.type)) {
      throw new Error("Invalid OFE card type");
    }
    this._config = {...config};
    this._signature = null;
    this._schedule();
  }
  set hass(hass) {
    this._hass = hass;
    for (const card of this._children) card.hass = hass;
    this._schedule();
  }
  connectedCallback() { this._schedule(); }
  getCardSize() { return this.offsetHeight ? Math.ceil(this.offsetHeight / 50) : Math.max(3, this._rowCount || 3); }
  getGridOptions() { return {columns: this._config?.type === "custom:ofe-overview-card" ? "full" : 12, min_columns: 6}; }
  _panel(config, uid, rows, helpers) {
    const root = document.createElement("ha-card"); root.className = "device";
    const header = document.createElement("div"); header.className = "device-head";
    const mark = document.createElement(uid === rows[0]?.attributes.ofe_master_uid ? "img" : "ha-icon");
    if (mark.localName === "img") { mark.src = "/open_fume_extractor/logo.png"; mark.alt = ""; }
    else mark.setAttribute("icon", ({1: "mdi:serial-port", 2: "mdi:electric-switch", 3: "mdi:electric-switch", 5: "mdi:fan",
      6: "mdi:monitor", 7: "mdi:serial-port", 8: "mdi:serial-port", 9: "mdi:usb-port"})[rows[0]?.attributes.ofe_device_type] || "mdi:chip");
    const identity = document.createElement("div"); identity.className = "identity";
    const title = document.createElement("div"); title.className = "device-title"; title.textContent = config.title;
    const model = document.createElement("div"); model.className = "model"; identity.append(title, model); header.append(mark, identity);
    const leds = document.createElement("div"); leds.className = "led-pair";
    for (const channel of ["ofe", "evt"]) {
      const led = document.createElement("button"); led.className = "led-word"; led.type = "button";
      led.dataset.channel = channel; led.textContent = channel.toUpperCase();
      led.addEventListener("click", () => { if (led.dataset.entity) this.dispatchEvent(new CustomEvent("hass-more-info", {
        detail: {entityId: led.dataset.entity}, bubbles: true, composed: true})); });
      leds.append(led);
    }
    header.append(leds);
    const indicators = document.createElement("div"); indicators.className = "indicators";
    for (const [name, icon] of [["link", "mdi:lan-connect"], ["transport", "mdi:serial-port"], ["eco", "mdi:sprout"]]) {
      const badge = document.createElement("span"); badge.className = `indicator ${name}`;
      const symbol = document.createElement("ha-icon"); symbol.setAttribute("icon", icon);
      badge.append(symbol, document.createElement("span")); indicators.append(badge);
    }
    const metrics = document.createElement("div"); metrics.className = "metrics";
    const alarms = document.createElement("section"); alarms.className = "alarms"; alarms.hidden = true;
    const alarmHeading = document.createElement("h3"), alarmIcon = document.createElement("ha-icon"), alarmTitle = document.createElement("span");
    alarmIcon.setAttribute("icon", "mdi:bell-alert-outline"); alarmHeading.append(alarmIcon, alarmTitle);
    const alarmList = document.createElement("ul"); alarmList.className = "alarm-list"; alarms.append(alarmHeading, alarmList);
    const meter = document.createElement("progress"); meter.className = "meter"; meter.max = 100;
    // The outer header owns the device title, independent of HA's shadow DOM.
    const {title: _title, ...entitiesConfig} = config;
    const card = helpers.createCardElement(entitiesConfig); card.hass = this._hass;
    root.append(header, indicators, metrics, meter, alarms, card);
    // Keep HA's controls, but remove the inner entities card's visual frame.
    const unframe = () => {
      if (!card.shadowRoot || card.shadowRoot.querySelector("[data-ofe-style]")) return;
      const style = document.createElement("style"); style.dataset.ofeStyle = "";
      style.textContent = ":host{--ha-card-border-width:0;--ha-card-box-shadow:none;--ha-card-background:transparent;border:0;box-shadow:none;background:transparent;border-radius:0}ha-card{border:0;box-shadow:none;background:transparent;border-radius:0}.card-header,h2{display:none}";
      card.shadowRoot.append(style);
    };
    customElements.whenDefined(card.localName).then(unframe);
    this._children.push(card);
    return {root, uid, metrics, model, indicators, meter, leds, alarms, alarmTitle, alarmList};
  }
  _updatePanels(rows, text) {
    for (const panel of this._panels) {
      const selected = rows.filter(s => s.attributes.ofe_uid === panel.uid), attr = selected[0]?.attributes || {};
      const find = key => selected.find(s => leafKey(s) === key);
      const alarmState = find("alarm_count");
      const alarms = alarmRows(alarmState);
      panel.alarms.hidden = panel.uid !== attr.ofe_master_uid || !alarms.length;
      panel.alarmTitle.textContent = text.alarms;
      updateAlarmList(panel.alarmList, alarms);
      const link = find("online"), connected = link ? hasValue(link) && ["on", "true", "online"].includes(link.state) : selected.some(hasValue);
      const badge = panel.indicators.querySelector(".link"); badge.dataset.state = connected ? "online" : "offline";
      badge.querySelector("span").textContent = connected ? text.online : text.offline;
      badge.querySelector("ha-icon").setAttribute("icon", connected ? "mdi:lan-connect" : "mdi:lan-disconnect");
      const eco = find("eco") || find("power_save_active"), ecoBadge = panel.indicators.querySelector(".eco");
      ecoBadge.hidden = !connected || !hasValue(eco) || !["on", "true"].includes(eco.state);
      ecoBadge.querySelector("span").textContent = text.eco;
      const transport = attr.ofe_transport || find("transport")?.state, transportBadge = panel.indicators.querySelector(".transport");
      transportBadge.hidden = !["wifi", "rs485"].includes(transport);
      transportBadge.querySelector("span").textContent = transport === "wifi" ? "Wi-Fi" : "RS485";
      transportBadge.querySelector("ha-icon").setAttribute("icon", transport === "wifi" ? "mdi:wifi" : "mdi:serial-port");
      panel.model.textContent = attr.ofe_device_model || (panel.uid === attr.ofe_master_uid ? "Open Fume Extractor" : text.module);
      panel.model.hidden = panel.model.textContent === panel.root.querySelector(".device-title").textContent.replace(/^0x[0-9A-F]{2} /i, "");
      for (const led of panel.leds.children) {
        const state = find("led_" + led.dataset.channel), data = state?.attributes || {};
        const effect = ["off", "solid", "breath", "whitebreath", "greenwhite", "bluewhite", "purplewhite", "blink", "double"].includes(data.ofe_led_effect) ? data.ofe_led_effect : "off";
        const live = connected && hasValue(state) && data.ofe_led_enabled === true && effect !== "off";
        led.dataset.entity = state?.entity_id || ""; led.disabled = !state;
        led.dataset.live = String(live); led.dataset.effect = live ? effect : "off";
        led.style.setProperty("--led-color", /^#[0-9a-f]{6}$/i.test(data.ofe_led_color) ? data.ofe_led_color : "#59616b");
        led.style.setProperty("--led-period", `${Math.max(100, Math.min(10000, Number(data.ofe_led_period_ms) || 3200))}ms`);
        led.title = `${led.textContent}: ${formattedState(this._hass, state, text)}`;
        led.setAttribute("aria-label", led.title);
      }
      const summary = this._config.show_summary !== false ? summaryRows(rows, panel.uid) : [];
      panel.metrics.hidden = !summary.length;
      const signature = JSON.stringify(summary.map(s => [s.entity_id, s.attributes.ofe_name, s.attributes.icon]));
      if (signature !== panel.signature) {
        panel.signature = signature;
        panel.metrics.replaceChildren(...summary.map(s => {
          const button = document.createElement("button"); button.className = "metric"; button.type = "button"; button.dataset.entity = s.entity_id;
          button.addEventListener("click", () => this.dispatchEvent(new CustomEvent("hass-more-info", {
            detail: {entityId: s.entity_id}, bubbles: true, composed: true})));
          const label = document.createElement("span"); label.className = "metric-label";
          const icon = document.createElement("ha-icon"); icon.setAttribute("icon", s.attributes.icon || "mdi:gauge");
          const name = document.createElement("span"); name.textContent = (s.attributes.ofe_name || s.attributes.friendly_name || s.entity_id).replace(/^0x[0-9A-F]{2} /i, "");
          const value = document.createElement("span"); value.className = "metric-value"; label.append(icon, name); button.append(label, value); return button;
        }));
      }
      for (const metric of panel.metrics.children) {
        const state = this._hass.states[metric.dataset.entity];
        const value = formattedState(this._hass, state, text);
        const display = metric.querySelector(".metric-value"); if (display.textContent !== value) display.textContent = value;
        metric.dataset.tone = !hasValue(state) ? "muted" : leafKey(state) === "alarm_count" && Number(state.state) > 0 ? "alert" :
          state.entity_id.startsWith("binary_sensor.") && state.state === "on" ? "active" : "normal";
        metric.title = value;
      }
      const power = summary.find(s => ["output_power", "power", "main_power", "weller_speed"].includes(leafKey(s)));
      const number = Number.parseFloat(power?.state);
      panel.meter.hidden = !connected || !hasValue(power) || !Number.isFinite(number) ||
        (power.attributes.unit_of_measurement && power.attributes.unit_of_measurement !== "%");
      panel.meter.value = Math.min(100, Math.max(0, number || 0));
      panel.meter.setAttribute("aria-label", power?.attributes.ofe_name || text.status);
      const powerMetric = power && [...panel.metrics.children].find(el => el.dataset.entity === power.entity_id);
      if (powerMetric && panel.meter.parentElement !== powerMetric) powerMetric.append(panel.meter);
    }
  }
  _schedule() {
    if (!this._config || !this._hass) return;
    const all = entities(this._hass), text = words[language(this._hass)];
    const hubs = masters(all), master = this._config.master_uid || (hubs.length === 1 ? hubs[0] : null);
    let message = !hubs.length ? text.empty : !master ? text.chooseMaster : "";
    const rows = all.filter(s => s.attributes.ofe_master_uid === master);
    let ids = devices(rows, master).map(([uid]) => uid);
    if (this._config.type === "custom:ofe-master-card") ids = ids.filter(uid => uid === master);
    if (this._config.type === "custom:ofe-module-card") {
      ids = ids.filter(uid => uid !== master);
      const uid = this._config.module_uid || (ids.length === 1 ? ids[0] : null);
      if (!uid && !message) message = text.chooseModule;
      ids = ids.filter(id => id === uid);
    }
    if (!ids.length && !message) message = text.missing;
    ids.sort((a, b) => (a === master ? -1 : b === master ? 1 : a.localeCompare(b)));
    const configs = message ? [] : ids.map(uid => deviceConfig(rows, uid, this._config.show_diagnostics, text,
      this._config.type !== "custom:ofe-overview-card" ? this._config.title : null, this._config.show_summary !== false));
    const signature = JSON.stringify([configs, message, language(this._hass)]);
    if (signature === this._signature) { this._updatePanels(rows, text); return; }
    this._signature = signature;
    const generation = ++this._generation;
    this.shadowRoot.querySelector(".message").textContent = message;
    const heading = this.shadowRoot.querySelector(".heading");
    heading.hidden = this._config.type !== "custom:ofe-overview-card" || !!message;
    heading.querySelector("span").textContent = this._config.title || "Open Fume Extractor";
    this._rowCount = configs.reduce((sum, c) => sum + c.entities.length, 0);
    if (!configs.length) {
      this._children = [];
      this._panels = [];
      this.shadowRoot.querySelector(".cards").replaceChildren();
      return;
    }
    helpersPromise ||= window.loadCardHelpers();
    helpersPromise.then(helpers => {
      if (generation !== this._generation) return;
      this._children = [];
      this._panels = configs.map((config, index) => this._panel(config, ids[index], rows.filter(s => s.attributes.ofe_uid === ids[index]), helpers));
      const container = this.shadowRoot.querySelector(".cards");
      container.classList.toggle("overview", this._config.type === "custom:ofe-overview-card");
      container.replaceChildren(...this._panels.map(panel => panel.root));
      this._updatePanels(entities(this._hass).filter(s => s.attributes.ofe_master_uid === master), words[language(this._hass)]);
    }).catch(error => {
      if (generation !== this._generation) return;
      this._signature = null;
      this.shadowRoot.querySelector(".message").textContent = String(error.message || error);
    });
  }
}

class OfeAlarmsCard extends HTMLElement {
  constructor() {
    super(); this.attachShadow({mode: "open"});
    this.shadowRoot.innerHTML = `<style>
      :host{display:block;min-width:0;letter-spacing:0}ha-card{display:block;padding:20px;border-radius:8px}
      h2{display:flex;align-items:center;gap:10px;font-size:17px;line-height:1.4;font-weight:600;margin:0 0 12px;overflow-wrap:anywhere}
      ha-icon{--mdc-icon-size:22px;flex-shrink:0;color:var(--error-color,#b43c3c)}
      ul{margin:0;padding-left:20px;font-size:14px;line-height:1.5}li{padding:4px 0;overflow-wrap:anywhere}
      .message{font-size:14px;color:var(--secondary-text-color);overflow-wrap:anywhere}[hidden]{display:none}
    </style><ha-card><h2><ha-icon icon="mdi:bell-alert-outline"></ha-icon><span></span></h2>
      <div class="message" role="status"></div><ul class="alarm-list"></ul></ha-card>`;
  }
  static getConfigElement() { return document.createElement("ofe-card-editor"); }
  static getStubConfig(hass) {
    const state = Object.values(hass?.states || {}).find(s => s.entity_id.startsWith("sensor.") && Array.isArray(s.attributes?.alarms));
    return {entity: state?.entity_id || ""};
  }
  setConfig(config) {
    if (config?.type !== "custom:ofe-alarms-card") throw new Error("Invalid OFE alarm card type");
    this._config = {...config}; this._update();
  }
  set hass(hass) { this._hass = hass; this._update(); }
  getCardSize() { return this.offsetHeight ? Math.ceil(this.offsetHeight / 50) : 3; }
  getGridOptions() { return {columns: 12, min_columns: 6}; }
  _update() {
    if (!this._config || !this._hass) return;
    const text = words[language(this._hass)], state = this._hass.states[this._config.entity], rows = alarmRows(state);
    this.shadowRoot.querySelector("h2 span").textContent = this._config.title || text.alarms;
    const message = this.shadowRoot.querySelector(".message"), list = this.shadowRoot.querySelector("ul");
    message.textContent = !this._config.entity ? text.chooseAlarm : !hasValue(state) ? text.unavailable :
      !Array.isArray(state.attributes?.alarms) ? text.unknown : rows.length ? "" : text.noAlarms;
    message.hidden = !message.textContent; list.hidden = !rows.length;
    updateAlarmList(list, rows);
  }
}

class OfeCardEditor extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({mode: "open"});
    this.shadowRoot.innerHTML = `<style>
      :host{display:block}label{display:grid;gap:6px;margin:12px 0}input,select{box-sizing:border-box;
      width:100%;min-height:40px;font:inherit;color:var(--primary-text-color);background:var(--card-background-color);
      border:1px solid var(--divider-color);border-radius:4px;padding:8px}.check{display:flex;align-items:center}
      .check input{width:20px;min-height:20px}
      label[hidden]{display:none}
    </style><label data-field="master_uid"><span></span><select></select></label>
    <label data-field="module_uid"><span></span><select></select></label>
    <label data-field="entity" hidden><span></span><select></select></label>
    <label data-field="title"><span></span><input type="text"></label>
    <label data-field="show_diagnostics" class="check"><input type="checkbox"><span></span></label>
    <label data-field="show_summary" class="check"><input type="checkbox"><span></span></label>`;
    this.shadowRoot.addEventListener("change", event => {
      const field = event.target.closest("label")?.dataset.field;
      if (!field) return;
      this._config = {...this._config, [field]: field.startsWith("show_") ? event.target.checked : event.target.value};
      if (field === "master_uid") delete this._config.module_uid;
      this.dispatchEvent(new CustomEvent("config-changed", {detail: {config: this._config}, bubbles: true, composed: true}));
      this._update();
    });
  }
  setConfig(config) { this._config = {...config}; this._signature = null; this._update(); }
  set hass(hass) { this._hass = hass; this._update(); }
  _update() {
    if (!this._config || !this._hass) return;
    const alarmCard = this._config.type === "custom:ofe-alarms-card";
    this.shadowRoot.querySelector('[data-field="entity"]').hidden = !alarmCard;
    for (const field of ["master_uid", "show_diagnostics", "show_summary"]) this.shadowRoot.querySelector(`[data-field="${field}"]`).hidden = alarmCard;
    if (alarmCard) {
      const text = words[language(this._hass)];
      this.shadowRoot.querySelector('[data-field="module_uid"]').hidden = true;
      this.shadowRoot.querySelector('[data-field="entity"] span').textContent = text.alarmSensor;
      this.shadowRoot.querySelector('[data-field="title"] span').textContent = text.title;
      this.shadowRoot.querySelector('input[type="text"]').value = this._config.title || "";
      const options = Object.values(this._hass.states).filter(s => s.entity_id.startsWith("sensor.") && Array.isArray(s.attributes?.alarms))
        .map(s => [s.entity_id, `${s.attributes.friendly_name || s.attributes.ofe_name || s.entity_id} (${s.entity_id})`]);
      if (this._config.entity && !options.some(([id]) => id === this._config.entity)) options.push([this._config.entity, this._config.entity]);
      const signature = JSON.stringify([options, this._config, language(this._hass)]);
      if (signature === this._signature) return;
      this._signature = signature;
      const select = this.shadowRoot.querySelector('[data-field="entity"] select');
      select.replaceChildren(...[["", text.chooseAlarm], ...options].map(([id, label]) => {
        const option = document.createElement("option"); option.value = id; option.textContent = label; return option;
      }));
      select.value = this._config.entity || "";
      return;
    }
    const rows = entities(this._hass), hubs = masters(rows), text = words[language(this._hass)];
    const master = this._config.master_uid || (hubs.length === 1 ? hubs[0] : null);
    const mods = devices(rows, master).filter(([uid]) => uid !== master);
    const signature = JSON.stringify([hubs, mods, this._config, language(this._hass)]);
    if (signature === this._signature) return;
    this._signature = signature;
    for (const [field, label] of [["master_uid", text.master], ["module_uid", text.module], ["title", text.title], ["show_diagnostics", text.diagnostic], ["show_summary", text.summary]]) {
      this.shadowRoot.querySelector(`[data-field="${field}"] span`).textContent = label;
    }
    this.shadowRoot.querySelector('[data-field="module_uid"]').hidden = this._config.type !== "custom:ofe-module-card";
    for (const [field, options] of [["master_uid", hubs.map(uid => [uid, `${text.master} ${uid}`])], ["module_uid", mods]]) {
      const select = this.shadowRoot.querySelector(`[data-field="${field}"] select`);
      select.replaceChildren(...[["", text.auto], ...options].map(([value, label]) => {
        const option = document.createElement("option"); option.value = value; option.textContent = label; return option;
      }));
      select.value = this._config[field] || "";
    }
    this.shadowRoot.querySelector('input[type="text"]').value = this._config.title || "";
    this.shadowRoot.querySelector('[data-field="show_diagnostics"] input').checked = !!this._config.show_diagnostics;
    this.shadowRoot.querySelector('[data-field="show_summary"] input').checked = this._config.show_summary !== false;
  }
}
if (!customElements.get("ofe-card-editor")) customElements.define("ofe-card-editor", OfeCardEditor);
if (!customElements.get("ofe-alarms-card")) customElements.define("ofe-alarms-card", OfeAlarmsCard);
window.customCards ||= [];
if (!window.customCards.some(card => card.type === "ofe-alarms-card")) {
  window.customCards.push({type: "ofe-alarms-card", name: "OFE Alarms", description: "Alarm list from MQTT or the direct integration", preview: true});
}
for (const [name, label, description] of [
  ["master", "OFE Master", "Extraction status and master controls"],
  ["module", "OFE Module", "Module controls, filters and JBC ports"],
  ["overview", "OFE Overview", "Master and all modules, automatically grouped"],
]) {
  if (!customElements.get(`ofe-${name}-card`)) customElements.define(`ofe-${name}-card`, class extends OfeCard {});
  window.customCards ||= [];
  if (!window.customCards.some(card => card.type === `ofe-${name}-card`)) {
    window.customCards.push({type: `ofe-${name}-card`, name: label, description, preview: true});
  }
}
console.info(`Open Fume Extractor cards ${VERSION}`);
