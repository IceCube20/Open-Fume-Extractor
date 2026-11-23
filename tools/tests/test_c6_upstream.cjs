'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const watch = require('../maintenance/check_c6_upstream.cjs');
const root = path.resolve(__dirname, '../..');
const catalogPath = path.join(root, 'updates/hosted-c6.json');
const pinsPath = path.join(root, 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/HostedC6Firmware.h');
const beforeCatalog = fs.readFileSync(catalogPath), beforePins = fs.readFileSync(pinsPath);
const catalog = JSON.parse(beforeCatalog);
const clone = value => JSON.parse(JSON.stringify(value));
const commit = 'a'.repeat(40), token = 'test-only-api-token';
function image(value = 0x55) { const b = Buffer.alloc(64, value); b[0] = 0xe9; b.writeUInt16LE(13, 12); return b; }
function entry(v, data = image()) {
  const name = `esp32c6-v${v}.bin`;
  return {name, path: `hosted/${name}`, type: 'file', size: data.length,
    sha: crypto.createHash('sha1').update(`blob ${data.length}\0`).update(data).digest('hex'),
    download_url: 'https://attacker.invalid/not-used.bin'};
}
function fixture(listing, data = image(), ref = {object: {type: 'commit', sha: commit}}) {
  const calls = [];
  const fetcher = async (url, options) => {
    calls.push({url, options});
    if (url.endsWith('/git/ref/heads/gh-pages')) return new Response(JSON.stringify(ref));
    if (url.endsWith(`/contents/hosted?ref=${commit}`)) return new Response(JSON.stringify(listing));
    if (url.startsWith(`https://raw.githubusercontent.com/espressif/arduino-esp32/${commit}/hosted/`))
      return new Response(data);
    throw Error(`Unexpected request ${url}`);
  };
  return {calls, fetcher};
}
function existing(candidate, {actor = 'github-actions[bot]', state = 'closed'} = {}) {
  return {number: 7, html_url: 'https://github.com/IceCube20/Open-Fume-Extractor/issues/7',
    user: {login: actor}, body: watch.marker(candidate), state};
}
function issueApi(pages = [[]], error = false) {
  const calls = [];
  return {calls, fetcher: async (url, options) => {
    calls.push({url, options});
    if (error) return new Response('denied', {status: 403});
    if (options.method === 'GET') {
      const page = Number(new URL(url).searchParams.get('page'));
      return new Response(JSON.stringify(pages[page - 1] || []));
    }
    assert.equal(url, 'https://api.github.com/repos/IceCube20/Open-Fume-Extractor/issues');
    const posted = JSON.parse(options.body);
    return new Response(JSON.stringify({number: 8, html_url: 'https://github.com/IceCube20/Open-Fume-Extractor/issues/8',
      user: {login: 'github-actions[bot]'}, body: posted.body}));
  }};
}

(async () => {
  assert(watch.version('2.13.1') > watch.version('2.12.13'));
  assert(watch.version('2.12.13') > watch.version('2.9.6'));
  for (const bad of ['2.256.0', '2.13', '2.13.1-rc1', '02.13.1', '2.13.1\n', null])
    assert.throws(() => watch.version(bad));
  assert.equal(watch.approvedBaseline(catalog), '2.12.13');
  assert.throws(() => watch.approvedBaseline({schema: 1, releases: []}));
  const noApproval = clone(catalog); noApproval.releases.forEach(r => {r.status = 'withdrawn';});
  assert.throws(() => watch.approvedBaseline(noApproval));

  const blob = image(), next = entry('2.13.1', blob);
  let f = fixture([entry('2.9.6'), entry('2.12.13'), next,
    {name: 'esp32c5-v2.14.0.bin'}, {name: 'esp32c6-v2.14.0-rc1.bin'}, {name: 'flash.sh'}]);
  const report = await watch.discover(catalog, f.fetcher, token);
  assert.equal(report.images.length, 1);
  const candidate = report.images[0];
  assert.equal(candidate.version, '2.13.1');
  assert.equal(candidate.sha256, crypto.createHash('sha256').update(blob).digest('hex'));
  assert.equal(candidate.commit, commit);
  assert(f.calls.every(c => c.options.redirect === 'error'));
  for (const c of f.calls) {
    if (new URL(c.url).hostname === 'api.github.com') assert.equal(c.options.headers.Authorization, `Bearer ${token}`);
    else assert(!c.options.headers.Authorization);
    assert(!c.url.includes('attacker.invalid'));
  }
  assert.equal((await watch.discover(catalog, fixture([entry('2.12.13')]).fetcher)).images.length, 0);
  f = fixture([entry('2.13.1'), entry('2.14.0')]);
  assert.deepEqual((await watch.discover(catalog, f.fetcher)).images.map(i => i.version), ['2.13.1', '2.14.0']);

  for (const badListing of [null, [], [{name: 'flash.sh'}], [next, next],
    [{...next, path: 'outside/file.bin'}], [{...next, type: 'dir'}], [{...next, sha: 'bad'}],
    [{...next, name: next.name + '\n'}], [{...next, sha: next.sha + '\n'}],
    [{...next, size: '64'}], [{...next, size: 8388609}], Array(513).fill(next)])
    await assert.rejects(() => watch.discover(catalog, fixture(badListing).fetcher));
  await assert.rejects(() => watch.discover(catalog, fixture([next], blob, {object: {type: 'tree', sha: commit}}).fetcher));
  await assert.rejects(() => watch.discover(catalog, fixture([next], blob, {object: {type: 'commit', sha: '../bad'}}).fetcher));
  await assert.rejects(() => watch.discover(catalog, fixture([next], blob, {object: {type: 'commit', sha: commit + '\n'}}).fetcher));
  const wrongChip = image(); wrongChip.writeUInt16LE(18, 12);
  for (const bad of [Buffer.alloc(4), wrongChip, Buffer.alloc(64), image(0x66)])
    assert.throws(() => watch.inspectImage(bad, next));
  await assert.rejects(() => watch.bytes('http://api.github.com/repos/x/y', {}, f.fetcher));
  await assert.rejects(() => watch.bytes('https://attacker.invalid/x', {}, f.fetcher));
  await assert.rejects(() => watch.bytes('https://api.github.com/x', {limit: 4}, async () => new Response('12345')));
  await assert.rejects(() => watch.bytes('https://api.github.com/x', {limit: 4}, async () => new Response('x', {headers: {'content-length': '5'}})));
  await assert.rejects(() => watch.discover(catalog, async () => new Response('denied', {status: 403})), /HTTP 403/);
  await assert.rejects(() => watch.discover(catalog, async () => new Response('{broken')));

  const settings = {repo: 'IceCube20/Open-Fume-Extractor', token, reviewer: 'IceCube20'};
  let api = issueApi();
  const result = await watch.publish(report, catalog, settings, api.fetcher);
  assert(result[0].created);
  const post = api.calls.find(c => c.options.method === 'POST');
  const body = JSON.parse(post.options.body);
  assert(body.title.includes('2.13.1'));
  assert.deepEqual(body.assignees, ['IceCube20']);
  assert(body.body.includes('NOT an OFE approval'));
  assert(body.body.includes('waveshare-p4-wifi6-poe-eth-v3'));
  assert(body.body.includes('SHA-256'));
  for (const state of ['open', 'closed']) {
    api = issueApi([[existing(candidate, {state})]]);
    assert.equal((await watch.publish(report, catalog, settings, api.fetcher))[0].created, false);
    assert(!api.calls.some(c => c.options.method === 'POST'));
  }
  api = issueApi([[existing(candidate, {actor: 'untrusted-user'})]]);
  assert((await watch.publish(report, catalog, settings, api.fetcher))[0].created);
  api = issueApi([[{...existing(candidate), pull_request: {}}]]);
  assert((await watch.publish(report, catalog, settings, api.fetcher))[0].created);
  const changed = {...candidate, sha256: 'b'.repeat(64)};
  api = issueApi([[existing(candidate)]]);
  assert((await watch.publish({...report, images: [changed]}, catalog, settings, api.fetcher))[0].created);
  const full = Array.from({length: 100}, (_, i) => ({number: i + 1, body: '', user: {login: 'someone'}}));
  api = issueApi([full, [existing(candidate)]]);
  assert.equal((await watch.publish(report, catalog, settings, api.fetcher))[0].created, false);
  assert.equal(api.calls.length, 2);
  api = issueApi(Array(20).fill(full));
  await assert.rejects(() => watch.publish(report, catalog, settings, api.fetcher), /deduplicate/);
  assert(!api.calls.some(c => c.options.method === 'POST'));
  api = issueApi([], true);
  await assert.rejects(() => watch.publish(report, catalog, settings, api.fetcher), /HTTP 403/);
  assert(!api.calls.some(c => c.options.method === 'POST'));
  assert.deepEqual(await watch.publish({...report, images: []}, catalog, settings, api.fetcher), []);
  for (const invalid of [{repo: '../other'}, {repo: 'user/../other'}, {repo: 'owner/repo?x=1'}, {token: ''}, {reviewer: 'bad/user'}]) {
    api = issueApi();
    await assert.rejects(() => watch.publish(report, catalog, {...settings, ...invalid}, api.fetcher));
    assert.equal(api.calls.length, 0);
  }
  assert(watch.issueBody({...candidate, size: 2097153}, catalog).includes('exceeds the current MCU C6 staging limit'));
  assert(fs.readFileSync(catalogPath).equals(beforeCatalog));
  assert(fs.readFileSync(pinsPath).equals(beforePins));
  console.log('PASS: C6 upstream watch: immutable official artifacts, numeric versions, size/chip/blob/hash checks, no token leakage, bounded errors, open/closed deduplication, pagination, reviewer assignment and unchanged approval/pins');
})().catch(error => {console.error(error); process.exitCode = 1;});
