// Run from project root: node tests/status_web_test.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const src = fs.readFileSync(path.join(root, 'src/WebStatus.inc.h'), 'utf8');
const catalogueHeader = fs.readFileSync(path.join(root, 'src/WebAutomationSignals.h'), 'utf8');
const script = src.split('<script>')[1].split('</script>')[0];
const automation = catalogueHeader.split('R"HTML(')[1].split(')HTML"')[0].replace('<script>', '').replace('</script>', '');
new vm.Script(script); // Full status page JS must be valid syntax.
new vm.Script(automation); // Shared signal catalogue JS must be valid syntax.
const sandbox = {console, uiLang: 'de', hx: n => '0x'+Number(n).toString(16).toUpperCase()};
vm.createContext(sandbox);
vm.runInContext(automation, sandbox);

// Extract only the actual named functions, without executing page-onload code.
function exposeFunction(name) {
  const line = script.split('\n').find(s => s.startsWith(`function ${name}(`) || s.startsWith(`async function ${name}(`));
  assert.ok(line, `Missing status function ${name}`);
  const first = line.indexOf(`function ${name}(`);
  const start = line.startsWith('async function ') ? line.indexOf('async function ') : first;
  const brace = line.indexOf('{', first);
  let depth = 0, quote = '', escaped = false, end = -1;
  for (let i = brace; i < line.length; ++i) {
    const c = line[i];
    if (quote) {
      if (escaped) escaped = false;
      else if (c === '\\') escaped = true;
      else if (c === quote) quote = '';
    } else if (c === '`' || c === '"' || c === "'") quote = c;
    else if (c === '{') ++depth;
    else if (c === '}' && --depth === 0) { end = i; break; }
  }
  assert.ok(end !== -1, `Cannot extract ${name}`);
  vm.runInContext(line.slice(start, end+1), sandbox);
}
exposeFunction('addRule');
exposeFunction('faultText');
exposeFunction('jbcFaeStandActive');

const fault = sandbox.faultText(0x0102, 3);
assert.match(fault, /Filterwarnung|Filter warn/);
assert.match(fault, /Drehzahlrückmeldung|speed feedback/);
const bothFilters = sandbox.faultText(0x0006, 3);
assert.match(bothFilters, /Filterwarnung|Filter warn/);
assert.match(bothFilters, /Filter voll|Filter full/);
assert.equal(sandbox.jbcFaeStandActive({online:true,station_addr:0x12,jbc_link_flags:1,jbc_work_mask:0}), true);

const state = {modules: [], system_inputs_mask:(1<<8), preferred_output_addr:0, input_rules:[{enabled:false}]};
// The module stays selected while extractor output is OFF, but is not active.
const selectedMask = {modules:[{addr:32,system_inputs_mask:(1<<9)}]};
assert.equal(sandbox.ofeAutomationInputInfo(selectedMask, 'state:system:9:32').on,true);
assert.equal(sandbox.ofeAutomationInputInfo(selectedMask, 'state:system:10:32').on,false);
selectedMask.modules[0].system_inputs_mask |= (1<<10);
assert.equal(sandbox.ofeAutomationInputInfo(selectedMask, 'state:system:10:32').on,true);

// Validate the real boolean-editor live indicator, independently of the C++
// routing path: it used to call an assigned output "Active" even when OFF.
const logicHeader = fs.readFileSync(path.join(root, 'src/WebLogic.inc.h'), 'utf8');
const logicSource = logicHeader.split('\n').find(s => s.startsWith('function liveSignalInfo('));
assert.ok(logicSource, 'Missing liveSignalInfo');
const logicVm = {liveState: {preferred_output_addr:32, active_output_addr:32, output_enabled:false}, ofeAutomationInputInfo:()=>null};
vm.createContext(logicVm);
vm.runInContext(logicSource, logicVm);
assert.equal(logicVm.liveSignalInfo('state:output:selected:32').on, true);
assert.equal(logicVm.liveSignalInfo('state:output:active:32').on, false);
logicVm.liveState.output_enabled=true;
assert.equal(logicVm.liveSignalInfo('state:output:selected:32').on, true);
assert.equal(logicVm.liveSignalInfo('state:output:active:32').on, true);
logicVm.liveState.preferred_output_addr=0;
assert.equal(logicVm.liveSignalInfo('state:output:selected:auto').on, true);
assert.equal(logicVm.liveSignalInfo('state:output:selected:32').on, false);
assert.equal(logicVm.liveSignalInfo('state:output:active:32').on, true);
logicVm.liveState.active_output_addr=0;
assert.equal(logicVm.liveSignalInfo('state:output:active:32').on, false);
// Ensure boolean runtime uses the same evaluator as routing.
assert.match(logicHeader, /systemInputActive\(MasterScheduler::INPUT_STATE_OUTPUT_SELECTED, uint8_t\(addr\)\)/);
assert.match(logicHeader, /systemInputActive\(MasterScheduler::INPUT_STATE_OUTPUT_ACTIVE, uint8_t\(addr\)\)/);

async function testAddRule(outputValue) {
  const urls=[];
  sandbox.window = {lastState:state};
  sandbox.document = {getElementById(id) { return {value:id==='main_input_sel'?'1,16,0':String(outputValue)}; }};
  sandbox.dec = s=>{const [t,a,b]=String(s).split(',').map(Number); return {t,a,b};};
  sandbox.fetch = async url=>{ urls.push(url); return {ok:true}; };
  sandbox.routingSaveOk = async r=>r.ok;
  sandbox.setTimeout = ()=>{};
  sandbox.load = ()=>{};
  await sandbox.addRule();
  assert.equal(urls.length,1);
  const qs = new URL(urls[0], 'http://unit.test').searchParams;
  assert.equal(qs.get('tt'), '1');
  assert.equal(qs.get('ta'), '0');
  assert.equal(qs.get('tb'), '0');
}
Promise.resolve().then(async()=>{
  await testAddRule(0);
  await testAddRule(32); // Regression: used to incorrectly create IO target at 0x20.
  console.log('Status web JS: all tests passed');
}).catch(e=>{console.error(e);process.exitCode=1;});
