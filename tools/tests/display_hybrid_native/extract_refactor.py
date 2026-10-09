"""Extract the actual Modbus transaction functions for native regression tests."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3]
source = (root / 'Module/ModbusRtuModule/ModbusRtuModule.ino').read_text(encoding='utf-8')

def function(name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^\n]*\) \{', source, re.M)
    if not match:
        raise RuntimeError(name)
    start = match.start()
    pos = source.index('{', start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

names = ['modbus_func_is_read', 'modbus_func_is_write', 'modbus_send_request',
         'modbus_queue_write', 'modbus_reset_exchange', 'process_profile_rx',
         'modbus_response_timeout_ms', 'profile_poll_tick']
Path(sys.argv[1]).write_text('\n\n'.join(function(name) for name in names), encoding='utf-8')
source = (root / 'OpenFumeExtractorMaster/src/WebSecurity.inc.h').read_text(encoding='utf-8')
Path(sys.argv[1]).with_name('generated_security.inc.h').write_text(
    function('web_check_credentials') + '\n' + function('web_require_config_auth'), encoding='utf-8')
source = (root / 'OpenFumeExtractorMaster/src/MasterMqtt.inc.h').read_text(encoding='utf-8')
Path(sys.argv[1]).with_name('generated_mqtt_offline.inc.h').write_text(
    function('mqtt_publish_all_availability_offline'), encoding='utf-8')
for module in ('FanIoModule', 'FanIoProModule'):
    source = (root / f'Module/{module}/{module}.ino').read_text(encoding='utf-8')
    Path(sys.argv[1]).with_name(f'generated_{module}_power.inc.h').write_text(
        function('poll_output_power_save'), encoding='utf-8')
prefixes = []
for index, display in enumerate(('320x480', '800x480', 'ST7796_320x480')):
    source = (root / f'Module/DisplayModule_{display}/DisplayModule_{display}.ino').read_text(encoding='utf-8')
    prefix = function('parse_display_module_detail_record').split('  if (selected_module.valid')[0]
    prefixes.append(prefix.replace('parse_display_module_detail_record', f'parse_prefix_{index}') + '  return true;\n}')
fields = sorted(set(re.findall(r'\bm\.(\w+)', prefixes[0])) - {'fw_suffix', 'name', 'jbc_device_id'})
fixture = 'struct DisplayModuleDetail { uint32_t ' + ','.join(fields) + '; char fw_suffix[16],name[64]; uint8_t jbc_device_id[40]; };\n'
Path(sys.argv[1]).with_name('generated_detail_prefix.inc.h').write_text(fixture + '\n'.join(prefixes), encoding='utf-8')
