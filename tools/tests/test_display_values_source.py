"""Guard the negotiated value stream, presentation reuse and bounded pacing."""
from pathlib import Path
import unittest
from tools.tests.display_ui_invariants import function

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('DisplayModule_320x480', 'DisplayModule_800x480', 'DisplayModule_ST7796_320x480')


def master_function(source, name):
    for result in ('bool', 'void'):
        source = source.replace(result + ' MasterScheduler::' + name + '(', 'static ' + result + ' ' + name + '(')
    return function(source, name)


class DisplayValueStreamSource(unittest.TestCase):
    def test_shared_wire_and_opt_in_in_both_master_paths(self):
        wire = (ROOT / 'OpenFumeExtractorMaster/src/DisplayValueStream.h').read_bytes()
        master = (ROOT / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertEqual(master.count('ofe_display_values::decodeRequest(resp.payload + rp'), 2)
        self.assertIn('display && display->display_values_supported ? 6 : 5', master)
        for name in NAMES:
            base = ROOT / 'Module' / name
            self.assertEqual(wire, (base / 'src/DisplayValueStream.h').read_bytes())
            source = (base / (name + '.ino')).read_text(encoding='utf-8')
            response = function(source, 'send_display_status_response')
            self.assertIn('ofe_display_values::CAPABILITY', response)
            self.assertIn('append_display_value_request(resp, display_view_arg', response)
            self.assertIn('append_display_value_request(resp, uni_addr)', response)
            status = function(source, 'handle_display_status')
            self.assertIn('req.payload[55] >= 6', status)
            pages = function(source, 'handle_display_detail_page')
            self.assertLess(pages.index('ofe_display_values::PAGE_VERSION'), pages.index('ofe_jbc_detail::VERSION'))

    def test_compact_values_reuse_full_page_presentation(self):
        master = (ROOT / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text(encoding='utf-8')
        page = master_function(master, 'sendDisplayUniversalEntityPage')
        self.assertIn('display->display_value_request.crc == rec.universal_descriptor_crc', page)
        self.assertIn('display->display_value_request.count == total_entities', page)
        self.assertEqual(page.count('descriptor_display_state_text('), 1)
        self.assertLess(page.index('descriptor_display_state_text('), page.index('auto& value = snapshot->values'))
        self.assertLess(page.index('return send_values(*snapshot)'), page.index('displayUniversalProfileEntityCount(rec)'))
        self.assertIn('value.number = value_v', page)
        self.assertIn('memcpy(value.text, state_text, strlen(state_text) + 1)', page)
        self.assertIn('ofe_display_values::encodePage(', page)

    def test_fanio_fast_readback_updates_the_common_entity_cache(self):
        master = (ROOT / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text(encoding='utf-8')
        readback = master_function(master, 'readIoStatus')
        self.assertIn('sync_fanio_gpio_entities(*rec, physical_input_mask)', readback)
        self.assertLess(readback.index('resp.payload[0] != STATUS_OK'), readback.index('sync_fanio_gpio_entities('))
        sync = function(master, 'sync_fanio_gpio_entities')
        self.assertIn('MODULE_FAN_IO_PRO', sync)
        self.assertNotIn('universal_entity_count++', sync)
        self.assertNotIn('universal_entities_valid =', sync)

    def test_readback_metadata_and_periodic_loss_repair(self):
        first = None
        for name in NAMES:
            state = (ROOT / 'Module' / name / 'src/OfeDisplayValueStreamState.h').read_bytes()
            if first is None:
                first = state
            self.assertEqual(first, state)
            text = state.decode('utf-8')
            self.assertIn('values_language != display_language', text)
            self.assertIn('values_refresh_ms) >= 10000U', text)
            self.assertIn('values_refresh_mask', text)
            receive = function(text, 'handle_display_value_page')
            self.assertLess(receive.index('if (!found)'), receive.index('send_status_response(req, STATUS_OK)'))
            self.assertIn('universal_pending_observe(page.addr, entity)', receive)
            self.assertIn('ui_needs_defer()', receive)
            self.assertIn('ui_defer_flags', receive)

    def test_scheduling_stays_bounded_and_does_not_change_safety_polls(self):
        master = (ROOT / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text(encoding='utf-8')
        background = master_function(master, 'pollOneBackgroundJob')
        self.assertIn('ofe_display_values::statusSlot(displays, DISPLAY_STATUS_SLOT_MS)', background)
        push = master_function(master, 'pushDisplayStatus')
        self.assertIn('if (rec.display_async_pending) continue', push)
        self.assertIn('if (module_fw_active_ || display_update_active_) return', push)
        cache = master_function(master, 'pushDisplayCache')
        self.assertIn('if (rec.display_async_pending) continue', cache)
        self.assertIn('(values ? 80UL : 300UL)', cache)
        self.assertIn(': 550UL', cache)


if __name__ == '__main__':
    unittest.main()
