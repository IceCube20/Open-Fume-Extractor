"""Stage the shared master for Waveshare ESP32-P4-Module-DEV-KIT (PRE-V3).

No SDK/IDE installation, flashing or private configuration copying is performed.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3"
DEST = ROOT / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4"
ECC = Path(__file__).resolve().parent / "elecrow_p4_5inch"
MANIFEST = "p4-master-manifest.json"
FQBN = ("esp32:esp32:esp32p4:ChipVariant=prev3,PSRAM=enabled,USBMode=hwcdc,"
        "CDCOnBoot=default,FlashSize=16M,PartitionScheme=custom")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def generated_files():
    outputs = {}
    for path in sorted(SOURCE.rglob("*")):
        relative = path.relative_to(SOURCE)
        if not path.is_file() or any(part.startswith(".") or part in ("build", "data", "__pycache__") for part in relative.parts):
            continue
        if "privateconfig" in path.name.lower() or path.name in ("build_opt.h", "OfeBleBuildConfig.h", "MasterBoardLocalConfig.h"):
            continue
        if path.suffix.lower() not in (".ino", ".h", ".hpp", ".cpp", ".c", ".csv"):
            continue
        name = relative.as_posix()
        if name == "OpenFumeExtractorMCU_S3.ino":
            name = "OpenFumeExtractorMCU_P4.ino"
        outputs[name] = path.read_bytes()
    upstream = json.loads((ECC / "tinycrypt/UPSTREAM.json").read_text())
    for name, info in upstream["files"].items():
        if digest((ECC / "tinycrypt" / name).read_bytes()) != info["sha256"]:
            raise ValueError("ECC source differs from pinned upstream: " + name)
    for path in sorted(ECC.glob("OfeP4BleEcc*")):
        outputs["src/p4/" + path.name] = path.read_bytes()
    for path in sorted((ECC / "tinycrypt").iterdir()):
        if path.is_file():
            outputs["src/p4/tinycrypt/" + path.name] = path.read_bytes()
    outputs['partitions.csv']=(ROOT/'tools/boards/master_p4_16mb.csv').read_bytes()
    usb_manifest=SOURCE/'src/usb_msc/UPSTREAM.json'
    if usb_manifest.exists():
        usb=json.loads(usb_manifest.read_text())
        for name,record in usb['staged'].items():
            if not (SOURCE/name).resolve().is_relative_to(SOURCE.resolve()):raise ValueError('Unsafe USB source path')
            if digest((SOURCE/name).read_bytes())!=record['sha256']:raise ValueError('USB source differs from pinned upstream: '+name)
        for name in ('LICENSE','UPSTREAM.json'):
            outputs['src/usb_msc/'+name]=(SOURCE/'src/usb_msc'/name).read_bytes()
    outputs["build_opt.h"] = b"-O3\n-DOFE_MASTER_P4_WAVESHARE=1\n"
    ble = (SOURCE / "src/OfeDisplayBle.h").read_text(encoding="utf-8-sig")
    protocol = ble[ble.index("namespace ofe_ble {"):ble.index("class OfeDisplayBle {")]
    outputs["src/p4/OfeBleProtocol.h"] = ("#pragma once\n" + protocol).encode()
    config = outputs["src/MasterBuildConfig.h"].decode("utf-8-sig")
    version = ".".join(re.search(r"^#define MASTER_FW_" + part + r" (\d+)$", config, re.M)[1]
                       for part in ("MAJOR", "MINOR", "PATCH")) + "RC"
    manifest = {
        "firmware_version": version,
        "status": "RC; PRE-V3 owner-tested; other board revisions unverified",
        "board": "Waveshare ESP32-P4-Module-DEV-KIT",
        "sdk": "Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5",
        "fqbn": FQBN,
        "private_config_copied": False,
        "ota_slot_bytes": 5242880,
        "partition_migration": "USB flash after backup; NVS address retained, LittleFS relocated",
        "generated_sha256": {name: digest(data) for name, data in outputs.items()},
    }
    outputs[MANIFEST] = (json.dumps(manifest, indent=2) + "\n").encode()
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--force", action="store_true", help="Replace reviewed local generated-file edits")
    args = parser.parse_args()
    outputs = generated_files()
    manifest = DEST / MANIFEST
    previous = json.loads(manifest.read_text())["generated_sha256"] if manifest.exists() else {}
    def destination(name):
        path = (DEST / name).resolve()
        if not path.is_relative_to(DEST.resolve()) or path == DEST.resolve():
            raise SystemExit("Unsafe generated manifest path: " + name)
        return path
    obsolete = [name for name in previous if name not in outputs]
    for name in obsolete:
        path = destination(name)
        if path.exists() and (args.check or digest(path.read_bytes()) != previous[name]):
            raise SystemExit("Obsolete generated file requires review: " + str(path))
    for name, data in outputs.items():
        path = destination(name)
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise SystemExit("P4 master preparation is stale: " + name)
        elif path.exists() and path.read_bytes() != data and name != MANIFEST and not args.force:
            if digest(path.read_bytes()) != previous.get(name):
                raise SystemExit("Locally edited file preserved: " + str(path))
    if not args.check:
        for name in obsolete:
            path = destination(name)
            if path.exists():
                path.unlink()  # Only unedited, previously generated files inside DEST.
        for name, data in outputs.items():
            path = destination(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists() or path.read_bytes() != data:
                path.write_bytes(data)
    print("PASS: P4 master preparation verified" if args.check else "PASS: P4 master generated; private config excluded")


if __name__ == "__main__":
    main()
