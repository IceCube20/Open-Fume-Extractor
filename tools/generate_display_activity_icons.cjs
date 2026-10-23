// Rasterize vector assets with supersampling; output is a generated A8 header.
const fs = require('fs');
const path = require('path');
const sharp = require(require.resolve('sharp', { paths: [
  process.env.OFE_NODE_MODULES || 'C:/Users/User/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules'
] }));
async function run() {
  const input = process.argv[2], output = process.argv[3], name = process.argv[4];
  if (!input || !output || !/^[a-z_]+$/.test(name || '')) throw Error('input.svg output.h icon_name');
  const size = Number(process.argv[5] || 20);
  if (!Number.isInteger(size) || size < 8 || size > 64) throw Error('icon size must be 8..64');
  const { data } = await sharp(input, { density: 768 }).resize(size, size).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
  const alpha = Array.from({ length: size * size }, (_, i) => data[i * 4 + 3]);
  const rows = Array.from({ length: size }, (_, y) => '  ' + alpha.slice(y * size, (y + 1) * size).map(v => '0x' + v.toString(16).padStart(2, '0')).join(', ') + ',');
  fs.writeFileSync(output, '#pragma once\n#include <lvgl.h>\n\n// Generated from ' + path.basename(input) +
    '; antialiased ' + size + 'x' + size + ' A8 mask.\nstatic const uint8_t ' + name + '_map[] = {\n' + rows.join('\n') +
    '\n};\nstatic const lv_image_dsc_t ' + name + ' = {\n  {LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_A8, 0, ' + size + ', ' + size + ', ' + size + ', 0},\n  sizeof(' +
    name + '_map), ' + name + '_map, nullptr, nullptr\n};\n');
}
run().catch(e => { console.error(e); process.exitCode = 1; });
