"""Fetch pinned P4 SDK sources into an explicitly selected, separate workspace."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import urllib.request

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("sdk_prepare", HERE / "prepare.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)
REPOS = {".": "espressif/esp32-arduino-lib-builder", "esp-idf": "espressif/esp-idf",
         "components/arduino": "espressif/arduino-esp32",
         "components/arduino_tinyusb/tinyusb": "hathach/tinyusb"}


def run(*args, cwd=None):
    print("+ " + " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)


def owned_file(path, content):
    SDK.require(not path.exists() or path.read_bytes() == content,
                f"Preserving unrelated file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def workspace(path):
    path = path.resolve()
    SDK.require(path != SDK.PROJECT and SDK.PROJECT not in path.parents,
                "Build outside the OFE project")
    SDK.require(not {p.lower() for p in path.parts} & {"arduino15", "arduino", "v6.0"},
                "Refusing an installed SDK or sketchbook")
    marker = path / ".ofe-p4-sdk-workspace"
    SDK.require(not path.exists() or marker.exists() or not any(path.iterdir()),
                "Choose a new/empty workspace or an existing OFE P4 workspace")
    owned_file(marker, b"OFE P4 Arduino 3.3.12 / IDF 5.5.5 isolated SDK build\n")
    return path


def fetch(root):
    builder = root / "builder"
    resolved = {}
    for relative, repo in REPOS.items():
        pin = SDK.SOURCE_PINS[relative]
        source = builder / relative
        if (source / ".git").exists():
            sha = SDK.git(source, "rev-parse", "HEAD")
            SDK.require(sha.startswith(pin), f"Preserving other checkout: {source}")
        else:
            SDK.require(not source.exists() or not any(source.iterdir()),
                        f"Preserving nonempty source directory: {source}")
            request = urllib.request.Request(f"https://api.github.com/repos/{repo}/commits/{pin}",
                                             headers={"User-Agent": "OFE-P4-SDK-builder"})
            with urllib.request.urlopen(request, timeout=60) as response:
                sha = json.load(response)["sha"]
            SDK.require(re.fullmatch(r"[a-f0-9]{40}", sha) and sha.startswith(pin),
                        f"Invalid resolved source revision: {repo}")
            source.mkdir(parents=True, exist_ok=True)
            run("git", "init", source)
            run("git", "-C", source, "config", "core.longpaths", "true")
            run("git", "-C", source, "config", "core.autocrlf", "false")
            run("git", "-C", source, "remote", "add", "origin", f"https://github.com/{repo}.git")
            run("git", "-C", source, "fetch", "--depth", "1", "origin", sha)
            run("git", "-C", source, "checkout", "--detach", "FETCH_HEAD")
        resolved[relative] = {"repository": repo, "commit": sha}
        run("git", "-C", source, "-c", "core.longpaths=true", "submodule", "update",
            "--init", "--recursive", "--depth", "1", "--jobs", "4")
    owned_file(root / "source-pins.json", (json.dumps(resolved, indent=2) + "\n").encode())
    print("Pinned sources fetched. Installed SDKs were not touched.", flush=True)


def constraint_manifest():
    versions = SDK.versions((HERE / "versions.txt").read_text())
    # JSON is a YAML subset: let the IDF manager parse this structured manifest.
    # 'require: no' constrains downloads without adding new linker requirements.
    return {"targets": ["esp32p4"], "dependencies": {
        name.replace("__", "/", 1): {"version": "==" + version, "require": "no"}
        for name, version in versions.items() if "__" in name}}


def pin_components(root):
    builder = root / "builder"
    SDK.require(SDK.git(builder, "rev-parse", "HEAD").startswith(SDK.SOURCE_PINS["."]),
                "Wrong builder checkout")
    component = builder / "components/ofe_sdk_version_pins"
    owned_file(component / "CMakeLists.txt", b"idf_component_register()\n")
    owned_file(component / "idf_component.yml", (json.dumps(constraint_manifest(), indent=2) + "\n").encode())
    tinyusb = builder / "components/arduino_tinyusb/tinyusb"
    for patch in sorted((builder / "patches/tinyusb").glob("*.diff")):
        applied = subprocess.run(["git", "-C", str(tinyusb), "apply", "--reverse", "--check", str(patch)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not applied:
            run("git", "-C", tinyusb, "apply", "--check", patch)
            run("git", "-C", tinyusb, "apply", patch)
    print("Managed-component versions constrained; pinned TinyUSB patches applied.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("fetch", "pin-components"))
    parser.add_argument("--workspace", type=Path, required=True)
    args = parser.parse_args()
    root = workspace(args.workspace)
    if args.action == "fetch":
        fetch(root)
    else:
        pin_components(root)


if __name__ == "__main__":
    main()
