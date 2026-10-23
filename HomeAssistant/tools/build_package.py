"""Build the manual-install archive from the self-contained component."""
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "custom_components/open_fume_extractor"


def build():
    version = json.loads((COMPONENT / "manifest.json").read_text(encoding="utf-8"))["version"]
    target = ROOT / "dist" / f"open-fume-extractor-ha-{version}.zip"
    target.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(COMPONENT.rglob("*")):
            if path.is_file() and "__pycache__" not in path.parts and path.suffix in (".py", ".json", ".js", ".png"):
                archive.write(path, path.relative_to(ROOT).as_posix())
        for name in ("README.md", "LICENSE"):
            archive.write(ROOT / name, name)
    return target


if __name__ == "__main__":
    print(build())
