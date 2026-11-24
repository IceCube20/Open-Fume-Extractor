from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
MCU = ROOT / 'OpenFumeExtractorMaster'


class MobileHeaderTests(unittest.TestCase):
    def test_all_mcus_share_the_mobile_header(self):
        expected = (MCU / 'OpenFumeExtractorMCU_S3/src/WebHeaderStyles.h').read_bytes()
        for name in ('OpenFumeExtractorMCU_S3', 'OpenFumeExtractorMCU_P4', 'OpenFumeExtractorMCU_P4_POE'):
            source = MCU / name / 'src'
            self.assertEqual((source / 'WebHeaderStyles.h').read_bytes(), expected)
            self.assertIn('OFE_WEB_HEADER_STYLE', (source / 'WebTypography.h').read_text())
            for page in ('WebShell.inc.h', 'WebStatus.inc.h'):
                self.assertIn('OFE_WEB_TYPOGRAPHY_STYLE', (source / page).read_text(encoding='utf-8'))

    def test_navigation_scrolls_without_hiding_destinations(self):
        source = (MCU / 'OpenFumeExtractorMCU_S3/src/WebHeaderStyles.h').read_text()
        self.assertIn('@media(max-width:700px)', source)
        self.assertIn('flex-wrap:nowrap', source)
        self.assertIn('overflow-x:auto', source)
        self.assertIn('flex:0 0 auto', source)
        self.assertNotIn('display:none', source)

    def test_developer_icon_keeps_live_text_and_tooltip(self):
        script = (MCU / 'OpenFumeExtractorMCU_S3/src/WebTransportIcons.h').read_text()
        self.assertIn('dev.title=dev.textContent.trim()', script)
        self.assertIn('new MutationObserver(label)', script)
        self.assertIn('active.offsetLeft-nav.offsetLeft', script)


if __name__ == '__main__':
    unittest.main()
