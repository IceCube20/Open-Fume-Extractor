from pathlib import Path
import unittest

from tools.tests.display_ui_invariants import function


ROOT = Path(__file__).resolve().parents[2]
NAMES = ('DisplayModule_320x480', 'DisplayModule_800x480',
         'DisplayModule_ST7796_320x480', 'DisplayModule_P4_800x480')


class DisplayModuleCardTests(unittest.TestCase):
    def test_health_and_bar_helpers_are_shared_without_transport_side_effects(self):
        helpers = [(ROOT / 'Module' / name / 'src/OfeDisplayModuleCardsUi.inc.h').read_bytes() for name in NAMES]
        self.assertTrue(all(helper == helpers[0] for helper in helpers))
        text = helpers[0].decode('utf-8')
        for forbidden in ('bus.send(', 'queue_display_event(', 'lv_anim_start(', 'Preferences', 'WiFi.'):
            self.assertNotIn(forbidden, text)
        self.assertIn('uint32_t(now - received) <= 10000U', text)
        self.assertIn('i < capacity', text)
        self.assertNotIn('FAST_FLAG_ERROR_PENDING', text)
        self.assertIn('status.master_alarm_critical_mask & (1U << i)', text)

    def test_all_lists_use_actual_target_progress_and_reset_widgets_on_rebuild(self):
        for name in NAMES:
            with self.subTest(display=name):
                sketch = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8-sig')
                update = function(sketch, 'lv_update_module_list')
                self.assertIn('ofe_module_cards::row_height(row_is_updating)', update)
                self.assertIn('ofe_module_cards::row_progress(ui_module_row_progress[i], ui_module_rows[i], row_is_updating, status.update_progress)', update)
                self.assertIn('ofe_module_cards::severity(m, true)', update)
                self.assertIn('row_y += height + 6;', update)
                self.assertIn('module_summaries[i].valid) ofe_module_cards::create_row(i)', update)
                self.assertIn('ui_module_row_progress[i] = nullptr;', function(sketch, 'lv_create_app_screens'))
                self.assertIn('lv_obj_get_y(ui_module_rows[(uint8_t)idx])', function(sketch, 'bus_update_scroll_to_target'))
                target = function(sketch, 'bus_update_is_for_addr')
                self.assertIn('status.update_target == addr', target)
                self.assertNotIn('ADDR_MASTER', target)

    def test_bars_allocate_only_for_updates_and_do_not_animate(self):
        helper = (ROOT / 'Module/DisplayModule_800x480/src/OfeDisplayModuleCardsUi.inc.h').read_text()
        self.assertIn('if (updating && !bar)', helper)
        self.assertIn('lv_obj_set_flag(bar, LV_OBJ_FLAG_HIDDEN, !updating)', helper)
        self.assertIn('progress > 100 ? 100 : progress, LV_ANIM_OFF', helper)
        self.assertIn('lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE)', helper)


if __name__ == '__main__':
    unittest.main()
