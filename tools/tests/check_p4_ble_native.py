"""Exercise the actual P4 peripheral class with native NimBLE/RTOS test adapters.

Only platform includes are replaced; class implementation and Config validation
are extracted verbatim. Hardware radio behavior is not simulated or claimed.
"""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TEST = ROOT / "tools/tests/display_hybrid_native"
OUT = ROOT / "tools/tests/build-checks/p4-ble-native"
P4 = ROOT / "tools/boards/elecrow_p4_5inch"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    ble = (P4 / "OfeP4DisplayBle.h").read_text()
    tunnel = (ROOT / "Module/DisplayModule_800x480/src/OfeDisplayTunnel.h").read_text(encoding="utf-8-sig")
    config = tunnel[tunnel.index("namespace ofe_wifi {"):tunnel.index("inline uint64_t nonce()")]+"}\n"
    master_ble = (ROOT / "OpenFumeExtractorMaster/src/OfeDisplayBle.h").read_text(encoding="utf-8-sig")
    protocol = master_ble[master_ble.index("namespace ofe_ble {"):master_ble.index("class OfeDisplayBle {")]
    protocol = protocol.replace("namespace ofe_ble {", "namespace ofe_master_ble {")
    cpp = OUT / "test.cpp"
    cpp.write_text('#include "p4_ble_stubs.h"\n'+config+protocol+'\n#define private public\n'+
                   ble[ble.index("namespace ofe_ble {"):]+
                   '\n#undef private\n#include "OfeP4FirmwareGuard.h"\n'+
                   (TEST / "test_p4_ble.cpp").read_text(), encoding="utf-8")
    vc = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207")
    sdk = Path("C:/Program Files (x86)/Windows Kits/10")
    ver = "10.0.26100.0"
    env = dict(os.environ)
    env["INCLUDE"] = ";".join(str(p) for p in (vc / "include", sdk / f"Include/{ver}/ucrt", sdk / f"Include/{ver}/shared", sdk / f"Include/{ver}/um"))
    env["LIB"] = ";".join(str(p) for p in (vc / "lib/x64", sdk / f"Lib/{ver}/ucrt/x64", sdk / f"Lib/{ver}/um/x64"))
    exe = OUT / "test.exe"
    subprocess.run([str(vc / "bin/Hostx64/x64/cl.exe"), "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/W4", "/O2",
                    "/I"+str(TEST), "/I"+str(P4), str(cpp), "/Fo"+str(OUT)+"/", "/Fe"+str(exe)], env=env, check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
