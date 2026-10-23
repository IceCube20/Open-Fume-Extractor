"""Compile the P4 preparation using installed P4 board/SDK flags in private output.

Cached S3 Arduino prototypes supply declarations only. No S3 object or SDK flags
are reused. This is not a full firmware link/upload or hardware validation.
"""
import argparse
import json
import os
from pathlib import Path
import re
import runpy
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / "Module/DisplayModule_P4_800x480"
BUILD = ROOT / "tools/tests/build-checks/p4-preparation"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chip-variant", choices=("prev3", "postv3"), default="prev3")
    args = parser.parse_args()
    helpers = runpy.run_path(str(ROOT / "tools/tests/check_display_board_syntax.py"))
    fqbn = ("esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,"
            "ChipVariant=" + args.chip_variant)
    props = helpers["board_properties"](FOLDER, fqbn)
    output = BUILD / args.chip_variant
    output.mkdir(parents=True, exist_ok=True)
    (output / "build_opt.h").write_bytes((FOLDER / "build_opt.h").read_bytes())
    (output / "file_opts").write_text("", encoding="utf-8")
    cache = helpers["CACHE"] / helpers["SPECS"][1][1]
    generated = (cache / "sketch/DisplayModule_800x480.ino.cpp").read_text(encoding="utf-8-sig")
    directives = list(re.finditer(r'^#line \d+ "[^"\r\n]*"\r?$', generated, re.M))
    if len(directives) < 3:
        raise RuntimeError("Missing Arduino prototypes; no IDE caches modified")
    prototypes = generated[directives[1].start():directives[-1].start()]
    source = (FOLDER / (FOLDER.name + ".ino")).read_text(encoding="utf-8-sig")
    marker = "static void ofe_keep_module_fw_signature() __attribute__((noinline));"
    if source.count(marker) != 1:
        raise RuntimeError("Unknown prototype insertion boundary")
    cpp = output / "display.cpp"
    cpp.write_text(source.replace(marker, marker + "\n" + prototypes), encoding="utf-8")
    platform = Path(props["runtime.platform.path"])
    includes = [FOLDER, FOLDER / "src", Path(props["build.core.path"]), Path(props["build.variant.path"])]
    includes += [platform / "libraries" / lib / "src" for lib in
                 ("Preferences", "Update", "Wire", "WiFi", "Network", "ESPmDNS", "SPI")]
    libraries = Path(os.environ["USERPROFILE"]) / "Documents/Arduino/libraries"
    includes += [libraries / lib / "src" for lib in ("GFX_Library_for_Arduino", "lvgl")]
    includes += [libraries / "Adafruit_NeoPixel"]

    def compile_one(src, c_mode=False):
        recipe = props["recipe.c.o.pattern" if c_mode else "recipe.cpp.o.pattern"]
        recipe = recipe.replace(props["build.path"], str(output))
        recipe = recipe.replace("{includes}", " ".join('"-I' + str(p) + '"' for p in includes))
        recipe = recipe.replace("{source_file}", str(src)).replace("{object_file}", str(output / (src.name + ".o")))
        if re.search(r"\{[^}]+\}", recipe):
            raise RuntimeError("Unexpanded compiler property: " + recipe)
        command = [p[1:-1] if p.startswith('"') and p.endswith('"') else p for p in shlex.split(recipe, posix=False)]
        if not Path(command[0]).exists() and Path(command[0] + ".exe").exists():
            command[0] += ".exe"
        if c_mode:
            command.insert(1, "-fstack-usage")
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=180)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return command

    command = compile_one(cpp)
    (output / "compile_commands.json").write_text(json.dumps([{
        "arguments": command, "file": str(cpp), "directory": str(ROOT), "fqbn": fqbn
    }], indent=2), encoding="utf-8")
    print("PASS: complete P4 sketch, real P4 Arduino/SDK headers, " + args.chip_variant, flush=True)
    for src in sorted((FOLDER / "src").glob("*.cpp")):
        compile_one(src)
    for src in sorted((FOLDER / "src").glob("*.c")):
        compile_one(src, True)
    for src in sorted(FOLDER.glob("*.c")):
        compile_one(src, True)
    # Check the installed real LVGL allocator with the P4 project's config too.
    compile_one(libraries / "lvgl/src/stdlib/builtin/lv_mem_core_builtin.c", True)
    print("PASS: P4 module implementation files, original fonts and real LVGL allocator compiled", flush=True)
    # Relocatable integration link pulls the real SDK security primitive caller.
    # This does not replace the separate complete firmware/map verification.
    driver = Path(command[0]).with_name("riscv32-esp-elf-gcc.exe")
    sdk = Path(props["compiler.sdk.path"])
    ecc_objects = sorted(output.glob("OfeP4BleEcc*.c.o"))
    if len(ecc_objects) != 5:
        raise RuntimeError("Missing P4 ECC backport translation units")
    link_map = output / "ecc-sdk-link.map"
    subprocess.run([str(driver), "-mabi=ilp32f", "-march=rv32imafc_zicsr_zifencei", "-nostdlib", "-r",
                    "-Wl,-u,ble_sm_alg_gen_dhkey", "-Wl,-Map="+str(link_map),
                    *(str(p) for p in ecc_objects), str(sdk / "lib/libbt.a"), "-o", str(output / "ecc-sdk-link.o")], check=True)
    text = link_map.read_text(encoding="utf-8", errors="replace")
    if re.search(r"libbt\.a\((?:ecc|ecc_dh|ecc_dsa|esp_tinycrypt_port)\.c\.obj\)", text):
        raise RuntimeError("Old SDK ECC implementation was linked into the backport")
    if "ble_sm_alg_gen_dhkey" not in text:
        raise RuntimeError("SDK security primitive was not exercised")
    print("PASS: real SDK security primitive + P4 ECC integration link, old SDK ECC excluded, " + args.chip_variant, flush=True)


if __name__ == "__main__":
    main()
