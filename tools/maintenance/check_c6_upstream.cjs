'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const API = 'https://api.github.com';
const UPSTREAM = 'espressif/arduino-esp32';
const IMAGE_LIMIT = 8 * 1024 * 1024;
const FILE = /^esp32c6-v(\d{1,3}\.\d{1,3}\.\d{1,3})\.bin$/;
const SHA = /^[a-f0-9]{40}$/;

function version(value) {
  if (typeof value !== 'string' || !/^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(value)) throw Error('Invalid version');
  const parts = value.split('.').map(Number);
  if (parts.some(n => n > 255) || parts.join('.') !== value) throw Error('Invalid version');
  return parts[0] * 65536 + parts[1] * 256 + parts[2];
}

function approvedBaseline(catalog) {
  if (!catalog || catalog.schema !== 1 || !Array.isArray(catalog.releases) || catalog.releases.length > 32)
    throw Error('Invalid approval catalog');
  const approved = catalog.releases.filter(r => r.status === 'approved');
  if (!approved.length) throw Error('No approved baseline');
  for (const r of approved) {
    if (!r.host || !r.coprocessor || r.transport !== 'sdio' || r.coprocessor.chip !== 'esp32c6' ||
        typeof r.board !== 'string' || !/^[a-z0-9.-]{1,96}$/.test(r.board)) throw Error('Invalid approved combination');
    version(r.host.esp_hosted); version(r.coprocessor.version);
    version(r.host.arduino_esp32); version(r.host.esp_idf);
  }
  return approved.reduce((a, b) => version(a.coprocessor.version) > version(b.coprocessor.version) ? a : b).coprocessor.version;
}

async function bytes(url, {token = '', method = 'GET', body, limit = 8 * 1024 * 1024} = {}, fetcher = fetch) {
  const parsed = new URL(url);
  if (parsed.protocol !== 'https:' || !['api.github.com', 'raw.githubusercontent.com'].includes(parsed.hostname))
    throw Error('Untrusted upstream URL');
  const headers = {'User-Agent': 'Open-Fume-Extractor-C6-watch', Accept: 'application/vnd.github+json'};
  if (token && parsed.hostname === 'api.github.com') headers.Authorization = `Bearer ${token}`;
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  const response = await fetcher(url, {method, headers, redirect: 'error', signal: AbortSignal.timeout(30000),
    ...(body === undefined ? {} : {body: JSON.stringify(body)})});
  if (!response.ok) { await response.body?.cancel(); throw Error(`HTTP ${response.status}: ${url}`); }
  if (Number(response.headers.get('content-length')) > limit || !response.body) {
    await response.body?.cancel(); throw Error('Response too large or empty');
  }
  const reader = response.body.getReader(), chunks = []; let size = 0;
  try {
    for (;;) {
      const part = await reader.read(); if (part.done) break;
      size += part.value.byteLength;
      if (size > limit) throw Error('Response too large');
      chunks.push(Buffer.from(part.value));
    }
  } finally { await reader.cancel(); reader.releaseLock(); }
  return Buffer.concat(chunks, size);
}
async function json(url, options, fetcher) { return JSON.parse((await bytes(url, options, fetcher)).toString('utf8')); }

function inspectImage(data, entry) {
  if (!Buffer.isBuffer(data) || data.length !== entry.size || data.length < 24 || data.length > IMAGE_LIMIT ||
      data[0] !== 0xe9 || data.readUInt16LE(12) !== 13) throw Error('Invalid C6 image size/header/chip');
  const blob = crypto.createHash('sha1').update(`blob ${data.length}\0`).update(data).digest('hex');
  if (blob !== entry.sha) throw Error('Upstream Git blob mismatch');
  return {size: data.length, sha256: crypto.createHash('sha256').update(data).digest('hex')};
}

async function discover(catalog, fetcher = fetch, token = '') {
  const baseline = approvedBaseline(catalog);
  const ref = await json(`${API}/repos/${UPSTREAM}/git/ref/heads/gh-pages`, {token}, fetcher);
  const commit = ref?.object?.sha;
  if (typeof commit !== 'string' || commit.length !== 40 || !SHA.test(commit) || ref.object.type !== 'commit')
    throw Error('Invalid upstream commit');
  const listing = await json(`${API}/repos/${UPSTREAM}/contents/hosted?ref=${commit}`, {token}, fetcher);
  if (!Array.isArray(listing) || !listing.length || listing.length > 512) throw Error('Invalid upstream listing');
  const candidates = [], seen = new Set(); let known = 0;
  for (const entry of listing) {
    const match = typeof entry.name === 'string' && entry.name.match(FILE);
    if (!match) continue;
    if (match[0] !== entry.name) throw Error('Invalid C6 upstream filename');
    const v = version(match[1]); known++;
    if (seen.has(v) || entry.type !== 'file' || entry.path !== `hosted/${entry.name}` ||
        typeof entry.sha !== 'string' || entry.sha.length !== 40 || !SHA.test(entry.sha) ||
        !Number.isInteger(entry.size) || entry.size < 24 || entry.size > IMAGE_LIMIT) throw Error('Invalid C6 upstream entry');
    seen.add(v);
    if (v > version(baseline)) candidates.push({...entry, version: match[1]});
  }
  if (!known || candidates.length > 16) throw Error('Upstream list needs manual inspection');
  candidates.sort((a, b) => version(a.version) - version(b.version));
  const images = [];
  for (const entry of candidates) {
    const source = `https://raw.githubusercontent.com/${UPSTREAM}/${commit}/hosted/${entry.name}`;
    // Never follow listing-provided URLs, execute binaries or send API tokens to the image host.
    const image = await bytes(source, {limit: IMAGE_LIMIT}, fetcher);
    images.push({version: entry.version, ...inspectImage(image, entry), commit, gitBlob: entry.sha, source,
      officialUrl: `https://espressif.github.io/arduino-esp32/hosted/${entry.name}`});
  }
  return {baseline, images};
}

function marker(image) { return `<!-- ofe-c6-upstream:v1:${image.version}:${image.sha256} -->`; }
function issueBody(image, catalog) {
  const combinations = catalog.releases.filter(r => r.status === 'approved').map(r =>
    `- \`${r.board}\`: Arduino ${r.host.arduino_esp32}, IDF ${r.host.esp_idf}, Hosted/C6 ${r.host.esp_hosted}`).join('\n');
  return `${marker(image)}\n## Upstream C6 image found\n\n` +
    `Espressif publishes a versioned ESP32-C6 image **${image.version}** in its Arduino hosted-image directory.\n\n` +
    `**Discovery only: NOT an OFE approval, SDK compatibility claim or permission to flash.**\n\n` +
    `- Official URL: ${image.officialUrl}\n- Immutable source: ${image.source}\n` +
    `- Upstream commit: \`${image.commit}\`\n- Git blob: \`${image.gitBlob}\`\n` +
    `- Size: ${image.size} bytes\n- SHA-256: \`${image.sha256}\`\n` +
    `- Downloaded image: ESP image magic and ESP32-C6 chip ID checked; Git blob verified.\n\n` +
    (image.size > 2097152 ? '**This image exceeds the current MCU C6 staging limit (2 MiB).**\n\n' : '') +
    `## Current approved combinations\n\n${combinations}\n\n` +
    `## Maintainer review\n\n- [ ] Review upstream changes and matching Arduino/IDF/ESP-Hosted versions.\n` +
    `- [ ] Test the exact board, SDIO wiring, WiFi/BLE and update/recovery path.\n` +
    `- [ ] Review image provenance, size and SHA-256; update MCU SDK/pins if necessary.\n` +
    `- [ ] Only then update the per-board approval catalog and run its tests.\n\n` +
    `The watcher has not modified the approval catalog, firmware pins or devices. ` +
    `Closing this issue prevents another notice for this exact version/hash, including after withdrawal.\n`;
}

async function publish(report, catalog, {repo, token, reviewer = ''}, fetcher = fetch) {
  if (!/^[A-Za-z0-9][A-Za-z0-9-]{0,38}\/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$/.test(repo || '') || !token ||
      (reviewer && !/^[A-Za-z0-9-]{1,39}$/.test(reviewer))) throw Error('Invalid issue publication settings');
  if (!report.images.length) return [];
  const issues = []; let complete = false;
  for (let page = 1; page <= 20; page++) {
    const items = await json(`${API}/repos/${repo}/issues?state=all&per_page=100&page=${page}`, {token}, fetcher);
    if (!Array.isArray(items) || items.length > 100) throw Error('Invalid issue listing');
    issues.push(...items);
    if (items.length < 100) { complete = true; break; }
  }
  if (!complete) throw Error('Cannot safely deduplicate all issues');
  const results = [];
  for (const image of report.images) {
    const existing = issues.find(i => !i.pull_request && i.user?.login === 'github-actions[bot]' &&
      typeof i.body === 'string' && i.body.includes(marker(image)));
    if (existing) { results.push({version: image.version, created: false, url: existing.html_url}); continue; }
    const issue = await json(`${API}/repos/${repo}/issues`, {token, method: 'POST', body: {
      title: `C6 upstream ${image.version}: compatibility review (${image.sha256.slice(0, 8)})`,
      body: issueBody(image, catalog), ...(reviewer ? {assignees: [reviewer]} : {})
    }}, fetcher);
    if (!Number.isInteger(issue.number) || issue.number < 1 ||
        issue.html_url !== `https://github.com/${repo}/issues/${issue.number}`) throw Error('Invalid created issue response');
    issues.push(issue);
    results.push({version: image.version, created: true, url: issue.html_url});
  }
  return results;
}

async function main() {
  const args = process.argv.slice(2);
  if (args.some(a => a !== '--publish')) throw Error('Usage: node check_c6_upstream.cjs [--publish]');
  const notify = args.includes('--publish');
  if (notify && process.env.GITHUB_ACTIONS !== 'true') throw Error('--publish is restricted to GitHub Actions');
  const catalog = JSON.parse(fs.readFileSync(path.join(__dirname, '../../updates/hosted-c6-v2.json'), 'utf8'));
  const token = process.env.GITHUB_TOKEN || '';
  const report = await discover(catalog, fetch, token);
  const notices = notify ? await publish(report, catalog, {repo: process.env.GITHUB_REPOSITORY, token,
    reviewer: process.env.OFE_C6_REVIEWER || ''}) : [];
  console.log(JSON.stringify({...report, mode: notify ? 'review-issues' : 'read-only', notices}, null, 2));
  if (process.env.GITHUB_STEP_SUMMARY) {
    const lines = report.images.map(i => `- C6 ${i.version}: candidate only, SHA-256 \`${i.sha256}\``);
    fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, `## C6 upstream watch\n\nApproved baseline: ${report.baseline}.\n\n` +
      (lines.join('\n') || 'No newer versioned upstream C6 images found.') + '\n\n' +
      notices.map(n => `- ${n.created ? 'Created' : 'Existing'} review: ${n.url}`).join('\n') +
      '\n\nNo catalog/pin changes or device updates were performed.\n');
  }
}
module.exports = {version, approvedBaseline, bytes, inspectImage, discover, marker, issueBody, publish};
if (require.main === module) main().catch(error => {console.error(error.message); process.exitCode = 1;});
