"""Regression checks for persistent routing and backup restore failure paths."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster' / 'src'


class SettingsTransactionSourceTest(unittest.TestCase):
    def test_network_writes_use_verified_store_and_keep_existing_mutex_ownership(self):
        source = (ROOT / 'MasterNetwork.inc.h').read_text(encoding='utf-8')
        for method in ('putString', 'putBool', 'putUChar', 'putUShort'):
            self.assertNotIn('.' + method + '(', source)
            self.assertIn('MasterSettingsStore::' + method + 'Verified(', source)
        save = source.split('static bool netcfg_save(', 1)[1].split('static bool netcfg_save_wifi(', 1)[0]
        self.assertLess(save.index('if (!ok) return false;'), save.index('ssid.toCharArray(wifi_ssid'))
        self.assertNotIn('MqttConfigGuard', save)

    def test_led_and_eco_ram_apply_only_after_store_success(self):
        source = (ROOT / 'MasterNetwork.inc.h').read_text(encoding='utf-8')
        for name, store, assignment in (
            ('save_status_led_config', 'saveStatusLed', 'status_led_enabled = enabled;'),
            ('save_module_power_save_config', 'savePowerSave', 'module_power_save_enabled = enabled;'),
        ):
            body = source.split('static bool ' + name + '(', 1)[1].split('\n}', 1)[0]
            self.assertLess(body.index('if (!MasterSettingsStore::' + store), body.index(assignment))

    def test_continuous_remains_transient_and_store_is_a_real_compilation_unit(self):
        header = (ROOT / 'MasterSettingsStore.h').read_text(encoding='utf-8')
        store = (ROOT / 'MasterSettingsStore.cpp').read_text(encoding='utf-8')
        config = (ROOT / 'WebConfig.inc.h').read_text(encoding='utf-8')
        self.assertNotIn('prefs.putBytes(', header)
        self.assertNotIn('.continuous', store)
        export = config.split('static String build_config_backup_json()', 1)[1].split('static bool apply_config_backup_json_impl', 1)[0]
        self.assertNotIn('"continuous"', export)
        scheduler = (ROOT / 'MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('MasterSettingsStore::normalizeMainInput(source)', scheduler)

    def test_rules_and_main_input_are_single_nvs_records(self):
        store = (ROOT / 'MasterSettingsStore.cpp').read_text(encoding='utf-8')
        self.assertIn('putBlobVerified(prefs, key.c_str(), blob, sizeof(blob))', store)
        self.assertIn('putBlobVerified(prefs, KEY_MAIN_IN_BLOB, blob, sizeof(blob))', store)
        self.assertIn('putBlobVerified(prefs, KEY_CFG_BLOB, blob, sizeof(blob))', store)
        self.assertIn('prefs.putBytes(key, blob, size) == size && blobMatches(', store)
        self.assertIn('if (blob_size)', store)

    def test_logic_arrays_and_entries_must_be_valid(self):
        logic = (ROOT / 'WebLogic.inc.h').read_text(encoding='utf-8')
        compile_body = logic.split('static bool logic_compile_json(', 1)[1].split('\n}', 1)[0]
        self.assertIn('!logic_array_bounds(json, "nodes"', compile_body)
        self.assertIn('!logic_array_bounds(json, "links"', compile_body)
        self.assertIn('out.node_count >= LOGIC_RT_MAX_NODES', compile_body)
        self.assertIn('out.link_count >= LOGIC_RT_MAX_LINKS', compile_body)
        self.assertIn('if (from < 0 || to < 0)', compile_body)

    def test_restore_has_undo_journal_and_boot_recovery(self):
        config = (ROOT / 'WebConfig.inc.h').read_text(encoding='utf-8')
        setup = (ROOT / 'MasterSetup.inc.h').read_text(encoding='utf-8')
        self.assertIn('backup_restore_write_undo(snapshot)', config)
        self.assertIn('apply_config_backup_json_impl(snapshot, rollback_error)', config)
        self.assertIn('recover_interrupted_backup_restore()', setup)
        self.assertIn('LittleFS.exists(BACKUP_RESTORE_UNDO_PATH)', config)

    def test_web_fails_closed_on_unsaved_commands(self):
        controls = (ROOT / 'WebControls.inc.h').read_text(encoding='utf-8')
        scheduler = (ROOT / 'MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('if (!apply_control_settings(suction, select_flow', controls)
        self.assertIn('if (!master_cmd_set_afterrun_power_profile(', controls)
        self.assertIn('master_cmd_set_input_rule(index, previous_rule)', controls)
        self.assertIn('parsed_bit > 7', controls)
        self.assertIn('bit >= ((rec->type == MODULE_FAN_IO || rec->type == MODULE_FAN_IO_PRO) ? 8 : 2)', scheduler)

    def test_afterrun_uses_the_combined_delayed_control_write(self):
        scheduler = (ROOT / 'MasterScheduler.cpp').read_text(encoding='utf-8')
        setter = scheduler.split('void MasterScheduler::setAfterrunPowerProfile(', 1)[1].split('\n}', 1)[0]
        self.assertIn('if (persist) scheduleControlSettingsPersist();', setter)
        self.assertNotIn('saveAfterrunPower(', setter)


if __name__ == '__main__':
    unittest.main()
