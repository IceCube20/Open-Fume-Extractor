"""Stage reviewed public sources, preserving replaced files in a local backup."""
import argparse
import ast
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "GitHub-Release"
HA = ROOT.parent / "Open-Fume-Extractor-Home-Assistant"
SKIP = {"build", "build-checks", "__pycache__", "node_modules", ".git", ".venv", ".arduino-high-perf", "artifacts", "refactor-backups"}
EXTENSIONS = {".ino", ".h", ".hpp", ".c", ".cpp", ".inc", ".py", ".ps1", ".sh", ".bat",
              ".js", ".cjs", ".mjs", ".html", ".css", ".json", ".csv", ".md", ".txt", ".yml", ".yaml",
              ".png", ".jpg", ".jpeg", ".svg", ".ttf", ".ofeprofile", ".toml", ".ini"}
DROP_TESTS = {"test_led_styles_match_master_web_event_mapping", "test_master_device_constants_match_actual_mqtt_firmware_metadata"}


def allowed(path):
    name = path.name.lower()
    return not path.is_symlink() and not any(part in SKIP for part in path.parts) and not (
        ("privateconfig" in name and not name.endswith(".example.h")) or "masterboardlocalconfig" in name or path.suffix.lower() in {".pem", ".key"}) and (
        path.suffix.lower() in EXTENSIONS or path.name in {"LICENSE", "NOTICE", "COPYING", "build_opt.h"})


def files(directory):
    for parent, directories, names in os.walk(directory):
        directories[:] = [name for name in directories if name not in SKIP and not name.startswith(".")]
        for name in names:
            path = Path(parent) / name
            if allowed(path):
                yield path


def portable_tests(data):
    text = data.decode("utf-8-sig")
    lines = text.splitlines(keepends=True)
    spans = []
    for node in ast.walk(ast.parse(text)):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name in DROP_TESTS:
            spans.append((min([node.lineno] + [item.lineno for item in node.decorator_list]) - 1, node.end_lineno))
    for begin, end in sorted(spans, reverse=True):
        del lines[begin:end]
    return "".join(lines).encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    for repo in (PUBLIC, HA):
        if not (repo / ".git").exists():
            raise SystemExit("Expected existing checkout: " + str(repo))
    backup = ROOT / "tools/tests/build-checks" / ("publish-backup-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    changes = []

    def target(path):
        resolved = path.resolve()
        if not any(resolved.is_relative_to(repo.resolve()) and resolved != repo.resolve() for repo in (PUBLIC, HA)):
            raise SystemExit("Unsafe public target: " + str(path))
        return resolved

    def archive(path):
        path = target(path)
        if not path.exists():
            return
        changes.append({"archive": str(path.relative_to(ROOT.parent))})
        if args.apply:
            destination = (backup / path.relative_to(ROOT.parent)).resolve()
            if not destination.is_relative_to(backup.resolve()):
                raise SystemExit("Unsafe backup path")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(path), str(destination))

    def write(path, data):
        path = target(path)
        if path.exists() and path.read_bytes() == data:
            return
        changes.append({"write": str(path.relative_to(ROOT.parent)), "sha256": hashlib.sha256(data).hexdigest()})
        if args.apply:
            if path.exists():
                destination = backup / path.relative_to(ROOT.parent)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, destination)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    for legacy in ("MasterP4", "tools/refactor-backups", "OpenFumeExtractorMaster/src", "OpenFumeExtractorMaster/OpenFumeExtractorMaster.ino",
                   "OpenFumeExtractorMaster/build_opt.h", "OpenFumeExtractorMaster/OfeBleBuildConfig.h",
                   "OpenFumeExtractorMaster/partitions.csv", "OpenFumeExtractorMaster/partitions_16mb_large_nvs.csv"):
        archive(PUBLIC / legacy)
    for name in ("Module", "OpenFumeExtractorMaster", "HomeAssistant", "tools", "docs", "profiles", "examples", "assets", "updates", ".github"):
        source = ROOT / name
        desired = set()
        for path in files(source):
            relative = path.relative_to(ROOT)
            desired.add(relative)
            write(PUBLIC / relative, path.read_bytes())
        if name in ("Module", "OpenFumeExtractorMaster"):
            for path in list(files(PUBLIC / name)):
                relative = path.relative_to(PUBLIC)
                if relative not in desired:
                    archive(path)

    component = ROOT / "HomeAssistant/custom_components/open_fume_extractor"
    version = json.loads((component / "manifest.json").read_text())["version"]
    package = ROOT / "HomeAssistant/dist" / ("open-fume-extractor-ha-" + version + ".zip")
    if not package.is_file():
        raise SystemExit("Build the HA package first: " + str(package))
    for old in (PUBLIC / "HomeAssistant/dist").glob("*.zip"):
        if old.name != package.name:
            archive(old)
    write(PUBLIC / "HomeAssistant/dist" / package.name, package.read_bytes())

    for name in ("custom_components", "docs"):
        for path in files(ROOT / "HomeAssistant" / name):
            write(HA / path.relative_to(ROOT / "HomeAssistant"), path.read_bytes())
    for name in ("README.md", "CHANGELOG.md", "hacs.json", "requirements-test.txt"):
        write(HA / name, (ROOT / "HomeAssistant" / name).read_bytes())
    for name in ("build_package.py", "test_cards.cjs"):
        write(HA / "tools" / name, (ROOT / "HomeAssistant/tools" / name).read_bytes())
    for path in (HA / "tests").glob("test_*.py"):
        canonical = ROOT / "HomeAssistant/tests" / path.name
        if canonical.is_file():
            write(path, portable_tests(canonical.read_bytes()))
    if args.apply:
        backup.mkdir(parents=True, exist_ok=True)
        (backup / "sync-manifest.json").write_text(json.dumps(changes, indent=2), encoding="utf-8")
    print(json.dumps({"applied": args.apply, "changes": len(changes), "backup": str(backup)}, indent=2))


if __name__ == "__main__":
    main()
