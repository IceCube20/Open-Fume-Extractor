"""Compile and run actual logic parser/save/API functions at their boundaries."""
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'
OUT = ROOT / 'tools/tests/build-checks/logic-capacity'


def function(source, name):
    match = re.search(r'^static [^\n]*\b' + name + r'\([^\n]*\) \{', source, re.M)
    following = re.search(r'^static ', source[match.end():], re.M)
    return source[match.start():match.end() + following.start()]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    logic = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
    config = (SRC / 'WebConfig.inc.h').read_text(encoding='utf-8')
    pieces = [logic[logic.index('static constexpr uint8_t LOGIC_DEF_MAX'):logic.index('static LogicCompiledDefinition logic_rt_defs')]]
    pieces += [function(config, name) for name in ('json_value_pos', 'json_get_string_field', 'json_get_bool_field', 'json_get_u32_field')]
    pieces += [function(logic, name) for name in ('default_logic_json', 'logic_json_name', 'logic_str_copy', 'logic_find_matching', 'logic_array_bounds', 'logic_next_object', 'logic_node_type_from_string', 'logic_find_node', 'logic_rt_alloc', 'logic_def_release', 'logic_index_links', 'logic_compile_json')]
    (OUT / 'generated_compiler.inc.h').write_text('\n\n'.join(pieces), encoding='utf-8')
    (OUT / 'generated_save.inc.h').write_text(function(logic, 'save_logic_slot_json'), encoding='utf-8')
    (OUT / 'generated_route.inc.h').write_text(function(logic, 'web_handle_logic_json'), encoding='utf-8')
    vc = Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207')
    sdk = Path('C:/Program Files (x86)/Windows Kits/10')
    ver = '10.0.26100.0'
    env = dict(os.environ)
    env['INCLUDE'] = ';'.join(str(p) for p in (vc / 'include', sdk / f'Include/{ver}/ucrt', sdk / f'Include/{ver}/shared', sdk / f'Include/{ver}/um'))
    env['LIB'] = ';'.join(str(p) for p in (vc / 'lib/x64', sdk / f'Lib/{ver}/ucrt/x64', sdk / f'Lib/{ver}/um/x64'))
    exe = OUT / 'logic_capacity.exe'
    subprocess.run([str(vc / 'bin/Hostx64/x64/cl.exe'), '/nologo', '/EHsc', '/std:c++17', '/utf-8', '/W4', '/O2', '/I'+str(OUT),
                    str(ROOT / 'tools/tests/logic_capacity_native/test_capacity.cpp'), '/Fo'+str(OUT)+'/', '/Fe'+str(exe)], env=env, check=True)
    subprocess.run([str(exe)], check=True)
    tools = Path(os.environ['LOCALAPPDATA']) / 'Arduino15/packages/esp32/tools'
    for chip, compiler in (('S3', tools / 'esp-x32/2601/bin/xtensa-esp32s3-elf-g++.exe'), ('P4', tools / 'esp-rv32/2601/bin/riscv32-esp-elf-g++.exe')):
        subprocess.run([str(compiler), '-std=c++17', '-I'+str(OUT), '-c', str(ROOT / 'tools/tests/logic_capacity_native/test_capacity.cpp'), '-o', str(OUT / (chip+'.o'))], check=True)
        print('PASS: actual compiler/save/API fixture compiled with '+chip+' GCC (not a full firmware build)')


if __name__ == '__main__':
    main()
