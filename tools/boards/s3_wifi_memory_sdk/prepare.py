"""Prepare and audit an isolated S3 SDK memory experiment; never install or flash."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[2]
PROFILE = json.loads((HERE / "profile.json").read_text(encoding="utf-8"))
FLAG = "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP"
MARKER = b"OFE S3 Arduino 3.2.0 WiFi memory SDK experiment v1\n"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def parse_config(text, header=False):
    values = {}
    for line in text.splitlines():
        if header:
            match = re.fullmatch(r"\s*#define\s+(CONFIG_\w+)\s+(.+?)\s*", line)
            pair = match.groups() if match else None
        else:
            match = re.fullmatch(r"(CONFIG_\w+)=(.+)", line)
            disabled = re.fullmatch(r"# (CONFIG_\w+) is not set", line)
            pair = match.groups() if match else ((disabled[1], "n") if disabled else None)
        if pair:
            key, value = pair
            require(key not in values or values[key] == value, f"Conflicting setting: {key}")
            values[key] = value
    require(bool(values), "Empty configuration")
    return values


def normalized(values):
    return {key: ("1" if value == "y" else value)
            for key, value in values.items() if value != "n"}


def resolve_aliases(values):
    resolved = {}
    def resolve(key, seen):
        require(key not in seen, f"Cyclic configuration alias: {key}")
        value = values[key]
        if re.fullmatch(r"CONFIG_\w+", value):
            require(value in values, f"Missing configuration alias: {value}")
            value = resolve(value, seen | {key})
        return value
    for key in values:
        resolved[key] = resolve(key, set())
    return resolved


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def versions(text):
    result = {}
    for line in text.splitlines():
        if ":" in line:
            key, value = line.split(":", 1)
            require(key not in result, f"Duplicate version: {key}")
            result[key] = value.strip()
    return result


def check_versions(path):
    values = versions(path.read_text(encoding="utf-8"))
    names = {".": "lib-builder", "esp-idf": "esp-idf", "components/arduino": "arduino",
             "components/arduino_tinyusb/tinyusb": "tinyusb"}
    for relative, name in names.items():
        tokens = values.get(name, "").split()
        pin = PROFILE["sources"][relative]["commit"]
        require(any(re.fullmatch(r"[a-f0-9]{7,40}", token) and pin.startswith(token)
                    for token in tokens), f"Wrong {name} source version")
    return values


def sdk_header(sdk):
    return sdk / PROFILE["memory_variant"] / "include/sdkconfig.h"


def check_preserved(header):
    actual = normalized(header)
    for key, value in normalized(PROFILE["preserve"]).items():
        require(actual.get(key) == value, f"Unexpected active qio_opi setting: {key}")


def workspace(path):
    path = path.resolve()
    require(not path.is_relative_to(PROJECT) and not PROJECT.is_relative_to(path),
            "Use a separate build directory outside the project")
    require(not set(part.lower() for part in path.parts) & {"arduino", "arduino15", "v6.0"},
            "Do not prepare inside an installed SDK or sketchbook")
    marker = path / ".ofe-s3-sdk-workspace"
    require(not path.exists() or marker.is_file() or not any(path.iterdir()),
            "Preserving nonempty unrelated workspace")
    owned_file(marker, MARKER)
    return path


def owned_file(path, data):
    require(not path.is_symlink(), f"Refusing symbolic link: {path}")
    if path.exists():
        require(path.read_bytes() == data, f"Preserving other file: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def prepare(root, sdk):
    sdk = sdk.resolve()
    require(not sdk.is_relative_to(root) and not root.is_relative_to(sdk),
            "Baseline and workspace must be separate")
    version_file = sdk.parent / "versions.txt"
    check_versions(version_file)
    header_bytes = sdk_header(sdk).read_bytes()
    header = parse_config(header_bytes.decode(), header=True)
    active = resolve_aliases(header)
    check_preserved(header)
    require(header.get(FLAG) == "1", "Baseline must prefer WiFi/lwIP in PSRAM")
    base_config = parse_config((sdk / "sdkconfig").read_text(encoding="utf-8"))
    # The root sdkconfig describes QSPI PSRAM, not the active Octal/XIP variant.
    # Resolve boolean selections against the active header before making defaults.
    for key, value in list(base_config.items()):
        if value in ("y", "n"):
            base_config[key] = "y" if key in active else "n"
        elif key in active:
            base_config[key] = active[key]
    # The expanded QIO/OPI SDK's active header contains a few Kconfig choices
    # omitted by the builder's common sdkconfig. Preserve those active choices
    # explicitly so a fresh IDF configure cannot silently select another WiFi
    # TX-buffer mode or enable the PSRAM boot memtest.
    for key in ("CONFIG_SPIRAM_IGNORE_NOTFOUND", "CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER",
                "CONFIG_ESP_WIFI_STATIC_TX_BUFFER"):
        if key in active:
            base_config[key] = "y"
        elif key in ("CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER", "CONFIG_ESP_WIFI_STATIC_TX_BUFFER"):
            base_config[key] = "n"
    if "CONFIG_SPIRAM_MEMTEST" not in active:
        base_config["CONFIG_SPIRAM_MEMTEST"] = "n"
    require(normalized({k: v for k, v in base_config.items() if k in active}) ==
            {k: v for k, v in active.items() if k in base_config}, "Header/default mismatch")
    candidate = dict(base_config, **PROFILE["changes"])
    for name, values in (("baseline", base_config), ("candidate", candidate)):
        data = "\n".join(f"# {key} is not set" if value == "n" else f"{key}={value}"
                         for key, value in sorted(values.items())) + "\n"
        owned_file(root / f"{name}-r5.defaults", data.encode())
    owned_file(root / "baseline-qio_opi-sdkconfig.h", header_bytes)
    owned_file(root / "baseline-versions.txt", version_file.read_bytes())
    records = {}
    for relative in ("lib/libesp_wifi.a", "lib/liblwip.a"):
        require((sdk / relative).stat().st_size > 0, f"Missing baseline archive: {relative}")
        records[relative] = digest(sdk / relative)
    manifest = {"profile": PROFILE, "baseline": str(sdk), "archives": records,
                "active_header_sha256": hashlib.sha256(header_bytes).hexdigest(),
                "state": "prepared-only; SDK libraries not rebuilt or installed"}
    owned_file(root / "experiment-r5.json", (json.dumps(manifest, indent=2) + "\n").encode())
    print(f"Prepared baseline/candidate defaults: {root}. Not a rebuilt SDK.")


def fetch_builder(root):
    source = root / "builder"
    pin = PROFILE["sources"]["."]
    def git(*args):
        return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()
    if not (source / ".git").is_dir():
        require(not source.exists() or not any(source.iterdir()), "Preserving builder directory")
        subprocess.run(["git", "init", str(source)], check=True)
        git("config", "core.autocrlf", "false")
        git("config", "core.longpaths", "true")
        git("remote", "add", "origin", f"https://github.com/{pin['repository']}.git")
    require(git("remote", "get-url", "origin") == f"https://github.com/{pin['repository']}.git",
            "Unexpected builder repository")
    existing = subprocess.run(["git", "-C", str(source), "rev-parse", "--verify", "HEAD"],
                              capture_output=True, text=True)
    if existing.returncode == 0:
        require(existing.stdout.strip() == pin["commit"], "Preserving different builder checkout")
    else:
        git("fetch", "--depth", "1", "origin", pin["commit"])
        git("checkout", "--detach", "FETCH_HEAD")
    require(git("rev-parse", "HEAD") == pin["commit"], "Wrong builder source pin")
    print("Pinned builder fetched. IDF/components/toolchain and a real SDK rebuild still required.")


def audit(root, candidate):
    manifest = json.loads((root / "experiment-r5.json").read_text(encoding="utf-8"))
    require(manifest["profile"] == PROFILE, "Experiment profile changed; prepare a new workspace")
    baseline = root / "baseline-qio_opi-sdkconfig.h"
    require(digest(baseline) == manifest["active_header_sha256"], "Baseline header changed")
    old = parse_config(baseline.read_text(encoding="utf-8"), header=True)
    new = parse_config(sdk_header(candidate).read_text(encoding="utf-8"), header=True)
    check_preserved(new)
    expected = dict(old)
    expected.pop(FLAG)
    require(new == expected, "Active header differs beyond the one memory preference switch")
    before_versions = check_versions(root / "baseline-versions.txt")
    require(check_versions(candidate.parent / "versions.txt") == before_versions,
            "Managed component versions differ; not an isolated A/B candidate")
    for relative, before in manifest["archives"].items():
        file = candidate / relative
        require(file.stat().st_size > 0 and digest(file) != before,
                f"Unrebuilt archive (header-only changes are not enough): {relative}")
    print("Config/version/archive preflight passed. Not build provenance, install approval or hardware validation.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "fetch-builder", "audit"))
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, help="Baseline/candidate esp32s3 SDK directory")
    args = parser.parse_args()
    if args.action != "fetch-builder" and args.sdk is None:
        parser.error("--sdk is required")
    root = workspace(args.workspace)
    if args.action == "prepare":
        prepare(root, args.sdk)
    elif args.action == "fetch-builder":
        fetch_builder(root)
    else:
        audit(root, args.sdk)


if __name__ == "__main__":
    main()
