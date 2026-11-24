"""Exercise the actual persistent Fan/IO hardware configuration with fake NVS."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    out = ROOT / "tools/tests/build-checks/fanio-led-native"
    out.mkdir(parents=True, exist_ok=True)
    vc = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207")
    sdk = Path("C:/Program Files (x86)/Windows Kits/10")
    ver = "10.0.26100.0"
    env = dict(os.environ)
    env["INCLUDE"] = ";".join(str(p) for p in (vc/"include", sdk/f"Include/{ver}/ucrt", sdk/f"Include/{ver}/shared", sdk/f"Include/{ver}/um"))
    env["LIB"] = ";".join(str(p) for p in (vc/"lib/x64", sdk/f"Lib/{ver}/ucrt/x64", sdk/f"Lib/{ver}/um/x64"))
    for module in ("FanIoModule", "FanIoProModule"):
        exe = out / (module+".exe")
        subprocess.run([str(vc/"bin/Hostx64/x64/cl.exe"), "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/O2",
                        "/I"+str(ROOT/"tools/tests/settings_store_native/stubs"), "/I"+str(ROOT/"Module"/module/"src"),
                        str(ROOT/"tools/tests/fanio_led_native/test_config.cpp"), "/Fo"+str(out)+"/", "/Fe"+str(exe)], env=env, check=True)
        subprocess.run([str(exe)], check=True)
    scheduler = (ROOT / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterScheduler.cpp").read_text(encoding="utf-8")
    bank = scheduler[scheduler.index("static bool universal_merge_entity_response("):scheduler.index("static int16_t universal_entity_numeric_value(")]
    (out / "generated_bank.inc.h").write_text(bank, encoding="utf-8")
    exe = out / "bank.exe"
    subprocess.run([str(vc/"bin/Hostx64/x64/cl.exe"), "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/O2", "/I"+str(out),
                    str(ROOT/"tools/tests/fanio_led_native/test_bank.cpp"), "/Fo"+str(out)+"/", "/Fe"+str(exe)], env=env, check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
