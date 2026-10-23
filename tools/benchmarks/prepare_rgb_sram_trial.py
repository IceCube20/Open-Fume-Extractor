"""Keep the historical SRAM trial as a labelled snapshot of the promoted standard."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PRODUCTION = ROOT / "Module/DisplayModule_800x480"
TRIAL = Path(__file__).resolve().parent / "DisplayRgbSramTrial"


def generated_files():
    outputs = {}
    sources = [p for p in PRODUCTION.iterdir() if p.is_file()]
    sources += list((PRODUCTION / "src").rglob("*"))
    for source in sorted(sources):
        relative = source.relative_to(PRODUCTION)
        if not source.is_file() or "build" in relative.parts:
            continue
        if source.suffix.lower() not in (".ino", ".h", ".c", ".cpp", ".csv"):
            continue
        name = relative.as_posix()
        if name == "DisplayModule_800x480.ino":
            name = "DisplayRgbSramTrial.ino"
        outputs[name] = source.read_bytes()
    if "DisplayRgbSramTrial.ino" not in outputs:
        raise ValueError("Missing production sketch")
    # Production shares this bitmap with the small display via a relative include.
    # Resolve the forwarding header without changing the actual icon bytes.
    outputs["SproutIcon.h"] = (PRODUCTION.parent / "DisplayModule_320x480/SproutIcon.h").read_bytes()
    outputs["build_opt.h"] += b"\n-DOFE_RGB_SRAM_TRIAL=1\n"
    manifest = {
        "source": PRODUCTION.relative_to(ROOT).as_posix(),
        "changes": ["sketch filename", "OFE_RGB_SRAM_TRIAL=1 label; memory profile identical to standard",
                    "resolve shared SproutIcon.h forwarding include to unchanged source"],
        "files_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in outputs.items()},
    }
    outputs["sram-trial-manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for name, data in generated_files().items():
        target = TRIAL / name
        if args.check:
            if not target.exists() or target.read_bytes() != data:
                raise SystemExit("Trial snapshot is stale: " + name)
        elif not target.exists() or target.read_bytes() != data:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    print("PASS: complete OFE RGB SRAM trial verified" if args.check else
          "PASS: separate complete OFE RGB SRAM trial prepared; production build flags unchanged")


if __name__ == "__main__":
    main()
