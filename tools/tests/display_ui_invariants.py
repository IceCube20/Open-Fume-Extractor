"""Preserve screensaver, live updates and shared helpers during page restyling."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('DisplayModule_320x480', 'DisplayModule_800x480', 'DisplayModule_ST7796_320x480')
FIXTURE = Path(__file__).with_name('display_ui_preserved.json')


def function(source, name):
    import re
    match = re.search(r'^static [^\n]*\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    if not match:
        raise ValueError(name)
    end = source.index('\n}\n', match.start()) + 3
    return source[match.start():end]


def fingerprints():
    result = {}
    for name in NAMES:
        source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8-sig')
        blocks = {}
        for symbol in ('lv_create_dashboard_screen', 'lv_create_screensaver_screen',
                       'screensaver_update_values', 'screensaver_enter', 'screensaver_wake',
                       'screensaver_tick', 'lv_update_app_values', 'lv_update_dashboard_values',
                       'lv_card', 'lv_small_button', 'lv_apply_dropdown_theme', 'lv_label',
                       'lv_style_screen'):
            blocks[symbol] = function(source, symbol)
            if symbol == 'lv_create_screensaver_screen':
                blocks[symbol] = blocks[symbol].replace(
                    '  ofe_home_activity::create_saver(ui_screensaver_screen, ui_screensaver_power);\n', '')
            if symbol == 'screensaver_update_values':
                blocks[symbol] = blocks[symbol].replace(
                    '  ofe_home_activity::move_saver(ui_screensaver_power);\n', '').replace(
                    '  ofe_home_activity::update_saver(master_link_online() && status.output_enabled, !display_backlight.isOff(), status.output_power / 10U);\n', '')
            if symbol == 'lv_update_app_values':
                # Only the ownership predicate was renamed for the GUI task.
                # Retain the original widget/update fingerprint otherwise.
                blocks[symbol] = blocks[symbol].replace(
                    'if (ui_needs_defer()) {', 'if (running_in_rs485_task()) {')
                # New Home-only indicators replace the textual Work suffix.
                indicators = (
                    '  ofe_home_activity::set_counts(lv_home_jbc_counts(master_ok));\n'
                    '  const auto fan_state = lv_home_fan_state(master_ok);\n'
                    '  ofe_home_activity::set_fan(0, true, master_ok && status.output_enabled, output_pct);\n'
                    '  ofe_home_activity::set_fan(1, fan_state.capable, fan_state.active,\n'
                    '    fan_io_state_known ? home_fan_power / 10U : fan_state.percent);\n'
                    '  ofe_home_activity::set_fan(2, true, master_ok && home_weller_connected && (home_weller_outputs & 0x01), home_weller_speed);\n'
                    '  lv_update_bridge_home_cards(master_ok);\n'
                )
                blocks[symbol] = blocks[symbol].replace(indicators, '').replace(
                    '? (String(tr("Station ", "Station ")) + home_jbc_station_models())',
                    '? (String(tr("Station ", "Station ")) + home_jbc_station_models() + tr(" | Work ", " | Work ") + on_off(status.work_mask != 0))')
                blocks[symbol] = blocks[symbol].replace(
                    'const uint8_t fan_list_count = lv_home_module_count(MODULE_FAN_IO, true);',
                    'const uint8_t fan_list_count = online_module_count_by_type(MODULE_FAN_IO);')
                # The UI-only card layout now follows text updates to measure
                # current content. Normalize that deliberate move, not logic.
                call = '  lv_layout_home_cards(master_ok);\n'
                blocks[symbol] = blocks[symbol].replace(call, '').replace(
                    '  if (ui_home_suction_title)', call + '  if (ui_home_suction_title)', 1)
        # Home construction/navigation are now explicitly in scope. Keep the
        # original baseline for live updates, screensaver and shared helpers.
        result[name] = {key: hashlib.sha256(value.encode()).hexdigest() for key, value in blocks.items()}
    return result


if __name__ == '__main__':
    import sys
    if sys.argv[1:] != ['--capture']:
        raise SystemExit('Use --capture only before deliberately scoped UI edits')
    FIXTURE.write_text(json.dumps(fingerprints(), indent=2) + '\n', encoding='utf-8')
