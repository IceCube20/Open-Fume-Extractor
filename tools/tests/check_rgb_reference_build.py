"""Compile the reference and real LVGL into a private relocatable target image.

This does not run Arduino's slow library discovery or touch its caches. It is
not a final firmware link/upload and does not test LCD hardware or radio load.
"""
import argparse
import json
from pathlib import Path
import subprocess
import re

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / "tools/benchmarks/DisplayRgbReference"
PROD = ROOT / "Module/DisplayModule_800x480"
METADATA = ROOT / "tools/tests/build-checks/display-board-syntax/DisplayModule_800x480/compile_commands.json"
BUILD = ROOT / "tools/tests/build-checks/rgb-reference"
LVGL = Path.home() / "Documents/Arduino/libraries/lvgl"


def run(args):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, capture_output=True,
                            text=True, encoding="utf-8", errors="replace", timeout=180)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demos-only", action="store_true", help="Skip full LVGL core/relocatable link")
    args = parser.parse_args()
    run([Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe",
         ROOT / "tools/benchmarks/prepare_rgb_reference.py", "--check"])
    BUILD.mkdir(parents=True, exist_ok=True)
    entry = json.loads(METADATA.read_text(encoding="utf-8"))[0]
    compiler = Path(entry["arguments"][0])
    (BUILD / "empty_options").write_text("", encoding="utf-8")
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
            base.append("@" + str(REF / "build_opt.h"))
        elif arg.startswith("@") and arg.replace("\\", "/").endswith("/file_opts"):
            base.append("@" + str(BUILD / "empty_options"))
        else:
            base.append(arg.replace(str(PROD), str(REF)))

    def compile_one(source, cpp=False):
        relative = source.relative_to(ROOT) if source.is_relative_to(ROOT) else source.relative_to(LVGL)
        obj = BUILD / ("__".join(relative.parts) + ".o")
        flags = base if cpp else [a.replace("/flags/cpp_flags", "/flags/c_flags").replace(
            "\\flags\\cpp_flags", "\\flags\\c_flags") for a in base]
        tool = compiler if cpp else compiler.with_name("xtensa-esp32s3-elf-gcc.exe")
        run([tool, *flags, "-x", "c++" if cpp else "c", source, "-o", obj])
        return obj

    objects = [compile_one(REF / "DisplayRgbReference.ino", cpp=True)]
    symbols = run([compiler.with_name("xtensa-esp32s3-elf-objdump.exe"), "-t", objects[0]])
    if re.search(r"\.rtc_noinit\S*[^\r\n]*selection", symbols):
        raise RuntimeError("Reference selection must not depend on RTC retention")
    print("PASS: reference sketch with actual OFE board/SDK flags", flush=True)
    for source in sorted((REF / "src").glob("*.c")):
        objects.append(compile_one(source))
    print("PASS: unmodified installed LVGL benchmark, widgets and asset wrappers", flush=True)
    if args.demos_only:
        return
    sources = sorted((LVGL / "src").rglob("*.c"))
    for i, source in enumerate(sources, 1):
        objects.append(compile_one(source))
        if i % 40 == 0 or i == len(sources):
            print(f"LVGL reference core: {i}/{len(sources)} target C units compiled", flush=True)
    response = BUILD / "link_objects.rsp"
    response.write_text("\n".join('"' + str(p).replace("\\", "/") + '"' for p in objects), encoding="utf-8")
    combined = BUILD / "rgb-reference-combined.o"
    run([compiler.with_name("xtensa-esp32s3-elf-ld.exe"), "-r", "@" + str(response), "-o", combined])
    undefined = run([compiler.with_name("xtensa-esp32s3-elf-nm.exe"), "-u", combined])
    missing = re.findall(r"\bU\s+((?:lv_|_lv_|img_benchmark|img_demo_widgets|img_lvgl_logo|img_clothes)\w*)", undefined)
    if missing:
        raise RuntimeError("Unresolved LVGL/reference symbols: " + ", ".join(missing))
    print("PASS: reference + full LVGL relocatable target link; no unresolved LVGL/demo/asset symbols", flush=True)


if __name__ == "__main__":
    main()
