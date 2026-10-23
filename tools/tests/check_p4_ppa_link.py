"""Verify a completed P4 PPA build; does not claim hardware execution or FPS gains."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    build = parser.parse_args().build.resolve()
    root = Path(__file__).resolve().parents[2]
    folder = root / "Module/DisplayModule_P4_800x480"
    manifest = json.loads((folder / "p4-preparation-manifest.json").read_text())
    for name in ("ofe_lv_conf.h", "src/OfeP4PpaConfig.h", "src/OfeP4Ppa.h",
                 "src/OfeP4Ppa.cpp", "src/OfeDisplayMemory.h"):
        data = (build / "sketch" / name).read_bytes()
        if data.startswith(b"#line 1 "):
            data = data.split(b"\n", 1)[1]
        if hashlib.sha256(data).hexdigest() != manifest["generated_sha256"][name]:
            raise RuntimeError("Stale PPA build copy: " + name)
    options = json.loads((build / "build.options.json").read_text())
    renderer = re.search(r"(?:[:,])Renderer=(ppaimage|ppa)(?:,|$)", options["fqbn"])
    if not renderer:
        raise RuntimeError("Build did not select the PPA renderer")
    images = renderer[1] == "ppaimage"
    base = "DisplayModule_P4_800x480.ino"
    elf = build / (base + ".elf")
    binary = build / (base + ".bin")
    if binary.stat().st_mtime_ns < elf.stat().st_mtime_ns:
        raise RuntimeError("BIN predates the final ELF")
    image = binary.read_bytes()
    marker = b"PPA-fill+image (blocking)" if images else b"PPA-fill (blocking)"
    if marker not in image:
        raise RuntimeError("PPA mode marker missing from image")
    nm = Path(os.environ["LOCALAPPDATA"]) / "Arduino15/packages/esp32/tools/esp-rv32/2601/bin/riscv32-esp-elf-nm.exe"
    symbols = subprocess.run([str(nm), "-g", "--defined-only", str(elf)],
                             capture_output=True, text=True, check=True).stdout
    required = ("lv_draw_ppa_init", "lv_draw_ppa_fill", "__wrap_ppa_do_fill", "ppa_do_fill",
                "ofe_p4_ppa_cache_line_size")
    if images:
        required += ("lv_draw_ppa_img", "__wrap_ppa_do_blend", "ppa_do_blend")
    for symbol in required:
        if len(re.findall(r"(?m)^\S+ T " + re.escape(symbol) + r"$", symbols)) != 1:
            raise RuntimeError("Missing or ambiguous PPA symbol: " + symbol)
    link_map = (build / (base + ".map")).read_text(errors="replace").replace("\\", "/")
    if "/ofe-p4-high-performance/sdk/esp32p4_es/" not in link_map:
        raise RuntimeError("Installed custom SDK origin not proven")
    if "/esp32p4_es-libs/3.3.12/" in link_map:
        raise RuntimeError("Stock P4 SDK linked unexpectedly")
    report = {"firmware": manifest["firmware_version"], "sha256": hashlib.sha256(image).hexdigest(),
              "renderer": "LVGL PPA opaque fills" + (" + eligible images" if images else "")
                          + "; software fallback for unsupported draw tasks",
              "required_symbols": list(required), "image_acceleration_enabled": images,
              "blocking_operations": True, "draw_alignment": 128,
              "hardware_verified": False, "fps_gain_verified": False}
    (build / "ppa-link-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: PPA draw unit, hardware fill" + (" + image blend" if images else "")
          + " and diagnostic wrappers linked; hardware/FPS test still required")


if __name__ == "__main__":
    main()
