"""Run actual embedded upload JavaScript in the normal firmware regression suite."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class McuOtaTransportTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'), 'Node.js required')
    def test_complete_upload_uses_registered_endpoints(self):
        subprocess.run([shutil.which('node'), str(ROOT / 'tools/tests/test_update_transport.js')],
                       cwd=ROOT, check=True, capture_output=True, text=True, timeout=30)

    def test_both_mcus_preserve_ota_route_case(self):
        for name in ('OpenFumeExtractorMCU_S3', 'OpenFumeExtractorMCU_P4'):
            source = (ROOT / 'OpenFumeExtractorMaster' / name / 'src/WebUpdate.inc.h').read_text(encoding='utf-8-sig')
            self.assertNotIn('/update/MCU/', source, name)
            self.assertIn("fetch('/update/master/end'", source, name)
            self.assertIn("abortOwnedUpdate(run,'/update/master/abort')", source, name)

    @unittest.skipUnless(shutil.which('node'), 'Node.js required')
    def test_recovery_does_not_commit_or_remove_security_headers(self):
        subprocess.run([shutil.which('node'), str(ROOT / 'tools/tests/test_mcu_ota_recovery.js')],
                       cwd=ROOT, check=True, capture_output=True, text=True, timeout=15)
