"""Render the real sketch's backlight control builder, not a test reimplementation."""
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
source = (root / 'Module/DisplayModule_800x480/DisplayModule_800x480.ino').read_text(encoding='utf-8')
start = source.index('static void lv_create_backlight_controls(')
end = source.index('\n}\n', start) + 3
Path(sys.argv[1]).write_text(source[start:end], encoding='utf-8')

sys.path.insert(0, str(root))
from tools.tests.display_ui_invariants import function

for name, output in (('universal_entity_value_text', 'universal_value.inc.h'),
                     ('first_universal_module_addr_needing_cache', 'home_profile_refresh.inc.h')):
    Path(sys.argv[1]).with_name(output).write_text(function(source, name), encoding='utf-8')

pages = []
helpers = []
headers = []
for name, suffix in (('DisplayModule_800x480', '800'), ('DisplayModule_320x480', '480')):
    source = (root / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
    app = function(source, 'lv_create_app_screens')
    if suffix == '800':
        headers.append(function(source, 'lv_create_eco_sprout'))
        headers.append(function(source, 'lv_update_alarm_header_ui'))
    headers.append(function(source, 'lv_add_header').replace('lv_add_header', 'build_header_' + suffix, 1).replace('DISPLAY_RGB_WIDTH', suffix))
    pages.append(function(source, 'lv_layout_home_cards').replace('lv_layout_home_cards', 'layout_home_' + suffix, 1))
    start = app.index('  ui_dashboard_screen =')
    end = app.index('  ui_module_list_screen =')
    pages.append('static void build_home_' + suffix + '() {\n' + app[start:end] + '\n}\n')
    if suffix == '800':
        helpers = [function(source, name) for name in ('lv_home_status_card', 'lv_mini_bar')]
        numeric = [function(source, name) for name in ('lv_numeric_hide', 'lv_numeric_keyboard_event',
                   'lv_numeric_field_event', 'lv_numeric_field')]
        Path(sys.argv[1]).with_name('generic_numeric.inc.h').write_text('\n'.join(numeric), encoding='utf-8')
        sizing = [function(source, name) for name in ('lv_detail_controls_y', 'lv_detail_controls_h')]
        Path(sys.argv[1]).with_name('control_sizing.inc.h').write_text('\n'.join(sizing), encoding='utf-8')
    markers = ('ui_module_list_screen', 'ui_alarm_screen', 'ui_module_detail_screen', 'ui_system_screen', 'ui_benchmark_screen')
    for kind, begin, finish in (('modules', 0, 1), ('alarms', 1, 2), ('details', 2, 3), ('benchmark', 4, None)):
        start = app.index('  ' + markers[begin] + ' =')
        end = app.index('  ' + markers[finish] + ' =', start) if finish is not None else app.rfind('\n}')
        pages.append('static void build_' + kind + '_' + suffix + '() {\n' + app[start:end] + '\n}\n')
    for kind in ('boot', 'update', 'status', 'screensaver'):
        symbol = 'lv_create_' + kind + '_screen'
        pages.append(function(source, symbol).replace(symbol, 'build_' + kind + '_' + suffix, 1).replace('DISPLAY_RGB_WIDTH', suffix))
    if suffix == '800':
        pages.append(function(source, 'lv_alarm_add'))
Path(sys.argv[1]).with_name('other_pages.inc.h').write_text('\n'.join(pages), encoding='utf-8')
Path(sys.argv[1]).with_name('home_helpers.inc.h').write_text('\n'.join(helpers), encoding='utf-8')
Path(sys.argv[1]).with_name('header_builders.inc.h').write_text('\n'.join(headers), encoding='utf-8')
