// Execute the firmware publisher and FAE discovery block with transport stubs.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h'), 'utf8');
const captured = [];
const context = vm.createContext({
  master_device_id: 'test-master', master_extmem_malloc_enabled: false, developer_mode_enabled: true,
  nullptr: null, String, HEX: 16, CAP_JBC_BUS: 1, MODULE_JBC_BUS: 1,
  FAST_FLAG_CONNECTED: 1, MODULE_DISPLAY: 6, MODULE_JBC_USB: 9, MODULE_WELLER_ZERO_SMOG: 5, lang: 'de',
  strcmp: (a, b) => a === b ? 0 : 1, strstr: (a, b) => a.includes(b),
  strncmp: (a, b, n) => a.slice(0, n) === b.slice(0, n) ? 0 : 1,
  json_escape: value => JSON.stringify(value).slice(1, -1),
  mqtt_txt: (de, en) => context.lang === 'de' ? de : en,
  mqtt_module_device_json: () => '{"ids":["test-module"]}',
  mqtt_topic_path: leaf => 'ofe/' + leaf,
  mqtt_discovery_topic: (component, id, suffix) => `homeassistant/${component}/${id}/${suffix}/config`,
  mqtt_manifest_note_entity: () => {},
  cleared: [],
  mqtt_client: {publish: (topic, payload, retain) => {
    if (!payload) context.cleared.push(topic);
    else captured.push({topic, payload: JSON.parse(payload), retain});
    return true;
  }},
  station_type_name: () => 'DDE', bytes_ascii: (_, length) => length ? '84495820260912143910F283' : '',
});
context.mqtt_publish_measured = (...args) => context.mqtt_client.publish(...args);
const translate = text => text
  .replace(/F\(("(?:[^"\\]|\\.)*")\)/g, '$1')
  .replace(/\b(?:const )?(?:String|bool|uint8_t) (\w+)\s*=/g, 'let $1 =')
  .replace(/\.c_str\(\)/g, '')
  .replace(/\.length\(\)/g, '.length')
  .replace('station_device_id.trim();', 'station_device_id = station_device_id.trim();')
  .replace('entity_name.trim();', 'entity_name = entity_name.trim();')
  .replace(/\bname\.startsWith/g, 'name.startsWith')
  .replace(/String\((m\.station_addr|addr), HEX\)/g, '$1.toString(16)')
  .replace(/char station_address\[6\];\s*snprintf\([^;]*;/, 'let station_address = " 0x" + m.station_addr.toString(16).toUpperCase().padStart(2, "0");')
  .replace('hxAddr.toUpperCase();', 'hxAddr = hxAddr.toUpperCase();');
const iconSource = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/HaEntityIcons.h'), 'utf8');
context.HA_ENTITY_ICONS = [...iconSource.matchAll(/\{"([^"]+)", "(mdi:[^"]+)"\}/g)].map(m => ({key: m[1], icon: m[2]}));
const iconFunction = iconSource.match(/static inline const char\* ha_entity_icon\([\s\S]*?\n}/)[0]
  .replace(/^static inline const char\* [\s\S]*?\{/, 'function ha_entity_icon(key, component, unit = null) {')
  .replaceAll('std::', '').replace('key += 7;', 'key = key.slice(7);')
  .replace('for (const auto& entry : HA_ENTITY_ICONS)', 'for (const entry of HA_ENTITY_ICONS)');
vm.runInContext(iconFunction, context);
for (const name of ['mqtt_module_topic', 'mqtt_module_command_leaf']) {
  let helper = source.match(new RegExp(`static String ${name}\\([\\s\\S]*?\\n}`))[0];
  helper = helper.replace(/^static String .*?\{/, `function ${name}(addr, ${name.endsWith('topic') ? 'leaf' : 'action'}) {`)
    .replace(/char path\[\d+\];\s*snprintf\(path, sizeof\(path\), "([^"]+)", addr, ([^;]+)\);/,
      (_, format, fallback) => `let path = ${JSON.stringify(format)}.replace('%02X', addr.toString(16).toUpperCase().padStart(2, '0')).replace('%s', ${fallback});`);
  vm.runInContext(translate(helper), context);
}
let availability = source.match(/static String mqtt_module_availability_condition\([\s\S]*?\n}/)[0];
availability = availability.replace(/^static String .*?\{/, 'function mqtt_module_availability_condition(m, suffix, stateful) {')
  .replace('String condition;', 'let condition = "";');
vm.runInContext(translate(availability), context);
let publisher = source.match(/static void mqtt_publish_module_entity\([\s\S]*?\n}/)[0];
publisher = publisher.replace(/^static void [\s\S]*?\{/, 'function mqtt_publish_module_entity(m, component, suffix, name, value_template, extra_json = null, command_leaf = "", stateful = true) {')
  .replace(/char full_suffix\[\d+\];\s*snprintf\([^;]*;/, 'let full_suffix = "mod_" + m.addr.toString(16).toUpperCase().padStart(2, "0") + "_" + suffix;')
  .replace('String cmd_topic;', 'let cmd_topic = "";')
  .replace(/OfePsramTextBuffer& payload = [\s\S]*?payload\.reserveInternal\(1536UL\);/, 'let payload = "";');
publisher = publisher.replace(/const char\* unit =/g, 'let unit =');
vm.runInContext(translate(publisher), context);
const begin = source.indexOf('static void mqtt_publish_module_discovery(');
const end = source.indexOf('  if (m.type == MODULE_JBC_USB', begin);
let discovery = source.slice(begin, end) + '\n}';
discovery = discovery.replace(/^static void .*?\{/, 'function discovery(m) {');
vm.runInContext(translate(discovery), context);
// Execute the new reboot branch, including its online/update guards.
const commandStart = source.indexOf('static bool mqtt_apply_module_command(');
const guardStart = source.indexOf('  if (!m || !m->online)', commandStart);
const rebootEnd = source.indexOf('  } else if (action.startsWith("entity_"))', guardStart);
const guard = source.slice(guardStart, rebootEnd).replace(/  if \(action.startsWith\("jbc_cfg_"\)\) \{[\s\S]*?\n  }\n/, '').replace('m->online', 'm.online');
let queued = 0;
context.master_cmd_module_reboot = () => { queued++; };
for (const online of [false, true]) for (const busy of [false, true]) for (const target of [16, 32]) for (const value of ['RESTART', 'invalid']) {
  context.scheduler = {moduleFirmwareUpdateActive: () => busy, moduleFirmwareUpdateTarget: () => target};
  context.m = {online}; context.addr = 16; context.action = 'reboot'; context.value = value;
  queued = 0;
  vm.runInContext('(function() {' + guard + '\n} return true; })()', context);
  assert.equal(queued, Number(online && !(busy && target === 16) && value === 'RESTART'));
}
const faeStart = source.indexOf('    const bool linked = m.station_addr &&');
const faeEnd = source.indexOf('\n  }', faeStart);
const faeState = translate(source.slice(faeStart, faeEnd));
const results = [];
for (const lang of ['de', 'en']) {
  context.lang = lang;
  captured.length = 0;
  context.discovery({addr: 16, caps: 1, type: 1});
  const states = [];
  for (const connected of [false, true]) for (const address of [0, 18]) for (const work of [0, 1]) for (const cached of [false, true]) {
    context.m = {online: true, station_addr: address, jbc_link_flags: connected ? 1 : 0,
      jbc_addr: 16, jbc_work_mask: work, jbc_stand_mask: 1,
      jbc_filter_life: 1, jbc_filter_sat: 2, jbc_stat_error: 0, jbc_device_id: [], jbc_device_id_len: cached ? 22 : 0};
    context.json = '{"probe":0';
    vm.runInContext('{' + faeState + '}', context);
    states.push({connected, address, work, cached, state: JSON.parse(context.json + '}')});
  }
  results.push({lang, discovery: [...captured], states});
}
process.stdout.write(JSON.stringify(results));
