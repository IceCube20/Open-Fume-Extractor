'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/WebStatus.inc.h'), 'utf8');
const lines = source.split(/\r?\n/);
const context = vm.createContext({
  uiLang: 'de', fixText: String, t: key => key,
  universalDefsForModule: module => module.defs || [],
  universalDescriptorCache: {},
  amn: module => `Module ${module.addr}`,
  escAttr: String, escHtml: String,
  universalOutputDefs: () => [], fanIoMainConfigured: () => false,
  ruleLabel: () => 'Previously selected input',
});
const shared = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/WebAutomationSignals.h'), 'utf8');
vm.runInContext(shared.match(/<script>([\s\S]*?)<\/script>/)[1], context);
for (const name of ['universalLines', 'universalNormalizeDescriptorText', 'universalParseDescriptorRaw',
  'universalInputDefs', 'universalIsMainInput', 'universalMainInputDefs', 'fanIoDef',
  'fanIoRouteInputs', 'ioInputLabel', 'universalEntityControlLabel', 'dec', 'automationRuleOptions', 'routingSignals', 'optionHas', 'sourceOptions']) {
  const line = lines.find(line => line.startsWith(`function ${name}(`));
  assert.ok(line, name);
  vm.runInContext(line, context);
}
vm.runInContext(source.slice(source.indexOf('function mainInputAvailable('), source.indexOf('function dhm(')), context);

const descriptor = [
  '19 binary_sensor system ro role=main_input',
  '20 binary_sensor configured ro role=main_input',
  '21 binary_sensor unassigned ro',
  '22 switch readable rw role=input',
  '23 switch output rw role=main_output_enable',
  '24 number analog ro role=main_input',
  '25 binary_sensor writeonly wo role=main_input',
  '26 binary_sensor uppercase ro role=MAIN_INPUT',
  '27 binary_sensor configured8 ro role=main_input',
  '28 binary_sensor extra ro role=main_input',
].join('\n');
const values = html => [...html.matchAll(/<option value="([^"]+)"([^>]*)>/g)]
  .filter(match => !match[2].includes('disabled')).map(match => match[1]);
let checks = 0;
for (const language of ['de', 'en']) {
  context.uiLang = language;
  for (const type of [2, 3, 7, 8]) {
    const module = {addr: 32, type, online: true, caps: 65536 | 2048,
      universal_descriptor: descriptor, defs: context.universalParseDescriptorRaw(descriptor,true)};
    const state = {modules: [module], jbc_inputs_count: 0};
    const main = values(context.sourceOptions(state, '0,0,0', false));
    for (const id of [20, 22, 26, 27]) assert.ok(main.includes(`3,32,${id}`), `${type}: missing ${id}`);
    for (const id of [19, 21, 23, 24, 25]) assert.ok(!main.includes(`3,32,${id}`), `${type}: unexpected ${id}`);
    assert.equal(main.includes('3,32,28'), type === 7 || type === 8, 'Fan/IO has at most eight physical inputs');
    const rules = values(context.sourceOptions(state, '0,0,0'));
    for (const id of [20, 21, 22, 23, 26, 27]) assert.ok(rules.includes(`3,32,${id}`), `Rule source ${id} must remain usable`);
    assert.ok(rules.includes('4,0,1'), 'System states remain available to rules');
    assert.ok(!main.includes('4,0,1'), 'System states are not main inputs');
    const selected = context.sourceOptions(state, '3,32,21', false);
    assert.match(selected, /value="3,32,21" selected disabled/, 'Keep old selection visible but not selectable');
    assert.ok(!values(selected).includes('3,32,21'));
    for (const [id, expected] of [[20, true], [21, false], [23, false]]) {
      assert.equal(context.mainInputAvailable({...state, main_input_source_type: 3,
        main_input_source_addr: 32, main_input_source_bit: id}), expected);
    }
    module.online = false;
    assert.ok(!values(context.sourceOptions(state, '3,32,20', false)).includes('3,32,20'));
    ++checks;
  }
}
const legacy = {modules: [{addr: 48, type: 4, online: true, caps: 2048}], jbc_inputs_count: 0};
assert.deepEqual(values(context.sourceOptions(legacy, '0,0,0', false)), ['0,0,0', '2,48,0', '2,48,1']);
const jbc = {modules: [{addr: 16, type: 1, online: true, caps: 1},
  {addr: 17, type: 9, online: true, caps: 16777216}], jbc_inputs_count: 2};
assert.deepEqual(values(context.sourceOptions(jbc, '1,0,0', false)), ['0,0,0', '1,0,0', '1,16,0', '1,17,0']);
console.log(`PASS: main input roles and unrestricted rule sources, ${checks} module/language fixtures plus JBC and legacy inputs`);
