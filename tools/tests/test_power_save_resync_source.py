from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster'


class PowerSaveResyncIntegrationTests(unittest.TestCase):
    def test_all_mcus_invalidate_acknowledged_state_after_disconnect_or_reboot(self):
        for name in ('OpenFumeExtractorMCU_S3', 'OpenFumeExtractorMCU_P4', 'OpenFumeExtractorMCU_P4_POE'):
            with self.subTest(mcu=name):
                src = ROOT / name / 'src'
                record = (src / 'ModuleRegistry.h').read_text(encoding='utf-8')
                self.assertIn('bool eco_state_known = false;', record)
                uptime = (src / 'ModulePowerSaveState.h').read_text(encoding='utf-8')
                self.assertIn('rec.eco_state_known = false;', uptime)
                scheduler = (src / 'MasterScheduler.cpp').read_text(encoding='utf-8')
                self.assertEqual(scheduler.count('rec->eco_mode = false;'), 3)
                self.assertEqual(scheduler.count('rec->eco_state_known = false;'), 3)
                setter = scheduler.split('bool MasterScheduler::setModulePowerSave(', 1)[1].split(
                    'bool MasterScheduler::wakeModule', 1)[0]
                self.assertLess(setter.index('resp.payload[0] != STATUS_OK'), setter.index('rec.eco_state_known = true;'))

    def test_idle_grace_period_also_confirms_awake(self):
        source = (ROOT / 'OpenFumeExtractorMCU_S3/src/MasterScheduler.cpp').read_text(encoding='utf-8')
        grace = source.split('if ((uint32_t)(now - power_save_idle_since_ms_) < delay_ms)', 1)[1].split('\n  }', 1)[0]
        self.assertIn('wakeAllModules();', grace)
        wake = source.split('void MasterScheduler::wakeAllModules()', 1)[1].split(
            'void MasterScheduler::refreshPowerSaveActive()', 1)[0]
        self.assertIn('rec.eco_state_known && !rec.eco_mode', wake)
        self.assertIn('rec.caps & CAP_POWER_SAVE', wake)
        self.assertIn('rec.addr == module_fw_target_', wake)
        self.assertLess(wake.index('wakeModule(rec);'), wake.index('return;', wake.index('wakeModule(rec);')))


if __name__ == '__main__':
    unittest.main()
