from pathlib import Path
import unittest

SRC = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class SharedWebFormStylesTests(unittest.TestCase):
    def test_both_page_renderers_include_the_common_style(self):
        typography = (SRC / 'WebTypography.h').read_text()
        self.assertIn('#include "WebFormStyles.h"', typography)
        self.assertIn('OFE_WEB_FORM_STYLE', typography)
        for name in ('WebStatus.inc.h', 'WebShell.inc.h'):
            self.assertIn('OFE_WEB_TYPOGRAPHY_STYLE', (SRC / name).read_text())

    def test_styles_do_not_depend_on_usb_or_chip_target(self):
        style = (SRC / 'WebFormStyles.h').read_text()
        self.assertNotIn('CONFIG_IDF_TARGET_ESP32P4', style)
        self.assertIn('input[type=file]:enabled::file-selector-button:hover', style)
        self.assertIn('height:44px', style)
        self.assertNotIn('file-selector-button', (SRC / 'WebUsbStorageUi.h').read_text())


if __name__ == '__main__':
    unittest.main()
