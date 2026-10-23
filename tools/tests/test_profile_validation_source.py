"""Keep transport-specific profile preflight before persistence/transfer."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProfileValidationSourceTest(unittest.TestCase):
    def test_invalid_access_is_rejected_before_persistence_or_upload(self):
        source = (ROOT / 'OpenFumeExtractorMaster/src/WebControls.inc.h').read_text(encoding='utf-8')
        handler = source.split('static void web_handle_universal_profile()', 1)[1].split('static int universal_hex_nibble', 1)[0]
        self.assertIn('if (profile_text_arg)', handler)
        self.assertIn('is_modbus ? ofe_profile::validateModbusProfile(text)', handler)
        self.assertIn(': ofe_profile::validateRs232Access(text)', handler)
        validation = handler.index('validateModbusProfile(')
        self.assertLess(validation, handler.index('master_prefs.begin('))
        self.assertLess(validation, handler.index('master_cmd_set_universal_profile('))
        self.assertIn('web.send(400, "text/plain; charset=utf-8", message);', handler)

    def test_module_still_enforces_the_access_contract(self):
        source = (ROOT / 'Module/UniversalRs232Module/UniversalRs232Module.ino').read_text(encoding='utf-8')
        self.assertIn('if (profile_entity_readable(e) && !e.match[0]) return false;', source)
        self.assertIn('if (profile_entity_writable(e) && !e.set_cmd[0] && !e.set_on[0] && !e.set_off[0]) return false;', source)

    def test_modbus_module_keeps_its_own_validation(self):
        source = (ROOT / 'Module/ModbusRtuModule/ModbusRtuModule.ino').read_text(encoding='utf-8')
        self.assertIn('if (profile_entity_readable(e) && !entity_read_func_code(e)) return false;', source)
        self.assertIn('if (profile_entity_writable(e) && !entity_write_func_code(e)) return false;', source)
        self.assertIn('if (!parse_u16_value(reg, reg_value)) return false;', source)


if __name__ == '__main__':
    unittest.main()
