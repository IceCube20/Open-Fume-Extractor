// The production page uses automatic packet sizing, with no developer test selector.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/WebUpdate.inc.h'), 'utf8');
for (const removed of ['wifi_bulk_test', "name='wifi_bulk_limit'", '&wifi_bulk_limit=', 'syncBulkLimit', 'bulkLimitQuery', 'WiFi packet size (test)']) {
  assert(!source.includes(removed), 'Retired UI control remains: ' + removed);
}
assert(!source.includes('web.arg("wifi_bulk_limit")'));
assert(source.includes('module_update_wifi_bulk_limit = ofe_wifi::BULK_DATA_MAX;'));
const line = source.split(/\r?\n/).find(line => line.trimStart().startsWith('html += F("async function sendModule('));
const sender = JSON.parse(line.slice(line.indexOf('F(') + 2, line.lastIndexOf(');')));
new vm.Script(sender);
assert(!sender.includes('wifi_bulk_limit'));
console.log('PASS: no speed-test selector/query, automatic bulk default and upload JS syntax');
