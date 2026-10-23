"""Per-core CPU presentation uses one sampler and one shared JSON/JS format."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster/src'


class CpuCoresSource(unittest.TestCase):
    def test_both_pages_use_same_snapshot(self):
        for name, element in (('WebStatus.inc.h', 'cpu_cores'), ('WebDiagnostics.inc.h', 'diag_cpu_cores')):
            source = (ROOT / name).read_text(encoding='utf-8')
            self.assertIn('append_cpu_load_json(json);', source)
            self.assertIn(element, source)
            self.assertIn('cpuCoreText(d.cpu_core_load_pct)', source)
        source = (ROOT / 'MasterDiagnostics.inc.h').read_text(encoding='utf-8')
        sampler = source.split('static void sample_cpu_load()', 1)[1].split('static void append_cpu_load_json', 1)[0]
        self.assertEqual(sampler.count('vTaskGetInfo('), 1)
        self.assertNotIn('malloc', sampler)
        self.assertNotIn('xTaskCreate', sampler)
        self.assertIn('cpu_core_load_valid = false;', sampler)
        self.assertIn('if (cpu_core_load_valid)', source)
        self.assertIn('F("null")', source)
        self.assertIn('cpu_core_load_valid', (ROOT / 'MasterCli.inc.h').read_text(encoding='utf-8'))

    def test_actual_shared_js_formats_zero_full_partial_and_unavailable(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('Node.js unavailable')
        source = (ROOT / 'WebShell.inc.h').read_text(encoding='utf-8')
        code = source.split('function cpuCoreText(', 1)[1].split('</script>', 1)[0]
        script = "const assert=require('node:assert/strict');\nfunction cpuCoreText(" + code + r'''
assert.equal(cpuCoreText([90,10]),'Core 0: 90% \u00b7 Core 1: 10%');
assert.equal(cpuCoreText([0,100]),'Core 0: 0% \u00b7 Core 1: 100%');
assert.equal(cpuCoreText([null,null]),'Core 0: - \u00b7 Core 1: -');
assert.equal(cpuCoreText([20]),'Core 0: 20%');
assert.equal(cpuCoreText([NaN,101]),'Core 0: - \u00b7 Core 1: -');
assert.equal(cpuCoreText([-1,'30']),'Core 0: - \u00b7 Core 1: -');
assert.equal(cpuCoreText(),'-');assert.equal(cpuCoreText([]),'-');
'''
        subprocess.run([node, '-e', script], check=True, capture_output=True, text=True, timeout=15)


if __name__ == '__main__':
    unittest.main()
