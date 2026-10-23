"""Compile the module's actual function-name decoder alongside the preflight."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3]
source = (root / 'Module/ModbusRtuModule/ModbusRtuModule.ino').read_text(encoding='utf-8')


def function(name):
    match = re.search(r'^static [^\n]*\b' + name + r'\([^\n]*\) \{', source, re.M)
    if not match:
        raise RuntimeError(name)
    pos = source.index('{', match.start())
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


decoder = function('modbus_func_code')
aliases = re.findall(r'str_eq_ci\(name, "([^"]+)"\)', decoder)
parts = [function(name) for name in ('str_eq_ci', 'modbus_func_code', 'modbus_func_is_read', 'modbus_func_is_write')]
parts.append('static const char* module_function_aliases[] = {' + ','.join('"' + alias + '"' for alias in aliases) + '};')
Path(sys.argv[1]).write_text('\n\n'.join(parts), encoding='utf-8')
