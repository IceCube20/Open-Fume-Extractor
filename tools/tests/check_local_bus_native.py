"""Compile production Universal polling plus the shared diagnostic state."""
import os
from pathlib import Path
import subprocess
from display_ui_invariants import function

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'tools/tests/build-checks/local-bus-native'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'Module/UniversalRs232Module/UniversalRs232Module.ino').read_text(encoding='utf-8')
    (OUT / 'universal_poll.inc.h').write_text('\n'.join(function(source, name) for name in
        ('local_response_timeout_ms','profile_send_poll','profile_poll_tick')),encoding='utf-8')
    vc = Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207')
    sdk = Path('C:/Program Files (x86)/Windows Kits/10')
    ver = '10.0.26100.0'
    env = dict(os.environ)
    env['INCLUDE'] = ';'.join(str(p) for p in (vc/'include',sdk/f'Include/{ver}/ucrt',sdk/f'Include/{ver}/shared',sdk/f'Include/{ver}/um'))
    env['LIB'] = ';'.join(str(p) for p in (vc/'lib/x64',sdk/f'Lib/{ver}/ucrt/x64',sdk/f'Lib/{ver}/um/x64'))
    exe = OUT / 'test.exe'
    subprocess.run([str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/EHsc','/std:c++17','/utf-8','/W4',
        '/I'+str(OUT),'/I'+str(ROOT/'Module/UniversalRs232Module/src'),
        str(ROOT/'tools/tests/display_hybrid_native/test_local_bus_diagnostics.cpp'),
        '/Fo'+str(OUT)+'/', '/Fe'+str(exe)],env=env,check=True)
    subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    main()
