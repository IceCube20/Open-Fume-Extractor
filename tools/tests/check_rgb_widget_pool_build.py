"""Target-compile both OFE memory profiles and check the real LVGL allocator hook.

Uses installed board flags and cached Arduino prototypes, not cached LVGL
objects. The relocatable link is not a final firmware link or hardware test.
"""
import json
from pathlib import Path
import re
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PROD = ROOT / "Module/DisplayModule_800x480"
TRIAL = ROOT / "tools/benchmarks/DisplayRgbSramTrial"
BUILD = ROOT / "tools/tests/build-checks/rgb-widget-pool-target"
METADATA = ROOT / "tools/tests/build-checks/display-board-syntax/DisplayModule_800x480/compile_commands.json"
LVGL = Path.home() / "Documents/Arduino/libraries/lvgl"


def run(args):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, capture_output=True,
                            text=True, encoding="utf-8", errors="replace", timeout=180)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main():
    check = runpy.run_path(str(ROOT / "tools/tests/check_display_board_syntax.py"))
    source = check["current_source"](PROD, PROD.name, check["CACHE"] / check["SPECS"][1][1])
    entry = json.loads(METADATA.read_text(encoding="utf-8"))[0]
    compiler = Path(entry["arguments"][0])
    BUILD.mkdir(parents=True, exist_ok=True)
    (BUILD / "empty_options").write_text("", encoding="utf-8")
    for name, folder in (("internal-reference", PROD), ("standard", PROD)):
        output = BUILD / name
        output.mkdir(exist_ok=True)
        cpp = output / "display.cpp"
        cpp.write_text(source, encoding="utf-8")
        options = output / "build_opt.h"
        flags_bytes = (folder / "build_opt.h").read_bytes()
        if name == "internal-reference":
            flags_bytes = flags_bytes.replace(b"-DOFE_RGB_WIDGET_POOL_PSRAM=1", b"-DOFE_RGB_WIDGET_POOL_PSRAM=0")
        options.write_bytes(flags_bytes)
        base = []
        skip = False
        for arg in entry["arguments"][1:]:
            if skip:
                skip = False
            elif arg == "-o":
                skip = True
            elif arg in (entry["file"], "-MMD"):
                continue
            elif arg.startswith("@") and arg.replace("\\", "/").endswith("/build_opt.h"):
                base.append("@" + str(options))
            elif arg.startswith("@") and arg.replace("\\", "/").endswith("/file_opts"):
                base.append("@" + str(BUILD / "empty_options"))
            else:
                base.append(arg.replace(str(PROD), str(folder)))
        objects = []
        for src, cpp_mode, stem in ((cpp, True, "display"),
                                   (folder / "src/OfeRgbWidgetPool.cpp", True, "pool"),
                                   (LVGL / "src/stdlib/builtin/lv_mem_core_builtin.c", False, "lv_mem")):
            obj = output / (stem + ".o")
            flags = base if cpp_mode else [a.replace("/flags/cpp_flags", "/flags/c_flags").replace(
                "\\flags\\cpp_flags", "\\flags\\c_flags") for a in base]
            tool = compiler if cpp_mode else compiler.with_name("xtensa-esp32s3-elf-gcc.exe")
            run([tool, *flags, "-x", "c++" if cpp_mode else "c", src, "-o", obj])
            objects.append(obj)
        symbols = run([compiler.with_name("xtensa-esp32s3-elf-nm.exe"), "-S", objects[-1]])
        pools = re.findall(r"\b([0-9a-fA-F]+)\s+[bB]\s+\S*work_mem_int\S*", symbols)
        if name == "internal-reference":
            if len(pools) != 1 or int(pools[0], 16) != 32768:
                raise RuntimeError("Internal reference pool changed")
        elif pools:
            raise RuntimeError("Standard build still contains an internal LVGL base pool")
        combined = output / "combined.o"
        run([compiler.with_name("xtensa-esp32s3-elf-ld.exe"), "-r", *objects, "-o", combined])
        undefined = run([compiler.with_name("xtensa-esp32s3-elf-nm.exe"), "-u", combined])
        if "ofe_rgb_widget_pool_" in undefined:
            raise RuntimeError("Unresolved C/C++ LVGL pool hook")
        if name == "standard" and "ofe_rgb_widget_pool_alloc" not in run([
                compiler.with_name("xtensa-esp32s3-elf-nm.exe"), "-u", objects[-1]]):
            raise RuntimeError("Real LVGL allocator did not use the PSRAM hook")
        print(f"PASS: {name}: current complete sketch + pool backend + real LVGL allocator compiled/partially linked", flush=True)
    print("PASS: promoted standard removes exactly the 32768-byte internal LVGL base-pool block", flush=True)


if __name__ == "__main__":
    main()
