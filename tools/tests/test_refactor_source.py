"""Guard cross-task ownership and shared transaction integration."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RefactorIntegration(unittest.TestCase):
    def test_offline_modules_stop_accumulating_misses_in_both_request_paths(self):
        source = (ROOT / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text(encoding='utf-8')
        sync = source.split('bool MasterScheduler::request(', 1)[1].split('bool MasterScheduler::startAsyncDisplayRequest(', 1)[0]
        async_timeout = source.split('void MasterScheduler::recordRequestTimeout(', 1)[1].split('\n}', 1)[0]
        for path in (sync, async_timeout):
            self.assertLess(path.index('const bool was_online = rec->online;'), path.index('if (was_online) {'))
            self.assertLess(path.index('if (was_online) {'), path.index('rec->miss_count++'))

    def test_logic_slot_write_verifies_staging_before_replace(self):
        source = (ROOT / 'OpenFumeExtractorMaster/src/WebLogic.inc.h').read_text(encoding='utf-8')
        save = source.split('static bool logic_save_fs(', 1)[1].split('static String logic_load_fs(', 1)[0]
        self.assertLess(save.index('LittleFS.open(staged_path, "w")'),
                        save.index('LittleFS.open(staged_path, "r")'))
        self.assertLess(save.index('memcmp(chunk, stored.c_str() + offset, count)'),
                        save.index('LittleFS.rename(staged_path, path)'))
        slot_save = source.split('static bool save_logic_slot_json(', 1)[1].split('\n}', 1)[0]
        self.assertIn('logic_fs_ready ? logic_save_fs(slot, stored) : logic_save_legacy_nvs(slot, stored)', slot_save)

    def test_backup_checks_logic_before_writing_settings(self):
        source = (ROOT / 'OpenFumeExtractorMaster/src/WebConfig.inc.h').read_text(encoding='utf-8')
        restore = source.split('static bool apply_config_backup_json_impl(', 1)[1].split('\n}', 1)[0]
        self.assertLess(restore.index('logic_compile_json(slot_json, i, compiled)'),
                        restore.index('netcfg_save('))
        self.assertLess(restore.index('logic_save_fs(i, slot_json)'),
                        restore.index('remove_logic_slot_storage_open(i)'))
        self.assertIn('failed to restore logic slot', restore)
        self.assertIn('failed to remove logic slot', restore)

    def test_web_routing_surfaces_failed_persistence(self):
        controls = (ROOT / 'OpenFumeExtractorMaster/src/WebControls.inc.h').read_text(encoding='utf-8')
        status = (ROOT / 'OpenFumeExtractorMaster/src/WebStatus.inc.h').read_text(encoding='utf-8')
        for message in ('failed to persist main input', 'failed to persist main output',
                        'failed to persist JBC route', 'failed to persist input route',
                        'failed to persist input rule'):
            self.assertIn('web.send(507, "text/plain; charset=utf-8", "' + message + '")', controls)
        for name in ('saveMainInput', 'saveOutput', 'routeSet', 'ruleChanged', 'addRule', 'deleteRule'):
            action = status.split('async function ' + name + '(', 1)[1].split('async function ', 1)[0]
            self.assertIn('routingSaveOk(r)', action)

    def test_identical_profile_transaction_headers(self):
        paths = [ROOT / f'Module/{name}/src/OfeStateTransaction.h' for name in
                 ('UniversalRs232Module', 'ModbusRtuModule')]
        self.assertEqual(paths[0].read_bytes(), paths[1].read_bytes())

    def test_both_profile_parsers_check_durable_text_before_commit(self):
        for name in ('UniversalRs232Module', 'ModbusRtuModule'):
            source = (ROOT / f'Module/{name}/{name}.ino').read_text(encoding='utf-8')
            parser = source[source.index('static bool apply_profile_text('):]
            self.assertLess(parser.index('StateTransaction transaction'), parser.index('profile_clear_entities();', parser.index('StateTransaction transaction')))
            self.assertLess(parser.index('prefs.putString("profile_text", cfg) != cfg.length()'), parser.index('transaction.commit()'))

    def test_display_timeout_has_one_owner(self):
        for resolution in ('320x480', '800x480', 'ST7796_320x480'):
            source = (ROOT / f'Module/DisplayModule_{resolution}/DisplayModule_{resolution}.ino').read_text(encoding='utf-8')
            self.assertIn('std::atomic<bool> fw_update_active', source)
            for counter in ('started_ms', 'offset', 'size'):
                self.assertIn('std::atomic<uint32_t> fw_update_' + counter, source)
            task = source[source.index('static void rs485_task('):source.index('void setup()')]
            self.assertIn('fw_update_check_timeout();', task)
            loop = source[source.index('void loop()'):]
            self.assertEqual(loop.count('fw_update_check_timeout();'), 1)
            fallback = loop[loop.index('if (!rs485_task_started)'):]
            self.assertLess(fallback.index('fw_update_check_timeout();'), fallback.index('\n  }'))

    def test_failsafe_controls_repeat_direction(self):
        source = (ROOT / 'Module/UniversalRs232Module/UniversalRs232Module.ino').read_text(encoding='utf-8')
        self.assertIn('!(output_failsafe_active && profile_output_entity(e))', source)
        self.assertIn('repeat_on ? e.repeat_on_ms : e.repeat_off_ms', source)

    def test_reset_cancels_deferred_power_save(self):
        for name in ('FanIoModule', 'FanIoProModule'):
            source = (ROOT / f'Module/{name}/{name}.ino').read_text(encoding='utf-8')
            reset = source.split('case CMD_FACTORY_RESET:',1)[1].split('prefs.clear()',1)[0]
            self.assertIn('manual_output_power_dirty = false;', reset)
            self.assertIn('manual_output_power_save_due_ms = 0;', reset)
            self.assertIn('prefs.putUShort("power", manual_output_power) == sizeof(manual_output_power)', source)

    def test_bus_deadlines_match_observed_outliers_without_blocking_display(self):
        header = (ROOT / 'OpenFumeExtractorMaster/src/MasterScheduler.h').read_text(encoding='utf-8')
        scheduler = (ROOT / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('#define MODULE_STATUS_TIMEOUT_MS 80', header)
        self.assertIn('#define JBC_BUS_STATE_TIMEOUT_MS 120', header)
        self.assertIn('#define DISPLAY_WIFI_ASYNC_TIMEOUT_MS 1000UL', header)
        self.assertIn('CMD_GET_STATUS, nullptr, 0, resp, MODULE_STATUS_TIMEOUT_MS', scheduler)
        self.assertIn('CMD_GET_STATE, nullptr, 0, resp, JBC_BUS_STATE_TIMEOUT_MS', scheduler)
        for command in ('CMD_GET_TELEMETRY', 'CMD_DISPLAY_STATUS', 'CMD_DISPLAY_ALARMS',
                        'CMD_DISPLAY_MODULE_LIST', 'CMD_DISPLAY_MODULE_DETAIL', 'CMD_DISPLAY_DETAIL_PAGE'):
            self.assertRegex(scheduler, rf'startAsyncDisplayRequest\([^;\n]*{command}')
        self.assertIn('else if (timeout_ms > 1200UL) timeout_ms = 1200UL;', scheduler)

    def test_master_ota_commit_defers_reboot_and_rejects_abort(self):
        source = (ROOT / 'OpenFumeExtractorMaster/src/WebUpdate.inc.h').read_text(encoding='utf-8')
        handler = source.split('static void web_handle_master_chunk_end() {', 1)[1].split(
            'static void web_handle_master_chunk_abort()', 1)[0]
        self.assertIn('Update.end(true)', handler)
        self.assertIn('master_update_restart_due_ms.store(', handler)
        self.assertNotIn('ESP.restart()', handler)
        abort = source.split('static void web_handle_master_chunk_abort() {', 1)[1].split('\n}', 1)[0]
        self.assertLess(abort.index('master_update_restart_due_ms.load()'),
                        abort.index('master_update_abort_active()'))
        loop = (ROOT / 'OpenFumeExtractorMaster/src/MasterLoop.inc.h').read_text(encoding='utf-8')
        self.assertIn('master_update_restart_due_ms.load()', loop)
        self.assertIn('prepare_controlled_restart();', loop)


if __name__ == '__main__':
    unittest.main()
