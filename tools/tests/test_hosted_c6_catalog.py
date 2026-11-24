from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HostedC6CatalogTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'), 'Node.js required')
    def test_actual_catalog_selector_and_pinned_release(self):
        subprocess.run([shutil.which('node'), str(ROOT / 'tools/tests/test_hosted_c6_catalog.cjs')],
                       cwd=ROOT, check=True, timeout=30)

    def test_online_catalog_is_advisory_not_a_flash_authority(self):
        src = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'
        ui = (src / 'HostedC6WebUi.h').read_text(encoding='utf-8-sig')
        check = ui[ui.index('async function checkCatalog()'):ui.index('function phaseText(')]
        self.assertNotIn("request('install'", check)
        self.assertNotIn("request('begin'", check)
        self.assertNotIn('cfg.url=', ui)
        self.assertNotIn('cfg.sha256=', ui)
        self.assertIn('checkCatalog();setInterval(refresh,1000)', ui)
        self.assertNotIn('setInterval(checkCatalog', ui)
        updater = (src / 'HostedC6Updater.h').read_text(encoding='utf-8-sig')
        self.assertIn('test_image_?ofe_c6::TEST_SHA256:ofe_c6::SHA256', updater)
        self.assertIn('diff|=digest[i]^expected[i]', updater)
        self.assertNotIn('CATALOG_URL', updater)


if __name__ == '__main__':
    unittest.main()
