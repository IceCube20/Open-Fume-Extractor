"""Check a completed P4 Arduino link uses the pinned sketch ECC backport."""
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
    args = parser.parse_args()
    build = args.build.resolve()
    name = "DisplayModule_P4_800x480.ino"
    root = Path(__file__).resolve().parents[2]
    folder = root / "Module/DisplayModule_P4_800x480"
    manifest = json.loads((folder / "p4-preparation-manifest.json").read_text())
    version = manifest["firmware_version"]
    for relative, expected in manifest["generated_sha256"].items():
        if (relative.startswith("src/OfeP4BleEcc") or relative in ("src/OfeP4DisplayBle.h", "OfeP4Board.h", "src/OfeP4SdkProfile.h") or
                (relative.startswith("src/tinycrypt/") and relative.endswith(".h"))):
            data = (build / "sketch" / relative).read_bytes()
            if data.startswith(b"#line 1 "):
                data = data.split(b"\n", 1)[1]  # Arduino's source-location prefix.
            if hashlib.sha256(data).hexdigest()!=expected:
                raise RuntimeError("Build copy is stale; rebuild current P4 sketch: " + relative)
    elf = build / (name + ".elf")
    image = build / (name + ".bin")
    for path in (build / "sketch/src").glob("OfeP4BleEcc*.c.o"):
        if path.stat().st_mtime_ns > elf.stat().st_mtime_ns:
            raise RuntimeError("ELF predates its ECC objects; wait for the complete build")
    if image.stat().st_mtime_ns < elf.stat().st_mtime_ns:
        raise RuntimeError("BIN predates the linked ELF")
    link_map = (build / (name + ".map")).read_text(encoding="utf-8", errors="replace")
    old = re.findall(r"libbt\.a\((?:ecc|ecc_dh|ecc_dsa|esp_tinycrypt_port)\.c\.obj\)", link_map)
    if old:
        raise RuntimeError("Old SDK ECC objects were linked: " + ", ".join(sorted(set(old))))
    origins = {
        "uECC_shared_secret": "OfeP4BleEccDh.c.o",
        "uECC_make_key": "OfeP4BleEccDh.c.o",
        "EccPoint_mult": "OfeP4BleEccCore.c.o",
        "uECC_compute_public_key": "OfeP4BleEccCore.c.o",
        "uECC_valid_public_key": "OfeP4BleEccCore.c.o",
        "ofe_p4_ble_ecc_self_test": "OfeP4BleEccSelfTest.c.o",
    }
    for symbol, expected in origins.items():
        sections = re.findall(r"(?m)^\s*\.text\." + symbol + r"(?:\.[^\s]+)?\s*\n?\s*0x[0-9a-f]+\s+0x[0-9a-f]+\s+([^\r\n]+)", link_map)
        if not any(expected in line and "sketch" in line for line in sections):
            raise RuntimeError("Cannot prove sketch origin of " + symbol + ": " + repr(sections))
    nm = Path(os.environ["LOCALAPPDATA"]) / "Arduino15/packages/esp32/tools/esp-rv32/2601/bin/riscv32-esp-elf-nm.exe"
    symbols = subprocess.run([str(nm), "-g", "--defined-only", str(elf)], capture_output=True, text=True, check=True).stdout
    # Check the BLE ECC objects, not unrelated SDK crypto accelerator users.
    for component in ("Core", "Dh", "Dsa", "SelfTest"):
        obj = build / f"sketch/src/OfeP4BleEcc{component}.c.o"
        if obj.exists():
            undefined = subprocess.run([str(nm), "-u", str(obj)], capture_output=True, text=True, check=True).stdout
        else:
            archive = build / "sketch/src/objs.a"
            members = subprocess.run([str(nm.with_name("riscv32-esp-elf-ar.exe")), "t", str(archive)],
                                     capture_output=True, text=True, check=True).stdout.splitlines()
            if obj.name not in members:
                raise RuntimeError("Missing compiled ECC object: " + obj.name)
            output = subprocess.run([str(nm), "-A", "-u", str(archive)], capture_output=True, text=True, check=True).stdout
            undefined = "\n".join(line for line in output.splitlines() if obj.name + ":" in line)
        if re.search(r"esp_tinycrypt_|ecc_hal_|ecc_ll_|esp_crypto_ecc_", undefined):
            raise RuntimeError("BLE ECC object still calls hardware: " + component)
        if component == "SelfTest":
            for symbol in ("esp_fill_random", "uECC_set_rng"):
                if not re.search(r"\bU\s+" + symbol + r"$", undefined, re.M):
                    raise RuntimeError("P4 self-test is missing its RNG handoff: " + symbol)
    for symbol in origins:
        if len(re.findall(r"(?m)^\S+ T " + symbol + r"$", symbols)) != 1:
            raise RuntimeError("Expected exactly one strong definition of " + symbol)
    data = image.read_bytes()
    if (data[0]!=0xE9 or int.from_bytes(data[12:14], "little")!=0x12 or
            ("target=DISPLAY_P4_800X480;version=" + version + ";").encode() not in data or
            b"P4 BLE ECC: software-only + P4 hardware RNG + self-test v3" not in data):
        raise RuntimeError("Wrong P4 firmware/version/backport marker")
    report = {"firmware": version, "sha256": hashlib.sha256(data).hexdigest(), "origins": origins,
              "old_sdk_ecc_objects_linked": False, "ble_ecc_uses_accelerator": False,
              "ble_rng": "P4 esp_fill_random", "rgb_profile": {"pclk_hz": 16000000, "bounce_lines": 20},
              "hardware_pairing_verified": False}
    (build / "ecc-link-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: complete P4 ELF/BIN, sketch ECC origins proven, old SDK ECC excluded, BLE ECC has no accelerator calls")


if __name__ == "__main__":
    main()
