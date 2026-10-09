// Run the firmware's real renderer with a minimal DOM, in both UI languages.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/WebDiagnostics.inc.h'), 'utf8');
const script = source.split('html += R"JS(')[1].split(')JS";')[0].replace('</script>', '');
new vm.Script(script);
// Exercise the actual C++ append block after translating its small loop syntax.
const lengthStart = source.indexOf('const jbc_rs485::LengthErrorSnapshot& error = bd.last_timeout_length_error;');
const lengthEnd = source.indexOf('\n        }\n        if (probe.bad_version)', lengthStart);
assert.ok(lengthStart >= 0 && lengthEnd > lengthStart);
const serializer = source.slice(lengthStart, lengthEnd)
  .replace('const jbc_rs485::LengthErrorSnapshot& error = bd.last_timeout_length_error;', 'const error = bd.last_timeout_length_error;')
  .replace(/F\(("(?:[^"\\]|\\.)*")\)/g, '$1')
  .replace(/uint8_t hi/g, 'let hi')
  .replace(/sizeof\(error\.(header|second_header)\)/g, 'error.$1.length');
for (const tailPresent of [true, false]) {
  const error = {actual_length: 34, expected_length: 17, header: [1,1,32,157,180,9],
    first_crc_checked: true, first_crc_valid: false, remaining_length: 17,
    second_expected_length: 17, second_crc_checked: true, second_crc_valid: true,
    second_header_present: tailPresent, second_header: [1,1,32,158,180,9]};
  const state = vm.createContext({json: '{"probe":0', bd: {last_timeout_length_error: error}});
  vm.runInContext(serializer, state);
  const result = JSON.parse(state.json + '}').length_error;
  assert.equal(result.actual, 34);
  assert.equal(result.split.first_checked, 1);
  assert.equal(result.split.first_valid, 0);
  assert.equal(result.split.second_valid, 1);
  assert.deepEqual(result.split.header, tailPresent ? error.second_header : []);
}
const renderer = script.slice(script.indexOf('let diagModuleCardSignature='), script.indexOf('function jbcUsbLinkStateName'));
for (const de of [true, false]) {
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, {innerHTML: '', className: ''});
    return elements.get(id);
  };
  const context = vm.createContext({
    document: {getElementById: element}, prevDiag: null,
    u: (german, english) => de ? german : english,
    esc: value => String(value), addrText: value => String(value),
    modulePrevByAddr: () => null, fmtBytes: value => String(value),
    diagModuleVisual: () => ({cls: '', icon: '', sub: ''}),
    diagApplyLedSnapshot: () => {}, diagSproutGlyph: () => '<svg></svg>',
  });
  vm.runInContext(renderer, context);
  const render = (online, state) => {
    context.diagRenderModules({modules: [{addr: 16, online, device_link_state: state, device_bus: 'JBC P02'}]}, 1);
    return element('diag_dynamic_16').innerHTML.split('diag-device-section')[1];
  };
  assert.match(render(true, 1), new RegExp('>' + (de ? 'bereit' : 'ready') + '<'));
  assert.match(render(true, 0), /is-warning/);
  assert.match(render(true, 0), new RegExp('>' + (de ? 'getrennt' : 'disconnected') + '<'));
  assert.match(render(true, -1), new RegExp('>' + (de ? 'unbekannt' : 'unknown') + '<'));
  assert.match(render(true, undefined), new RegExp('>' + (de ? 'unbekannt' : 'unknown') + '<'));
  assert.match(render(false, 1), /is-offline/);
  assert.match(render(false, 1), />offline</);
  assert.match(render(true, 1), /is-online/);
  context.diagRenderModules({modules: [{addr: 32, online: true, requests: 100,
    timeouts: 2, update_requests: 70, update_timeouts: 5,
    recovery_requests: 10, recovery_timeouts: 3}]}, 1);
  const maintenance = element('diag_dynamic_32').innerHTML;
  assert.match(maintenance, /10\.0%/);
  assert.match(maintenance, /5 \/ 70/);
  assert.match(maintenance, /3 \/ 10/);
  assert.match(maintenance, new RegExp(de ? 'Update-Timeouts' : 'Update timeouts'));
  assert.match(maintenance, new RegExp(de ? 'Wiederanlauf-Timeouts' : 'Restart wait timeouts'));
  assert.equal(context.diagTimeoutProbeHtml({}), '');
  const sample = {
    addr: 32, online: true, timeout_io: 12, timeouts: 12,
    last_timeout_name: 'GET_IO',
    io_timeouts: {get: 7, set: 3, other: 2},
    timeout_probe: {
      budget_ms: 70, elapsed_us: 81000, max_poll_gap_us: 79500,
      rx_frames: 2, crc: 1, len: 3, version: 0, escape: 0, overflow: 0, short: 0,
      bad_seq: 1, bad_cmd: 2, late: 3,
      request_seq: 0x3C,
      length_error: {actual: 15, expected: 17, header: [1, 1, 32, 60, 180, 9]},
    },
  };
  const detail = context.diagTimeoutProbeHtml(sample);
  assert.match(detail, /GET_IO 7/);
  assert.match(detail, /SET_IO 3/);
  assert.match(detail, /70 ms/);
  assert.match(detail, /81\.0 ms/);
  assert.match(detail, /79\.5 ms/);
  assert.match(detail, /CRC 1/);
  assert.match(detail, /LEN 3/);
  assert.match(detail, /SEQ 1/);
  assert.match(detail, /CMD 2/);
  assert.match(detail, /Late 3/);
  assert.match(detail, /15 B/);
  assert.match(detail, /17 B/);
  assert.match(detail, /SRC 0x20/);
  assert.match(detail, /SEQ 0x3C/);
  assert.match(detail, /CMD 0xB4/);
  assert.match(detail, /LEN 9/);
  assert.match(detail, new RegExp(de ? 'Header des verworfenen Gesamtframes' : 'Header of rejected complete frame'));
  assert.doesNotMatch(detail, /CRC 2/);
  const split = {first_checked: 1, first_valid: 1, remaining: 17, second_expected: 17,
    second_checked: 1, second_valid: 1, header: [1, 1, 32, 157, 180, 9]};
  const renderSplit = s => context.diagTimeoutProbeHtml({timeout_probe: {...sample.timeout_probe,
    length_error: {...sample.timeout_probe.length_error, actual: 34, split: s}}});
  assert.match(renderSplit(split), /34 B/);
  assert.match(renderSplit(split), /SEQ 0x9D/);
  assert.match(renderSplit(split), new RegExp(de ? 'Zwei CRC-gültige' : 'Two CRC-valid'));
  assert.match(renderSplit(split), new RegExp(de ? 'weiterhin verworfen' : 'still rejected'));
  for(const changed of [{second_valid: 0}, {first_valid: 0}, {second_checked: 0},
    {header: [2, 1, 32, 157, 180, 9]}, {header: []}]){
    assert.doesNotMatch(renderSplit({...split,...changed}), /Zwei CRC-gültige|Two CRC-valid/);
  }
  assert.match(renderSplit({...split,first_valid:0}), new RegExp(de?'CRC 1 ungültig':'CRC 1 invalid'));
  assert.match(renderSplit({...split,second_checked:0}), new RegExp(de?'CRC 2 nicht prüfbar':'CRC 2 not checkable'));
  assert.doesNotMatch(context.diagTimeoutProbeHtml({timeout_probe: {...sample.timeout_probe, len: 0}}), /SRC 0x20/);
  assert.doesNotMatch(context.diagTimeoutProbeHtml({timeout_probe: {...sample.timeout_probe, length_error: {header: [1]}}}), /SRC/);
  assert.match(detail, new RegExp(de ? 'Master-Empfangsabstand max' : 'Master receive-service gap max'));
  const expanded = { ...sample, timeout_probe: { ...sample.timeout_probe,
    len: 0, version: 1,
    version_error: {actual: 9, header: [255, 1, 32, 60, 181, 1]},
    last_physical_frame: {header: [1, 32, 1, 60, 53, 4], filtered: true},
    unmatched_frame: {count: 1, reason: 5, network: true, header: [1, 1, 64, 17, 208, 0]},
  }};
  const expandedDetail = context.diagTimeoutProbeHtml(expanded);
  assert.match(expandedDetail, /VERSION/);
  assert.match(expandedDetail, /VER 0xFF/);
  assert.match(expandedDetail, /9 B/);
  assert.match(expandedDetail, new RegExp(de ? 'Protokollbyte erwartet' : 'Expected protocol byte'));
  assert.match(expandedDetail, new RegExp(de ? 'keine CRC-Prüfung' : 'no CRC validation'));
  assert.match(expandedDetail, new RegExp(de ? 'CRC-geprüft' : 'CRC checked'));
  assert.match(expandedDetail, /DST 0x20 · SRC 0x01/);
  assert.match(expandedDetail, new RegExp(de ? 'verworfen' : 'dropped'));
  assert.match(expandedDetail, /WLAN/);
  assert.match(expandedDetail, new RegExp(de ? 'separate Display-Antwort verarbeitet' : 'separate display response handled'));
  assert.match(expandedDetail, /SRC 0x40/);
  for (const [reason, german, english] of [
    [1, 'andere Quell-/Zieladresse', 'different source/destination'],
    [2, 'abweichende SEQ', 'different SEQ'],
    [3, 'abweichender CMD', 'different CMD'],
    [4, 'erkannte verspätete Antwort', 'recognized late response'],
  ]) {
    const probe = {...expanded.timeout_probe,
      last_physical_frame: {...expanded.timeout_probe.last_physical_frame, filtered: false},
      unmatched_frame: {...expanded.timeout_probe.unmatched_frame, reason, network: false},
    };
    const html = context.diagTimeoutProbeHtml({timeout_probe: probe});
    assert.match(html, new RegExp(de ? german : english));
    assert.match(html, new RegExp(de ? 'weitergegeben' : 'forwarded'));
  }
  const noFrames = {timeout_probe: {...expanded.timeout_probe, version: 0, rx_frames: 0,
    unmatched_frame: {...expanded.timeout_probe.unmatched_frame, count: 0}}};
  assert.doesNotMatch(context.diagTimeoutProbeHtml(noFrames), /VER 0xFF|SRC 0x40|Transport filter|Transportfilter/);
  context.diagRenderModules({modules: [expanded]}, 1);
  assert.match(element('diag_dynamic_32').innerHTML, /VER 0xFF/);
  assert.doesNotMatch(context.diagTimeoutProbeHtml({io_timeouts: sample.io_timeouts}), /RS485/);
  context.diagRenderModules({modules: [sample]}, 1);
  assert.match(element('diag_dynamic_32').innerHTML, /79\.5 ms/);
  // A later normal-mode/async response must remove the previous probe display.
  context.diagRenderModules({modules: [{addr: 32, online: true}]}, 1);
  assert.doesNotMatch(element('diag_dynamic_32').innerHTML, /79\.5 ms/);
  assert.doesNotMatch(element('diag_dynamic_32').innerHTML, /SRC 0x20/);
  assert.doesNotMatch(element('diag_dynamic_32').innerHTML, /VER 0xFF|SRC 0x40/);
}
console.log('PASS: diagnostics JS syntax, DE/EN local-link state, timeout observations and live transitions');
