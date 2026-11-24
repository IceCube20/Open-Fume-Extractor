"""Run all four production display fault formatters with native String adapters."""
import os
from pathlib import Path
import subprocess

from display_ui_invariants import function

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'tools/tests/build-checks/display-faults-native'
NAMES = ('DisplayModule_320x480', 'DisplayModule_800x480',
         'DisplayModule_ST7796_320x480', 'DisplayModule_P4_800x480')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    fixture = r'''
#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>
#include "Rs485PeripheralBus.h"
using namespace jbc_rs485;
static constexpr int HEX=16;
class String : public std::string {
public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  String(uint16_t n,int radix) {
    assert(radix==HEX);std::ostringstream out;out<<std::hex<<n;assign(out.str());
  }
};
static bool german=false;
static const char* tr(const char* en,const char* de) {return german?de:en;}
'''
    for i, name in enumerate(NAMES):
        source = (ROOT / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8-sig')
        fixture += '\nnamespace display' + str(i) + ' {\n' + function(source, 'fault_name') + '\n}\n'
    fixture += '\nint main() {\n'
    for i in range(len(NAMES)):
        fixture += f'  {{using display{i}::fault_name;\n' + r'''
    for(bool de:{false,true}) {
      german=de;
      const char* local=tr("Local connection disconnected","Lokale Verbindung getrennt");
      const char* rpm=tr("No speed feedback","Drehzahlr\303\274ckmeldung fehlt");
      assert(fault_name(0,MODULE_MODBUS_RTU)=="OK");
      for(uint8_t type:{MODULE_MODBUS_RTU,MODULE_UNIVERSAL_RS232}) {
        assert(fault_name(1,type)==local);
        assert(fault_name(0x100,type)==rpm);
        assert(fault_name(0x101,type)==std::string(local)+", "+rpm);
        assert(fault_name(5,type)==std::string(local)+", "+tr("Filter full","Filter voll"));
        assert(fault_name(0x800,type)==tr("Repeated response timeouts","Wiederholte Antwort-Timeouts"));
        assert(fault_name(0x1000,type)==tr("Repeated checksum errors","Gehaeufte Pruefsummenfehler"));
        assert(fault_name(0x2000,type)==tr("Repeated Modbus exceptions","Wiederholte Modbus-Exceptions"));
      }
      assert(fault_name(1,MODULE_WELLER_ZERO_SMOG)==tr("Weller device bus error","Weller Ger\303\244tebus Fehler"));
      assert(fault_name(1,MODULE_FAN_IO)==rpm);
      assert(fault_name(0x100,MODULE_FAN_IO_PRO)==rpm);
      assert(fault_name(0x200,MODULE_FAN_IO)=="Timeout");
      assert(fault_name(0x8000,MODULE_UNKNOWN)=="0x8000");
    }
  }
'''
    fixture += '  std::puts("PASS: all four production display decoders, DE/EN, local connection/RPM/Weller separation, combined and unknown faults");\n}\n'
    cpp = OUT / 'faults.cpp'
    cpp.write_text(fixture, encoding='utf-8')
    vc = Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207')
    sdk = Path('C:/Program Files (x86)/Windows Kits/10')
    ver = '10.0.26100.0'
    env = dict(os.environ)
    env['INCLUDE'] = ';'.join(str(p) for p in (vc/'include',sdk/f'Include/{ver}/ucrt',sdk/f'Include/{ver}/shared',sdk/f'Include/{ver}/um'))
    env['LIB'] = ';'.join(str(p) for p in (vc/'lib/x64',sdk/f'Lib/{ver}/ucrt/x64',sdk/f'Lib/{ver}/um/x64'))
    exe = OUT / 'faults.exe'
    subprocess.run([str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/EHsc','/std:c++17','/utf-8','/W4',
                    '/I'+str(ROOT/'tools/tests/display_hybrid_native/stubs'),
                    '/I'+str(ROOT/'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/bus'),
                    str(cpp),'/Fo'+str(OUT)+'/', '/Fe'+str(exe)],env=env,check=True)
    subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    main()
