// Audit discovery branches, using parsed descriptor fixtures and transport stubs.
// This does not emulate UART, descriptor parsing, or a running Home Assistant.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const scaffold = fs.readFileSync(path.join(__dirname, 'capture_mqtt_fae.cjs'), 'utf8');
const bootstrap = scaffold.slice(0, scaffold.indexOf("const begin = source.indexOf('static void mqtt_publish_module_discovery(')"));
const {source, context, translate, captured} = vm.runInNewContext(
  bootstrap + '\n({source, context, translate, captured})', {require, __dirname});
const bus = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/bus/Rs485PeripheralBus.h'), 'utf8');
for (const match of bus.matchAll(/\b(MODULE_\w+)\s*=\s*(0x[\dA-Fa-f]+)/g)) context[match[1]] = Number(match[2]);
for (const match of bus.matchAll(/\b(CAP_\w+)\s*=\s*1UL\s*<<\s*(\d+)/g)) context[match[1]] = 2 ** Number(match[2]);
for (const family of ['SOLD', 'HA', 'CL', 'PH', 'FE', 'SF']) context['JBC_USB_CORE_' + family] = family;
const counters = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/JbcUsbHaCounters.h'), 'utf8');
context.HA_JBC_COUNTERS = [...counters.matchAll(/HA_JBC_COUNTER\((\w+), (\w+), (0x[\da-fA-F]+)U, (\w+), "([^"]+)", "([^"]+)", ("min"|nullptr)\)/g)]
  .map((m) => ({key: m[1], flags: m[2], mask: Number(m[3]), family: m[4], de: m[5], en: m[6], unit: m[7] === '"min"' ? 'min' : null}));
context.HA_JBC_COUNTER_COUNT = context.HA_JBC_COUNTERS.length;
context.ha_jbc_counter_supported = (port, family, def) => family === def.family && ((port?.[def.flags] || 0) & def.mask) === def.mask;
Object.assign(context, {
  HA_JBC_CONTROL_COUNT: 69,
  ha_jbc_control: (m, id, _german, out) => {
    const item = m.core?.controls?.find(item => item.id === id);
    if (!item) return false;
    Object.assign(out, {...item, component: item.platform, minimum: item.min, maximum: item.max});
    return true;
  },
  duration_text_minutes: minutes => {
    const days = Math.floor(minutes / 1440), hours = Math.floor(minutes % 1440 / 60), mins = minutes % 60;
    return [days && `${days}d`, hours && `${hours}h`, mins && `${mins}m`].filter(Boolean).join(' ') || '0m';
  },
  mqtt_ha_discovery: true, mqtt_device_json: () => '{"ids":["test-master"]}',
  mqtt_clear_jbc_usb_incompatible_discovery: () => {}, mqtt_clear_module_entity: () => {},
  jbc_usb_core_state: m => m.core,
  mqtt_module_has_descriptor: m => !!m.universal_descriptor_valid,
  mqtt_fanio_filter_mode: m => m.fanio_filter_runtime_valid ? m.fanio_filter_mode : 0,
  mqtt_ascii_ci_eq: (a, b) => String(a).toLowerCase() === String(b).toLowerCase(),
  mqtt_universal_entity_readable: def => def.mode.includes('r'),
  mqtt_universal_entity_writable: def => def.mode.includes('w'),
  mqtt_universal_role_matches: (def, a, b) => def.role === a || def.role === b,
  mqtt_descriptor_meta: (line, key) => String(line.meta?.[key] ?? ''),
  mqtt_parse_universal_descriptor_line: (line, def) => {
    Object.assign(def, {id: line.id, type: line.type, mode: line.meta?.access || line.mode || 'ro',
      key: line.key, label: line.label, role: line.meta?.role || '', unit: line.meta?.unit || '',
      options: line.meta?.options || '', values: line.meta?.values || '',
      min_value: String(line.meta?.min ?? ''), max_value: String(line.meta?.max ?? ''), step: String(line.meta?.step ?? ''),
      time_base: line.meta?.time_base || line.meta?.tb || '', time_display: line.meta?.time_display || line.meta?.tf || ''});
    return true;
  },
  mqtt_main_input_options_extra: () => ',"options":["No input"],"icon":"mdi:source-branch"',
  mqtt_main_output_options_extra: () => ',"options":["Auto"],"icon":"mdi:fan"',
  web_is_german: () => context.lang === 'de',
  scheduler: {activeOutputMinSelectFlow: () => 100, activeOutputMaxSelectFlow: () => 1000},
});
context.mqtt_client.connected = () => true;
const adapt = text => translate(text)
  .replace(/\b(?:const )?(?:uint16_t|int|JbcUsbCoreState) (\w+)\s*=/g, 'let $1 =')
  .replace(/\bString (\w+);/g, 'let $1 = "";')
  .replace(/const HaJbcCounterDef& (\w+) =/g, 'let $1 =')
  .replace(/HaJbcControl (\w+);/g, 'let $1 = {};')
  .replace(/\bsize_t (\w+) =/g, 'let $1 =')
  .replace(/const char\* (\w+)\s*=/g, 'let $1 =')
  .replace(/\((?:uint8_t|uint16_t)\)/g, '')
  .replace(/\b(\d+)U\b/g, '$1')
  .replace('opts.replace(";", "|");', 'opts = opts.replaceAll(";", "|");')
  .replace('item.trim();', 'item = item.trim();');
function load(name, args, rewrite = text => text) {
  const fn = source.match(new RegExp(`static (?:String|bool|void) ${name}\\([\\s\\S]*?\\n}`))[0];
  vm.runInContext(adapt(rewrite(fn.replace(/^static [\s\S]*?\{/, `function ${name}(${args}) {`))), context);
}
load('mqtt_module_has_fanio_filter_telemetry', 'm');
load('mqtt_alias_or', 'alias, fallback');
load('mqtt_jbc_port_suffix', 'port, leaf');
load('mqtt_jbc_port_name', 'base, port, de, en');
load('mqtt_publish_jbc_usb_control_discovery', 'm', text => text
  .replace('char suffix[20], tpl[128];', 'let suffix, tpl;')
  .replace(/snprintf\(suffix, sizeof\(suffix\), "jbc_cfg_%u", id\);/, 'suffix = "jbc_cfg_" + id;')
  .replace(/snprintf\(tpl, sizeof\(tpl\), "([^"]+)", suffix\);/g, (_, format) => `tpl = ${JSON.stringify(format)}.replace('%s', suffix);`));
load('mqtt_publish_jbc_usb_core_discovery', 'm, base');
load('mqtt_universal_entity_extra', 'def, component');
load('mqtt_publish_universal_descriptor_entities', 'm, base', text => text
  .replace('bool seen_entity_id[256] = {false};', 'let seen_entity_id = new Array(256).fill(false);')
  .replace(/const char\* p = m\.universal_descriptor;[\s\S]*?MqttUniversalEntityDef def;/,
    'let p = m.definitions; while (p && p.length) { let next = p.length > 1 ? p.slice(1) : null; let line = p[0]; let def = {};')
  .replaceAll('p = next ? next + 1 : nullptr;', 'p = next;')
  .replace(/char suffix\[16\];\s*snprintf\([^;]*;/, 'let suffix = "entity_" + def.id;')
  .replace(/char legacy_on\[24\];\s*char legacy_off\[24\];\s*snprintf\([^;]*;\s*snprintf\([^;]*;/,
    'let legacy_on = "entity_" + def.id + "_on"; let legacy_off = "entity_" + def.id + "_off";'));
load('mqtt_publish_module_discovery', 'm');
const buffer = text => text.replace(/OfePsramTextBuffer& payload = [\s\S]*?payload\.reserveInternal\(1536UL\);/, 'let payload = "";');
load('mqtt_publish_discovery_entity', 'component, suffix, name, value_template, unit = null, device_class = null, state_class = null, binary = false', buffer);
load('mqtt_publish_discovery_control', 'component, suffix, name, value_template, command_leaf, extra_json', buffer);
const masterBegin = source.indexOf('  mqtt_publish_discovery_entity("binary_sensor", "output"');
const masterEnd = source.indexOf('  uint8_t order[ModuleRegistry::MAX_MODULES];', masterBegin);
vm.runInContext(adapt('function masterDiscovery() {\n' + source.slice(masterBegin, masterEnd) + '\n}'), context);
const cases = JSON.parse(fs.readFileSync(0, 'utf8'));
const results = [];
for (const fixture of cases) {
  context.lang = fixture.lang;
  context.developer_mode_enabled = fixture.developer_mode !== false;
  captured.length = 0;
  if (fixture.master) context.masterDiscovery();
  else context.mqtt_publish_module_discovery(fixture.module);
  results.push({id: fixture.id, discovery: [...captured]});
}
process.stdout.write(JSON.stringify(results));
