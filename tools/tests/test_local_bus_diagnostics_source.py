from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
MCU = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class LocalBusDiagnosticsSource(unittest.TestCase):
    def test_shared_diagnostics_and_pinned_warning_bits(self):
        canonical = (MCU / 'LocalBusDiagnostics.h').read_bytes()
        for name in ('UniversalRs232Module', 'ModbusRtuModule'):
            self.assertEqual((ROOT / 'Module' / name / 'src/LocalBusDiagnostics.h').read_bytes(), canonical)
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            self.assertIn('local_diagnostics.checksum.fail(last_local_activity_ms)', source)
            self.assertIn('local_diagnostics.checksum.good()', source)
            self.assertIn('local_diagnostics.faults(now)', source)
            self.assertIn('FAULT_LOCAL_UART_INACTIVE | ofe_local_diag::WARNING_MASK', source)
            self.assertIn('0x0800,warn,', source)
            self.assertIn('0x1000,warn,', source)

    def test_only_tracked_requests_can_timeout(self):
        modbus = (ROOT / 'Module/ModbusRtuModule/ModbusRtuModule.ino').read_text()
        self.assertIn('if (pending_modbus_entity_id) {\n    local_diagnostics.timeout.fail(now);', modbus)
        self.assertIn('local_diagnostics.response(millis(), data[2])', modbus)
        self.assertIn('last_modbus_exception = 0;', modbus)
        universal = (ROOT / 'Module/UniversalRs232Module/UniversalRs232Module.ino').read_text()
        self.assertIn('if (e.match[0] && !e.response_pending)', universal)
        self.assertIn('e.response_pending = false;', universal)
        self.assertIn('uint32_t(now - e.response_started_ms) >= local_response_timeout_ms()', universal)

    def test_mcu_does_not_promote_diagnostic_warnings_to_errors(self):
        policy = (MCU / 'ModuleAlarmPolicy.h').read_text()
        self.assertIn('state.warning_faults = state.faults & ofe_local_diag::WARNING_MASK;', policy)
        self.assertIn('offline || connection_error ||', policy)
        self.assertIn('~(0x0002U | warning_faults)', policy)
        scheduler = (MCU / 'MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('ignored |= ofe_local_diag::WARNING_MASK;', scheduler)
        self.assertIn('local_link_fault || rec.connection_warnings_muted', policy)
        for name in ('UniversalRs232Module', 'ModbusRtuModule'):
            module = (ROOT / 'Module' / name / (name + '.ino')).read_text()
            self.assertIn('if (!local_device_online_now(now)) local_diagnostics.timeout.reset();', module)


if __name__ == '__main__':
    unittest.main()
