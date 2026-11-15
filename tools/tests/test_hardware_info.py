from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class HardwareInfoTests(unittest.TestCase):
    def test_runtime_data_not_board_assumptions(self):
        source = (ROOT / 'WebConfig.inc.h').read_text(encoding='utf-8-sig')
        for api in ('getChipModel', 'getChipRevision', 'getChipCores', 'getCpuFreqMHz',
                    'getFlashChipSize', 'getFlashChipSpeed', 'getFlashChipMode',
                    'getPsramSize', 'getSdkVersion'):
            self.assertIn('ESP.' + api + '()', source)
        self.assertIn('ESP_ARDUINO_VERSION_STR', source)
        self.assertIn('web_append_hardware_info(html,master_hardware_info())', source)
        self.assertNotIn('esp_hosted_get_coprocessor_fwversion', source)

    def test_revision_and_coprocessor_are_distinct(self):
        source = (ROOT / 'WebHardwareInfo.h').read_text(encoding='utf-8-sig')
        self.assertIn('info.revision/100', source)
        self.assertIn('info.revision%100', source)
        self.assertIn('if(info.p4)', source)
        self.assertIn('C6 firmware', source)
        self.assertIn('html_escape(value)', source)


if __name__ == '__main__':
    unittest.main()
