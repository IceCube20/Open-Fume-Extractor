from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Rs485QualityTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'), 'Node.js required')
    def test_actual_session_engine(self):
        subprocess.run([shutil.which('node'), str(ROOT / 'tools/tests/test_rs485_quality.cjs')], check=True, timeout=30)

    def test_passive_feed_in_existing_diagnostics(self):
        source = (ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebDiagnostics.inc.h').read_text(encoding='utf-8-sig')
        self.assertIn('web_append_rs485_quality(html);', source)
        self.assertIn('window.ofeBusQuality.feed(d)', source)
        self.assertIn('html += OFE_RS485_QUALITY_SCRIPT;', source)

    def test_version_rollover(self):
        source = (ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterBuildConfig.h').read_text(encoding='utf-8-sig')
        version = tuple(int(re.search(r'#define MASTER_FW_' + name + r' (\d+)', source)[1]) for name in ('MAJOR', 'MINOR', 'PATCH'))
        self.assertGreaterEqual(version, (2, 1, 0))
        self.assertLess(version[2], 100)
        self.assertIn('#if MASTER_FW_PATCH >= 100', source)


if __name__ == '__main__':
    unittest.main()
