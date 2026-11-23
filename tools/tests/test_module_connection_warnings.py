"""Connection preferences never replace the MCU's required-output safety policy."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
MCU = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class ConnectionWarningTests(unittest.TestCase):
    def test_single_policy_for_web_and_display_alarms(self):
        web = (MCU / 'WebStatus.inc.h').read_text(encoding='utf-8')
        scheduler = (MCU / 'MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('const auto alarm = scheduler.moduleAlarmState(m);', web)
        self.assertIn('const auto alarm = moduleAlarmState(rec);', scheduler)
        self.assertIn('DISPLAY_ALARM_WELLER_LINK, rec.weller_uart_age_sec, alarm.connection_error', scheduler)
        self.assertIn('"Weller link disconnected"), alarm.connection_error', web)

    def test_authenticated_serialized_and_verified_setting(self):
        server = (MCU / 'WebServer.inc.h').read_text(encoding='utf-8')
        self.assertIn('if (web_require_auth()) web_handle_connection_warnings()', server)
        commands = (MCU / 'MasterCommandQueue.inc.h').read_text(encoding='utf-8')
        self.assertIn('scheduler.setConnectionWarnings(r.qw1, r.flag1)', commands)
        scheduler = (MCU / 'MasterScheduler.cpp').read_text(encoding='utf-8')
        setter = scheduler.split('bool MasterScheduler::setConnectionWarnings(', 1)[1].split('\nbool ', 1)[0]
        self.assertLess(setter.index('if (!saved) return false'), setter.index('rec->connection_warnings_muted ='))
        self.assertIn('system_alarm_sample_valid_ = false', setter)
        web = (MCU / 'WebStatus.inc.h').read_text(encoding='utf-8')
        self.assertIn('prefs.getBool(\n      MasterSettingsStore::moduleConnectionWarningKey(m.uid).c_str(), false)', web)
        self.assertIn('onchange="saveConnectionWarnings(this)"', web)

    def test_compatible_bounded_health_transport(self):
        scheduler = (MCU / 'MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('ofe_module_health::live(alarm.warning(), alarm.error())', scheduler)
        self.assertIn('ofe_module_health::metrics(alarm.warning(), alarm.error())', scheduler)
        for folder in sorted((ROOT / 'Module').glob('DisplayModule_*')):
            if not folder.is_dir():
                continue
            with self.subTest(display=folder.name):
                health = folder / 'src/DisplayModuleHealth.h'
                self.assertEqual(health.read_bytes(), (MCU / 'DisplayModuleHealth.h').read_bytes())
                source = (folder / 'src/OfeDisplayModuleCardsUi.inc.h').read_text()
                self.assertIn('module.flags & SUMMARY_KNOWN', source)
                self.assertIn('state.flags & METRICS_KNOWN', source)
                self.assertIn('module.live_io_flags & LIVE_KNOWN', source)
                self.assertNotIn('FAST_FLAG_ERROR_PENDING', source)
                self.assertIn('status.master_alarm_critical_mask & (1U << i)', source)

    def test_optional_bridge_links_and_separate_control_column(self):
        policy = (MCU / 'ModuleAlarmPolicy.h').read_text()
        support = policy.split('inline bool supportsConnectionWarning(', 1)[1].split('inline State', 1)[0]
        self.assertIn('MODULE_UNIVERSAL_RS232', support)
        self.assertIn('MODULE_MODBUS_RTU', support)
        self.assertIn('if (local_link_fault && !required) state.faults &= 0xFFFEU;', policy)
        scheduler = (MCU / 'MasterScheduler.cpp').read_text()
        self.assertIn('s.faults = alarm.faults;', scheduler)
        web = (MCU / 'WebStatus.inc.h').read_text(encoding='utf-8')
        self.assertIn('data-i18n="connection_monitoring"', web)
        self.assertIn('colspan="15"', web)


if __name__ == '__main__':
    unittest.main()
