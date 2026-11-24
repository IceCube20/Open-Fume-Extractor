from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class MasterLedServiceTests(unittest.TestCase):
    def test_evt_uses_the_same_module_alarm_policy_as_the_alarm_centre(self):
        source = (SRC / 'MasterStatusLed.inc.h').read_text()
        self.assertIn('scheduler.moduleAlarmState(m)', source)
        self.assertIn('!scheduler.mainInputSourceAvailable() || scheduler.outputAddr() == 0', source)
        self.assertIn('alarm.warning()', source)
        self.assertIn('alarm.error()', source)
        self.assertNotIn('m.jbc_link_flags', source)
        self.assertNotIn('m.weller_uart_age_sec', source)

    def test_one_pixel_owner_and_safe_fallback(self):
        service = (SRC / 'MasterStatusLedService.h').read_text()
        self.assertIn('std::atomic<uint32_t>', service)
        self.assertIn('if (!task_) render()', service)
        self.assertIn('4096, this, 2, &task_, 1', service)
        self.assertIn('pdMS_TO_TICKS(20)', service)
        setup = (SRC / 'MasterSetup.inc.h').read_text()
        callback = setup.split('scheduler.setServiceCallback(', 1)[1].split('});', 1)[0]
        self.assertNotIn('.tick()', callback)
        self.assertIn('master_update_active', callback)
        self.assertIn('master_update_restart_due_ms.load()', callback)

    def test_full_rgbw_frame_and_no_communication_flash(self):
        transport = (SRC / 'MasterStatusPixels.h').read_text()
        self.assertIn('RMT_MEM_NUM_BLOCKS_2', transport)
        self.assertIn('rmt_data_t symbols_[64]', transport)
        self.assertIn('RMT_WAIT_FOR_EVER', transport)
        self.assertNotIn('Adafruit_NeoPixel::show(', transport)
        self.assertIn('void pulseBusActivity() {}', (SRC / 'MasterStatusLedService.h').read_text())

    def test_mcu_names_do_not_change_firmware_target_or_product(self):
        config = (SRC / 'MasterBuildConfig.h').read_text()
        for name in ('"MCU-S3"', '"MCU-P4"', '"Main Control Unit"'):
            self.assertIn(name, config)
        self.assertIn('#define MASTER_FW_NAME "Open Fume Extractor"', config)
        self.assertIn('OFE_FW_SIG:v1;target=MASTER;', config)
        self.assertIn('json += MASTER_MCU_MODEL', (SRC / 'WebStatus.inc.h').read_text())


if __name__ == '__main__':
    unittest.main()
