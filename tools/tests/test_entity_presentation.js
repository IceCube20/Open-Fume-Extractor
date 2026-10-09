'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { execFileSync } = require('node:child_process');

const src = path.resolve(__dirname, '../../OpenFumeExtractorMaster/src');
const status = fs.readFileSync(path.join(src, 'WebStatus.inc.h'), 'utf8');
const mqtt = fs.readFileSync(path.join(src, 'MasterMqtt.inc.h'), 'utf8');
const scheduler = fs.readFileSync(path.join(src, 'MasterScheduler.cpp'), 'utf8');
const executable = process.argv[2] || path.join(process.env.TEMP, 'ofe-entity-presentation-native/test_entity.exe');
const fixtures = JSON.parse(execFileSync(executable, ['--fixtures'], { encoding: 'utf8' }));

function lineFunction(name) {
  const line = status.split(/\r?\n/).find(line => line.startsWith(`function ${name}(`));
  assert.ok(line, name);
  return line;
}
const normalize = status.slice(status.indexOf('function universalNormalizeDef('), status.indexOf('function universalEnsureProfileControls('));
const functions = ['universalLines', 'universalNormalizeDescriptorText', 'universalParseDescriptorRaw',
  'universalList', 'universalAccessMode', 'universalReadable', 'universalWritable',
  'universalNumberMeta', 'universalSelectPairs', 'universalAsciiNumber', 'universalRawValue',
  'universalSelectLabel', 'universalEntityValue', 'universalUpdateEntity'].map(lineFunction).join('\n');
const elements = new Map();
const context = vm.createContext({
  uiLang: 'en', fixText: value => String(value ?? ''), t: key => key.toUpperCase(),
  document: { getElementById: id => elements.get(id) || null },
  set: (id, text) => { const el = elements.get(id); if (el) el.textContent = text; },
  val: (el, value) => { if (el) el.value = value; },
  fieldBusy: el => !!el?.busy, universalWoSwitchTarget: {}, universalWoSwitchVisual: () => {},
});
vm.runInContext(normalize + '\n' + functions, context);
let cases = 0;
for (const lang of ['en', 'de']) {
  context.uiLang = lang;
  for (const fixture of fixtures) {
    const def = context.universalParseDescriptorRaw(fixture.line, true)[0];
    assert.ok(def);
    assert.equal(def.label, fixture[`label_${lang}`]);
    assert.equal(context.universalEntityValue(def, fixture.state), fixture.expected);
    if (def.type !== 'switch' && def.type !== 'sensor' && def.type !== 'number') {
      assert.equal(context.universalEntityValue(def, fixture.state), fixture.display_wire);
    }
    const module = { addr: 96 }, prefix = `uni_ent_`, suffix = `96_${def.id}`;
    const value = { textContent: '' }, input = { value: 'unsaved', busy: false },
      select = { value: 'unsaved', busy: false }, button = { dataset: {} };
    elements.clear();
    elements.set(prefix + 'val_' + suffix, value);
    elements.set(prefix + 'in_' + suffix, input);
    elements.set(prefix + 'sel_' + suffix, select);
    elements.set(prefix + 'sw_' + suffix, button);
    context.universalUpdateEntity(module, def, fixture.state);
    assert.equal(value.textContent, fixture.expected);
    if (def.type === 'number') assert.equal(input.value, fixture.state.ascii);
    if (def.type === 'select') {
      assert.equal(select.value, '960');
      const changed = { ...fixture.state, ascii: '3d 0h 0m', display_text: '3 days', selected_value: '432' };
      context.universalUpdateEntity(module, def, changed);
      assert.equal(select.value, '432'); assert.equal(value.textContent, '3 days');
      select.busy = true; select.value = '48';
      context.universalUpdateEntity(module, def, fixture.state);
      assert.equal(select.value, '48', 'polling must preserve an edited select');
    }
    if (def.type === 'number') {
      input.busy = true; input.value = '77';
      context.universalUpdateEntity(module, def, fixture.state);
      assert.equal(input.value, '77', 'polling must preserve an edited slider');
    }
    if (def.type === 'switch') { assert.equal(button.className, 'switch uni-switch off'); assert.equal(button.dataset.next, '1'); }
    ++cases;
  }
}
// Source contracts: callers use the compiled core, not local transform copies.
assert.ok(status.includes('ofe_entity::rawText(e.data, data_len, raw, sizeof(raw),'));
assert.ok(status.includes('has_definition ? ofe_entity::valueEncoding(definition)'));
assert.ok(mqtt.includes('has_def ? ofe_entity::valueEncoding(def)'));
assert.ok(status.includes('entity_display_string(definition, raw)'));
assert.ok(mqtt.includes('return entity_display_string(def, raw.c_str())'));
assert.ok(mqtt.includes('ofe_entity::label(def, strcmp(web_lang, "de") == 0)'));
assert.ok(scheduler.includes('ofe_entity::displayText(def, ofe_entity::text(raw), out, out_len)'));
assert.ok(scheduler.includes('ofe_entity::label(definition, german)'));
for (const obsolete of ['descriptor_time_value_text', 'descriptor_scaled_value_text', 'descriptor_map_text_for_state']) assert.ok(!scheduler.includes(obsolete));
assert.ok(!mqtt.includes('mqtt_universal_duration_minutes'));
assert.ok(!mqtt.includes('mqtt_descriptor_next_meta'));
console.log(`Shared entity presentation: ${cases} DE/EN renderer cases, live select/slider checks passed`);
