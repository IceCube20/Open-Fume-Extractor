"""Audit the LED-only correction of the owner's V3 P4 sketch, without uploading."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def source_bytes(path):
    text = path.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
    return re.sub(r"(?m)^#line [^\n]*\n", "", text).encode()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path, help="Unmodified Arduino build matching the crash ELF")
    parser.add_argument("fixed", type=Path, help="Completed independent corrected build")
    args = parser.parse_args()
    original, fixed = args.original.resolve(), args.fixed.resolve()
    name = "DisplayModule_P4_800x480.ino"
    old_hash = sha(original / (name + ".elf"))
    crash_hash = "fa9206b9dc03c0e116b26eb39ea63f894e0b056531490b11974c56a06d9cb812"
    def sources(build):
        return {p.relative_to(build / "sketch") for p in (build / "sketch").rglob("*")
                if p.is_file() and p.suffix in (".h", ".hpp", ".c", ".cpp")}
    if sources(original) != sources(fixed):
        raise RuntimeError("Sketch source files were added or removed")
    differences = []
    for path in (fixed / "sketch").rglob("*"):
        if not path.is_file() or path.suffix not in (".h", ".hpp", ".c", ".cpp"):
            continue
        relative = path.relative_to(fixed / "sketch")
        if source_bytes(path) != source_bytes(original / "sketch" / relative):
            differences.append(relative.as_posix())
    if any(name != "src/OfeStatusLed.h" for name in differences):
        raise RuntimeError("Not an LED-only correction: " + repr(differences))
    led_path = Path("sketch/src/OfeStatusLed.h")
    fixed_led = source_bytes(fixed / led_path)
    guard = b"#if OFE_STATUS_LED_ENABLE && OFE_STATUS_LED_PIN >= 0 && OFE_STATUS_LED_COUNT > 0"
    old_led = fixed_led.replace(guard, b"#if OFE_STATUS_LED_ENABLE")
    old_led_variants = (old_led, old_led.replace(b"\n", b"\r\n"))
    if fixed_led.count(guard) != 5 or not any(hashlib.sha256(data).hexdigest() ==
            "1eec96af860cfe00732c0ccf4ea666b349542aaecfaee613c3ef28ca79136fd0" for data in old_led_variants):
        raise RuntimeError("Unexpected LED changes beyond the five hardware guards")
    old_options = json.loads((original / "build.options.json").read_text())
    options = json.loads((fixed / "build.options.json").read_text())
    for field in ("fqbn", "compiler.optimization_flags", "customBuildProperties", "sketchLocation"):
        if options[field] != old_options[field]:
            raise RuntimeError("Changed build profile: " + field)
    # IDE saves may normalize CRLF. The earlier no-index diff recorded this blob
    # before the LED edit; do not infer original code from a rebuilt cache ELF.
    main_source = source_bytes(Path(options["sketchLocation"]) / name)
    main_git_hash = hashlib.sha1(b"blob " + str(len(main_source)).encode() + b"\0" + main_source).hexdigest()
    led_disabled_in_sketch = False
    if not main_git_hash.startswith("fd0bd20") and main_source.count(b"#define OFE_STATUS_LED_ENABLE 0\n") == 1:
        restored = main_source.replace(b"#define OFE_STATUS_LED_ENABLE 0\n",
                                       b"#define OFE_STATUS_LED_ENABLE 1\n", 1)
        restored_hash = hashlib.sha1(b"blob " + str(len(restored)).encode() + b"\0" + restored).hexdigest()
        led_disabled_in_sketch = restored_hash.startswith("fd0bd20")
    if not main_git_hash.startswith("fd0bd20") and not led_disabled_in_sketch:
        raise RuntimeError("The supplied V3 sketch changed since its original review")
    for suffix in (".bootloader.bin", ".partitions.bin"):
        if sha(fixed / (name + suffix)) != sha(original / (name + suffix)):
            raise RuntimeError("Unexpected bootloader/partition change: " + suffix)
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((root / "Module/DisplayModule_P4_800x480/p4-preparation-manifest.json").read_text())
    for relative, expected in manifest["generated_sha256"].items():
        if (relative.startswith("src/OfeP4BleEcc") or relative == "src/OfeP4DisplayBle.h" or
                (relative.startswith("src/tinycrypt/") and relative.endswith(".h"))):
            path = fixed / "sketch" / relative
            data = path.read_bytes()
            if data.startswith(b"#line 1 "):
                data = data.split(b"\n", 1)[1]
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError("BLE/RNG source changed: " + relative)
    elf = fixed / (name + ".elf")
    binary = fixed / (name + ".bin")
    if elf.stat().st_mtime_ns < (fixed / "sketch" / (name + ".cpp.o")).stat().st_mtime_ns:
        raise RuntimeError("ELF predates its corrected sketch object")
    if binary.stat().st_mtime_ns < elf.stat().st_mtime_ns or sha(elf) == crash_hash:
        raise RuntimeError("Incomplete or unchanged firmware build")
    nm = Path(os.environ["LOCALAPPDATA"]) / "Arduino15/packages/esp32/tools/esp-rv32/2601/bin/riscv32-esp-elf-nm.exe"
    symbols = subprocess.run([str(nm), "-C", "--defined-only", str(elf)],
                             capture_output=True, text=True, check=True).stdout
    if re.search(r"\bespShow\b|Adafruit_NeoPixel::", symbols):
        raise RuntimeError("NeoPixel remains linked despite the unconfigured LED GPIO")
    for symbol in ("v3_gui_task(void*)", "ppa_do_fill", "ofe_p4_ble_ecc_self_test"):
        if symbol not in symbols:
            raise RuntimeError("Existing GUI/PPA/RNG path missing: " + symbol)
    data = binary.read_bytes()
    if data[0] != 0xE9 or int.from_bytes(data[12:14], "little") != 0x12:
        raise RuntimeError("Wrong firmware chip")
    report = {"original_crash_elf_sha256": crash_hash, "reference_cache_elf_sha256_now": old_hash,
              "reference_cache_rebuilt": old_hash != crash_hash, "fixed_elf_sha256": sha(elf),
              "fixed_bin_sha256": sha(binary), "bin_bytes": len(data),
              "changed_source_files_from_reviewed_state": ["src/OfeStatusLed.h"] +
                  ([name] if led_disabled_in_sketch else []),
              "led_disabled_in_sketch": led_disabled_in_sketch,
              "current_cache_source_differences": differences, "main_source_git_blob": main_git_hash,
              "fqbn": options["fqbn"],
              "neopixel_linked": False, "gui_task_retained": True,
              "ppa_fill_retained": True, "ble_rng_sources_unchanged": True,
              "bootloader_and_partitions_unchanged": True,
              "hardware_stability_verified": False, "post_fix_fps_verified": False}
    (fixed / "v3-led-fix-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: LED-only correction, NeoPixel excluded; V3 GUI/PPA/RNG and flash layout retained")


if __name__ == "__main__":
    main()
