"""Shared design components stay transport-independent and identical on all boards."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('DisplayModule_320x480', 'DisplayModule_800x480',
         'DisplayModule_ST7796_320x480', 'DisplayModule_P4_800x480')


class DisplayUiTests(unittest.TestCase):
    def test_shared_design_components(self):
        base = ROOT / 'Module/DisplayModule_800x480/src'
        for name in NAMES:
            for file in ('OfeDisplayUi.h', 'OfeDisplaySettingsUi.inc.h', 'OfeJbcDetailUi.h',
                         'OfeDisplayHomeUi.inc.h', 'OfeDisplayDetailUi.inc.h', 'OfeJbcActivityUi.h',
                         'OfeDisplayHeaderUi.h', 'OfeDisplayHomeActivityUi.h', 'OfeDisplayBridgeHomeUi.inc.h', 'OfeParkingIcon.h', 'OfeFanIcon.h'):
                self.assertEqual((base / file).read_bytes(), (ROOT / 'Module' / name / 'src' / file).read_bytes())

    def test_common_controls_and_no_transport_or_settings_side_effects(self):
        ui = (ROOT / 'Module/DisplayModule_800x480/src/OfeDisplayUi.h').read_text()
        for expected in ('CONTROL_HEIGHT = 44', 'RADIUS = 6', 'LV_GRAD_DIR_NONE',
                         'UI_FONT_DEFAULT', 'UI_FONT_18', 'LV_STATE_DISABLED',
                         'LV_PART_SELECTED', 'lv_obj_set_ext_click_area(obj, 6)'):
            self.assertIn(expected, ui)
        for forbidden in ('WiFi.', 'Preferences', 'queue_display_event(', 'set_brightness(',
                          'lv_timer_create(', 'lv_anim_start(', 'heap_caps_malloc('):
            self.assertNotIn(forbidden, ui)

    def test_real_backlight_builder_is_identical(self):
        builders = []
        for name in NAMES[:3]:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            start = source.index('static void lv_create_backlight_controls(')
            end = source.index('\n}\n', start) + 3
            builders.append(source[start:end])
            self.assertIn('#include "src/OfeDisplayUi.h"', source)
        self.assertTrue(all(source == builders[0] for source in builders))
        self.assertEqual(builders[0].count('ofe_display_ui::dropdown('), 2)
        self.assertIn('i <= 60', builders[0])
        self.assertEqual(builders[0].count('lv_backlight_settings_event'), 2)

    def test_compact_back_arrows(self):
        for name in NAMES[:3]:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            self.assertIn('button(ui_module_detail_screen, 12, 48, 44, LV_SYMBOL_LEFT, lv_back_to_modules_event)', source)
            self.assertRegex(source, r'button\(ui_benchmark_screen, 12, (272|430), 44, LV_SYMBOL_LEFT, display_benchmark_back_event\)')
            self.assertIn('ui_detail_title = lv_label(ui_module_detail_screen, "Module", 68, 58,', source)

    def test_home_navigation_and_direct_jbc_loading(self):
        from tools.tests.display_ui_invariants import function
        for name in NAMES[:3]:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            nav = function(source, 'lv_add_main_nav')
            self.assertIn('ofe_display_ui::navigation(screen, active, labels, callbacks);', nav)
            self.assertNotIn('lv_small_button(', nav)
            opening = function(source, 'lv_open_module_summary')
            self.assertLess(opening.index('lv_update_jbc_detail('), opening.index('lv_screen_switch('))
            self.assertIn('selected_module.caps = module_summaries[index].caps;', opening)
            self.assertIn('lv_style_home_page();', source)
            self.assertIn('lv_obj_set_style_border_width(ui_alarm_rows[i], 1, 0);', source)
            self.assertIn('lv_obj_set_style_shadow_color(ui_alarm_rows[index], critical', source)

    def test_generic_values_and_controls_do_not_overlap(self):
        for name in NAMES[:3]:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            self.assertIn('lv_detail_values_view(values, detail_fields_is_jbc(m.type, m.caps));', source)
            self.assertIn('const uint16_t text_bottom = (uint16_t)lv_detail_values_bottom();', source)

    def test_shared_numeric_and_responsive_controls(self):
        base = ROOT / 'Module/DisplayModule_800x480/src'
        for name in NAMES:
            for file in ('OfeDisplayControlsUi.inc.h', 'OfeDisplayNumericUi.h'):
                self.assertEqual((base / file).read_bytes(), (ROOT / 'Module' / name / 'src' / file).read_bytes())
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            self.assertIn('#include "src/OfeDisplayControlsUi.inc.h"', source)
            self.assertIn('ofe_display_ui::numeric_dialog(lv_screen_active()', source)
            self.assertNotIn('lv_keyboard_create(lv_layer_top())', source)
            self.assertIn('lv_obj_set_style_text_align(input, LV_TEXT_ALIGN_CENTER, 0)', source)
        controls = (base / 'OfeDisplayControlsUi.inc.h').read_text()
        for callback in ('lv_detail_universal_switch_event', 'lv_detail_universal_select_event',
                         'lv_detail_universal_slider_event', 'lv_detail_weller_reset_event',
                         'lv_detail_output_power_slider_event'):
            self.assertIn(callback, controls)
        self.assertIn('ofe_display_ui::control_cell(ui_detail_controls, row, 152)', controls)

    def test_master_detail_system_values_are_not_duplicated(self):
        from tools.tests.display_ui_invariants import function
        for name in NAMES:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            update = function(source, 'lv_update_module_detail')
            master = update.split('if (m.addr == ADDR_MASTER) {', 1)[1].split('} else if', 1)[0]
            self.assertIn('m.master_ip', master)
            for duplicate in ('Heap:', 'CPU:', 'Uptime:', 'Laufzeit:', 'Loop max:'):
                self.assertNotIn(duplicate, master)
            self.assertEqual(update.count('values += lv_detail_system_text();'), 1)

    def test_header_optical_alignment(self):
        from tools.tests.display_ui_invariants import function
        for name in NAMES:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            header = function(source, 'lv_add_header')
            self.assertIn('lv_obj_set_size(ble_lbl,20,20)', header)
            self.assertIn('center_glyph(ble_lbl, 0xE001)', header)
            self.assertIn('center_glyph(alarm_lbl, 0xF0F3)', header)
            self.assertIn('center_clock(clock_lbl)', header)
            self.assertIn('center_clock(ui_header_clock_labels[i])', function(source, 'lv_update_clock_ui'))

    def test_detail_identity_alignment_and_content_fitting(self):
        base = ROOT / 'Module/DisplayModule_800x480/src'
        jbc = (base / 'OfeJbcDetailUi.h').read_text()
        self.assertIn('0x%02X  FW %s  |  ', jbc)
        self.assertIn('jbc_detail_identity', jbc)
        self.assertNotIn('jbc_detail_summary', jbc)
        self.assertIn('jbc_detail_link', jbc)
        self.assertIn('lv_obj_set_pos(jbc_detail_list, 0, 76)', jbc)
        self.assertIn('ofe_display_ui::detail_alignment(ui_detail_controls)', (base / 'OfeDisplayControlsUi.inc.h').read_text())
        self.assertIn('lv_fit_home_fan_card()', (base / 'OfeDisplayHomeUi.inc.h').read_text())
        from tools.tests.display_ui_invariants import function
        for name in NAMES[:3]:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            refresh = function(source, 'lv_update_app_values')
            self.assertEqual(refresh.count('lv_layout_home_cards(master_ok);'), 1)
            self.assertGreater(refresh.index('lv_layout_home_cards(master_ok);'), refresh.index('lv_set_text(ui_home_fan_detail, fan_detail);'))
            if name != 'DisplayModule_800x480':
                self.assertIn('lv_obj_set_height(ui_update_target, 34);', source)
                self.assertIn('ui_update_status = lv_label(hero, "", 16, 112,', source)

    def test_activity_icons_and_uniform_home_labels(self):
        base = ROOT / 'Module/DisplayModule_800x480/src'
        activity = (base / 'OfeJbcActivityUi.h').read_text()
        self.assertIn('lv_color_hex(0xFFB020)', activity)
        self.assertIn('!strcmp(value, "an")', activity)
        home = (base / 'OfeDisplayHomeActivityUi.h').read_text()
        for expected in ('LV_ANIM_REPEAT_INFINITE', 'LV_EVENT_SCREEN_UNLOAD_START', 'lv_anim_delete',
                         'saver_lit', 'lv_image_set_antialias'):
            self.assertIn(expected, home)
        from tools.tests.display_ui_invariants import function
        for name in NAMES:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            self.assertIn('lv_obj_set_pos(ui_module_list, 10, 44)', source)
            self.assertIn('tr("Afterrun (s)", "Nachlauf (s)")', source)
            self.assertIn('tr("Afterrun power %", "Nachlaufleistung %")', source)
            self.assertIn('ofe_home_activity::create_saver(ui_screensaver_screen, ui_screensaver_power)', function(source, 'lv_create_screensaver_screen'))
            self.assertIn('ofe_home_activity::move_saver(ui_screensaver_power)', function(source, 'screensaver_update_values'))

    def test_home_bridge_cards_stand_inference_and_fixed_large_controls(self):
        from tools.tests.display_ui_invariants import function
        home = (ROOT / 'Module/DisplayModule_800x480/src/OfeDisplayHomeUi.inc.h').read_text()
        self.assertIn('lv_obj_set_parent(card, ui_home_module_content)', home)
        self.assertIn('lv_obj_remove_flag(ui_home_content, LV_OBJ_FLAG_SCROLLABLE)', home)
        self.assertIn('module.type == MODULE_FAN_IO || module.type == MODULE_FAN_IO_PRO', home)
        self.assertIn('fae_stand(true, module.live_jbc_work)', home)
        bridge = (ROOT / 'Module/DisplayModule_800x480/src/OfeDisplayBridgeHomeUi.inc.h').read_text()
        self.assertIn('namespace ofe_module_home', bridge)
        self.assertIn('home_metrics_get(module.addr, module.type', bridge)
        self.assertIn('lv_open_module_summary(i)', bridge)
        self.assertIn('card->addr', bridge)
        self.assertIn('ofe_home_metrics::remaining(state)', bridge)
        self.assertIn('module_summary_provides_extractor_output(module)', bridge)
        self.assertIn('lv_obj_add_event_cb(card.root, open, LV_EVENT_CLICKED, &card)', bridge)
        for name in NAMES:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            detail = function(source, 'lv_update_module_detail')
            self.assertIn('lv_set_detail_work_icon(false, false)', detail)
            self.assertIn('ofe_jbc_activity_ui::fae_stand(module_online && m.station_addr', detail)

    def test_home_metrics_wire_and_cache_are_shared_and_negotiated(self):
        master = (ROOT / 'OpenFumeExtractorMaster/src').resolve()
        wire = (master / 'DisplayHomeMetrics.h').read_bytes()
        scheduler = (master / 'MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('display->display_home_metrics_supported ? 3U : 2U', scheduler)
        self.assertEqual(scheduler.count('display_home_metrics_supported = resp.payload[rp++] == ofe_home_metrics::VERSION'), 2)
        self.assertIn('live_can_write_to(1U + ofe_home_metrics::WIRE_SIZE, native_end)', scheduler)
        for name in NAMES:
            base = ROOT / 'Module' / name
            self.assertEqual(wire, (base / 'src/DisplayHomeMetrics.h').read_bytes())
            source = (base / (name + '.ino')).read_text(encoding='utf-8')
            self.assertIn('resp.payload[resp.len++] = ofe_home_metrics::CAPABILITY;', source)
            self.assertIn('p + ofe_home_metrics::WIRE_SIZE > block_end', source)
            self.assertIn('display_home_metrics_cache.put(next, millis())', source)

    def test_home_profiles_show_every_configured_entity_per_address(self):
        from tools.tests.display_ui_invariants import function
        for name in NAMES:
            base = ROOT / 'Module' / name
            cards = (base / 'src/OfeDisplayBridgeHomeUi.inc.h').read_text(encoding='utf-8')
            self.assertIn('universal_cache_find(module.addr, false)', cards)
            self.assertIn('cache->type == module.type', cards)
            self.assertIn('i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX', cards)
            self.assertIn('universal_entity_value_text(entity)', cards)
            self.assertNotIn('shown <', cards)
            source = (base / (name + '.ino')).read_text(encoding='utf-8')
            refresh = function(source, 'first_universal_module_addr_needing_cache')
            self.assertIn('next = (i + 1) % count', refresh)
            self.assertNotIn('!c->universal_entity_total ||', refresh)
            home = (base / 'src/OfeDisplayHomeUi.inc.h').read_text()
            self.assertIn('lv_obj_set_pos(stripe, 4, 12)', home)

    def test_ui_capacity_includes_persistent_home_and_detail_widgets(self):
        from tools.tests.display_ui_invariants import function
        for name in NAMES:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            init = function(source, 'lvgl_init_ui')
            self.assertIn('widget_pools[11]', init)
            self.assertIn('i < 11', init)
            self.assertIn('pool_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT', init)

    def test_header_shows_the_screensaver_green_check_when_alarm_free(self):
        from tools.tests.display_ui_invariants import function
        for name in NAMES:
            source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
            update = function(source, 'lv_update_alarm_header_ui')
            self.assertIn('lv_set_text_c(alarm, LV_SYMBOL_OK)', update)
            self.assertIn('lv_color_hex(0x55D98A)', update)
            self.assertNotIn('lv_set_visible(alarm, false)', update)
            self.assertIn('active_alarm_count ? 0xF0F3 : 0xF00C', update)

    def test_home_module_palette_matches_master(self):
        import re
        web = (ROOT / 'OpenFumeExtractorMaster/src/WebStatus.inc.h').read_text(encoding='utf-8')
        activity = (ROOT / 'Module/DisplayModule_800x480/src/OfeDisplayHomeActivityUi.h').read_text()
        for selector in ('fan-card', 'weller-card', 'universal-card', 'universal-card.modbus-card'):
            color = re.search(r'\.module-card\.' + re.escape(selector) + r'\{--accent:#([0-9a-fA-F]{6})', web).group(1)
            self.assertIn('0x' + color.upper(), activity)


if __name__ == '__main__':
    unittest.main()
