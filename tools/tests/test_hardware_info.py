from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class HardwareInfoTests(unittest.TestCase):
    def test_board_information_is_rendered_for_both_mcus(self):
        render = (ROOT / 'WebHardwareInfo.h').read_text()
        self.assertIn('row("Board","Board",info.board)', render)
        self.assertIn('info.build_board', render)
        self.assertNotIn('CONFIG_IDF_TARGET_ESP32P4', render)
        producer = (ROOT / 'WebConfig.inc.h').read_text()
        self.assertIn('info.board=OFE_MASTER_BOARD_NAME', producer)
        self.assertIn('info.build_board=ARDUINO_BOARD', producer)
        board = (ROOT / 'MasterBoardConfig.h').read_text()
        self.assertIn('"ESP32-S3 (MCU-S3)"', board)

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
