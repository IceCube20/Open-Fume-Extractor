import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('DisplayModule_320x480', 'DisplayModule_800x480',
         'DisplayModule_ST7796_320x480', 'DisplayModule_P4_800x480')


class JbcDisplayDetailTests(unittest.TestCase):
    def test_shared_wire_and_ui(self):
        canonical = (ROOT / 'OpenFumeExtractorMaster/src/bus/OfeJbcDetailWire.h').read_bytes()
        source = ROOT / 'Module/DisplayModule_800x480/src'
        for name in NAMES:
            folder = ROOT / 'Module' / name / 'src'
            self.assertEqual(canonical, (folder / 'OfeJbcDetailWire.h').read_bytes())
            for file in ('OfeJbcDetailState.h', 'OfeJbcDetailUi.h'):
                self.assertEqual((source / file).read_bytes(), (folder / file).read_bytes())

    def test_transport_parity_and_opt_in(self):
        source = (ROOT / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('consumeDisplayJbcExtension(display, resp, rp, marker)', source)
        self.assertIn('consumeDisplayJbcExtension(*display, resp, rp, marker)', source)
        self.assertEqual(source.count('? sendDisplayJbcPage(rec, *detail)'), 2)
        self.assertIn('CMD_DISPLAY_DETAIL_PAGE, payload, length, DISPLAY_WIFI_ASYNC_TIMEOUT_MS', source)
        self.assertIn('request(display.addr, CMD_DISPLAY_DETAIL_PAGE, payload, length, response, 60)', source)
        self.assertIn('u32(p + 4) == ofe_jbc_catalog::revision(*target)', source)
        self.assertIn('ha_jbc_control_payload(*target', source)
        self.assertIn('hash != display.display_jbc_command_hash', source)

    def test_no_hidden_screen_or_radio_work(self):
        for name in NAMES:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            self.assertIn('user_needs_detail && !screensaver_active && !fw_update_active', source)
            self.assertIn('display_jbc_state->accept(req.payload, req.len)', source)
            self.assertIn('if (accepted) ui_defer_flags(UI_DEFER_MODULE_DETAIL)', source)
            self.assertIn('jbc_detail_reset_ui();', source)
        state = (ROOT / 'Module/DisplayModule_800x480/src/OfeJbcDetailState.h').read_text()
        self.assertNotIn('WiFi', state)
        ui = (ROOT / 'Module/DisplayModule_800x480/src/OfeJbcDetailUi.h').read_text()
        self.assertIn('heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', ui)
        self.assertNotIn('MALLOC_CAP_INTERNAL', ui)
        self.assertIn('jbc_usb_core_detail_text(selected_module)', ui)
        self.assertIn('jbc_detail_list = lv_obj_create(jbc_detail_root)', ui)
        self.assertIn('jbc_detail_row[i] = lv_obj_create(jbc_detail_list)', ui)
        self.assertIn('lv_obj_scroll_to_y(jbc_detail_list, 0, LV_ANIM_OFF)', ui)
        self.assertIn('jbc_detail_text_height(label, UI_FONT_DEFAULT, label_width)', ui)
        self.assertIn('wide || snapshot.section != SETTINGS ? 2 : 1', ui)
        self.assertIn('jbc_detail_row_controls(i, number ? NUMBER', ui)

    def test_twelve_rows_with_legacy_negotiation(self):
        wire = (ROOT / 'OpenFumeExtractorMaster/src/bus/OfeJbcDetailWire.h').read_text()
        state = (ROOT / 'Module/DisplayModule_800x480/src/OfeJbcDetailState.h').read_text()
        scheduler = (ROOT / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text()
        self.assertIn('WINDOW = 12', wire)
        self.assertIn('LEGACY_REQUEST = 0xB0', wire)
        self.assertIn('REQUEST = 0xB2', wire)
        self.assertIn('uint16_t loaded', state)
        self.assertIn('marker == LEGACY_REQUEST', scheduler)
        self.assertIn('display.display_jbc_window == WINDOW', scheduler)

    def test_compact_numeric_dialog(self):
        ui = (ROOT / 'Module/DisplayModule_800x480/src/OfeJbcDetailUi.h').read_text()
        shared = (ROOT / 'Module/DisplayModule_800x480/src/OfeDisplayNumericUi.h').read_text()
        self.assertIn('ofe_display_ui::numeric_dialog(', ui)
        self.assertIn('const int width = large ? 400 : 360', shared)
        self.assertIn('keyboard_height = large ? 176 : 152', shared)
        self.assertIn('lv_obj_create(result.overlay)', shared)
        self.assertIn('lv_keyboard_create(dialog)', shared)
        self.assertIn('(screen_width - width) / 2, (body_height - height) / 2', shared)
        self.assertIn('lv_obj_set_style_text_align(result.input, LV_TEXT_ALIGN_CENTER, 0)', shared)
        self.assertIn('LV_EVENT_CANCEL', ui)

    def test_reuses_common_capabilities(self):
        source = (ROOT / 'OpenFumeExtractorMaster/src/DisplayJbcCatalog.h').read_text()
        for text in ('ha_jbc_control(m, id', 'ha_jbc_counter_supported', 'ha_jbc_counter_value',
                     'jbc_usb_core_state(m)', 'master_developer_mode_enabled_for_modules()'):
            self.assertIn(text, source)


if __name__ == '__main__':
    unittest.main()
