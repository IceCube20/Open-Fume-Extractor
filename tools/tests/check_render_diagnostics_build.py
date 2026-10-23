"""Compile actual LVGL hook sites and check their C linkage against our profiler."""
import json
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
display = root / "Module/DisplayModule_800x480"
cache = root / "tools/tests/build-checks/display-board-syntax/DisplayModule_800x480"
entry = json.loads((cache / "compile_commands.json").read_text(encoding="utf-8"))[0]
build = root / "tools/tests/build-checks/render-diagnostics-target"
build.mkdir(parents=True, exist_ok=True)
lvgl = Path.home() / "Documents/Arduino/libraries/lvgl/src"
tool_folder = Path(entry["arguments"][0]).parent


def run(args):
    return subprocess.run([str(a) for a in args], cwd=root, check=True,
                          capture_output=True, text=True, timeout=120).stdout


def compile_file(source, out, enabled, cpp=False):
    options = (display / "build_opt.h").read_text(encoding="utf-8")
    options = re.sub(r"(?m)^-DOFE_RGB_RENDER_DIAGNOSTICS=\d+\s*$", "", options)
    opts = build / ("options-" + str(enabled))
    opts.write_text(options + f"\n-DOFE_RGB_RENDER_DIAGNOSTICS={enabled}\n", encoding="utf-8")
    args = [tool_folder / ("xtensa-esp32s3-elf-g++.exe" if cpp else "xtensa-esp32s3-elf-gcc.exe")]
    skip = False
    for arg in entry["arguments"][1:]:
        if skip:
            skip = False
        elif arg == "-o":
            skip = True
        elif arg == entry["file"] or arg == "-MMD":
            continue
        elif arg.startswith("@") and arg.replace("\\", "/").endswith("/build_opt.h"):
            args.append("@" + str(opts))
        else:
            args.append(arg if cpp else arg.replace("/flags/cpp_flags", "/flags/c_flags").replace("\\flags\\cpp_flags", "\\flags\\c_flags"))
    args.extend([source, "-o", out])
    run(args)
    return out


def undefined(obj):
    return run([tool_folder / "xtensa-esp32s3-elf-nm.exe", "-u", obj])


sites = ["core/lv_refr.c", "draw/sw/lv_draw_sw.c", "draw/sw/lv_draw_sw_letter.c",
         "draw/sw/blend/lv_draw_sw_blend.c", "indev/lv_indev.c"]
objects = []
for site in sites:
    obj = compile_file(lvgl / site, build / (Path(site).stem + ".o"), 1)
    symbols = undefined(obj)
    for name in ("ofe_lv_profile_enabled", "ofe_lv_profile_begin", "ofe_lv_profile_end"):
        assert name in symbols, f"Missing active hook in {site}: {name}"
    assert not re.search(r"\bstrcmp\b", symbols), f"Tag filter did not fold in {site}"
    objects.append(obj)
    print("PASS: real LVGL C hook site " + site, flush=True)
objects.append(compile_file(display / "src/OfeLvProfiler.cpp", build / "ofe-profiler.o", 1, cpp=True))
combined = build / "hook-link.o"
run([tool_folder / "xtensa-esp32s3-elf-ld.exe", "-r", *objects, "-o", combined])
assert "ofe_lv_profile_" not in undefined(combined), "Unresolved profiler C linkage"
print("PASS: actual LVGL/profiler relocatable link, no unresolved profiling symbols", flush=True)

normal = compile_file(lvgl / "core/lv_refr.c", build / "normal-refr.o", 0)
assert "ofe_lv_profile_" not in undefined(normal), "Production still has deep hooks"
print("PASS: normal production config removes deep hooks", flush=True)
probe = compile_file(root / "tools/tests/lvgl_profile_codegen.c", build / "filter-probe.o", 1)
asm = run([tool_folder / "xtensa-esp32s3-elf-objdump.exe", "-dr", probe])
(build / "filter-probe.asm.txt").write_text(asm, encoding="utf-8")
unknown = asm.split("<unselected_hot_function>:", 1)[1].split("Disassembly of section", 1)[0]
assert not re.search(r"\bcall(?:0|4|8|12|x\d+)\b|ofe_lv_profile|strcmp", unknown), "Unselected hook has runtime work"
assert not re.search(r"\bstrcmp\b", undefined(probe)), "Literal hook names are compared at runtime"
print("PASS: unselected tag compiles without runtime calls; selected tags have no strcmp", flush=True)
