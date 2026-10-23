"""Prepare/check a separate P4 PRE-V3 SDK. Never install, download or flash."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[2]
FEATURE = "ofe_p4_es_l2_highperf"
SOURCE_PINS = {".": "6671d0b", "esp-idf": "b774170ff46",
               "components/arduino": "e7c1e8cd4",
               "components/arduino_tinyusb/tinyusb": "88c7f0b81"}
CACHE_KEYS = {"CONFIG_CACHE_L2_CACHE_128KB", "CONFIG_CACHE_L2_CACHE_256KB",
              "CONFIG_CACHE_L2_CACHE_512KB", "CONFIG_CACHE_L2_CACHE_SIZE",
              "CONFIG_CACHE_L2_CACHE_LINE_64B", "CONFIG_CACHE_L2_CACHE_LINE_128B",
              "CONFIG_CACHE_L2_CACHE_LINE_SIZE"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def versions(text):
    result = {}
    for line in text.splitlines():
        if line.strip():
            key, value = line.split(":", 1)
            require(key not in result, f"Duplicate component: {key}")
            result[key] = value.strip().split()[-1]
    return result


def config(text):
    result = {}
    for line in text.splitlines():
        if line.startswith("CONFIG_"):
            key, value = line.split("=", 1)
            # IDF emits some identical deprecated aliases twice.
            require(key not in result or result[key] == value, f"Conflicting setting: {key}")
            result[key] = value
        elif re.fullmatch(r"# CONFIG_\w+ is not set", line):
            key = line.split()[1]
            require(key not in result or result[key] == "n", f"Conflicting setting: {key}")
            result[key] = "n"
    return result


def check_versions(path):
    expected = versions((HERE / "versions.txt").read_text())
    actual = versions(path.read_text())
    require(actual.keys() == expected.keys(), f"Component versions differ from the 3.3.12 baseline: {path}")
    for name, value in expected.items():
        found = actual[name]
        if name in ("lib-builder", "esp-idf", "arduino", "tinyusb"):
            # Git's automatic abbreviation length varies with clone depth/history.
            matches = bool(re.fullmatch(r"[0-9a-f]{7,40}", found)) and (
                found.startswith(value) or value.startswith(found))
        else:
            matches = found == value
        require(matches, f"Component versions differ from the 3.3.12 baseline: {name} ({found})")


def validate_sdk(sdk, baseline, high_perf):
    check_versions(sdk / "versions.txt")
    current = config((sdk / "sdkconfig").read_text())
    expected = {"CONFIG_IDF_TARGET": '"esp32p4"',
                "CONFIG_ESP32P4_SELECTS_REV_LESS_V3": "y",
                "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ": "360",
                "CONFIG_SPIRAM_SPEED": "200"}
    for key, value in expected.items():
        require(current.get(key) == value, f"Unexpected {key}: {current.get(key)}")
    size, line = ((262144, 128) if high_perf else (131072, 64))
    for key, value in (("CONFIG_CACHE_L2_CACHE_SIZE", size),
                       ("CONFIG_CACHE_L2_CACHE_LINE_SIZE", line)):
        require(int(current.get(key, "0"), 0) == value, f"Unexpected {key}")
    if baseline is not None:
        before = config((baseline / "sdkconfig").read_text())
        differences = {key for key in before.keys() | current.keys()
                       if before.get(key, "n") != current.get(key, "n")}
        require(not differences - CACHE_KEYS,
                f"Non-cache config changes need review: {sorted(differences - CACHE_KEYS)}")
        for folder in ("include", "lib", "ld", "flags", "bin", "qio_qspi", "dio_qspi", "esp_sr"):
            required = {p.relative_to(baseline) for p in (baseline / folder).rglob("*")
                        if p.is_file()}
            require(required, f"Baseline folder missing/empty: {folder}")
            for relative in required:
                require((sdk / relative).is_file() and
                        ((sdk / relative).stat().st_size > 0 or (baseline / relative).stat().st_size == 0),
                        f"Incomplete SDK: {relative}")
        # A changed config beside identical archives is not a rebuilt SDK.
        archives = list((baseline / "lib").glob("*.a"))
        require(any((sdk / "lib" / p.name).read_bytes() != p.read_bytes() for p in archives),
                "SDK archives are unchanged; config flags alone do not rebuild the SDK")
    for variant in ("qio_qspi", "dio_qspi"):
        header = (sdk / variant / "include/sdkconfig.h").read_text()
        for key, value in (("CONFIG_CACHE_L2_CACHE_SIZE", size),
                           ("CONFIG_CACHE_L2_CACHE_LINE_SIZE", line)):
            match = re.search(r"^#define " + key + r"\s+(\S+)", header, re.M)
            require(match and int(match[1], 0) == value,
                    f"Inconsistent {variant} generated header: {key}")
    return current


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()


def validate_dependency_lock(document):
    require(isinstance(document, dict) and document.get("target") == "esp32p4",
            "Dependency lock must target esp32p4")
    dependencies = document.get("dependencies", {})
    require(isinstance(dependencies, dict), "Invalid dependency lock")
    expected = versions((HERE / "versions.txt").read_text())
    expected = {name.replace("__", "/", 1): value for name, value in expected.items()
                if name not in ("lib-builder", "esp-idf", "arduino", "tinyusb")}
    expected["idf"] = "5.5.5"
    for name, version in expected.items():
        entry = dependencies.get(name, {})
        require(isinstance(entry, dict) and str(entry.get("version")) == version,
                f"Dependency lock version differs/missing: {name}; need {version}")


def check_sources(builder):
    for relative, pin in SOURCE_PINS.items():
        source = builder / relative
        require(git(source, "rev-parse", "HEAD").startswith(pin), f"Wrong source revision: {source}")
        require(all(not row.startswith(("-", "+", "U")) for row in
                    git(source, "submodule", "status", "--recursive").splitlines()),
                f"Initialize matching submodules: {source}")
        if relative in ("esp-idf", "components/arduino"):
            require(not git(source, "diff", "HEAD", "--name-only"),
                    f"Source has local tracked changes: {source}")
    require((builder / "dependencies.lock").is_file(),
            "Resolve/pin managed components first; dependencies.lock is required")
    try:
        from ruamel.yaml import YAML
    except ImportError as error:
        raise ValueError("Run check-sources in the installed IDF Python environment (ruamel.yaml required)") from error
    validate_dependency_lock(YAML(typ="safe").load((builder / "dependencies.lock").read_text()))


def separate_workspace(builder, baseline):
    require(builder != baseline and baseline not in builder.parents and builder not in baseline.parents,
            "Builder must be separate from installed SDK")
    require(builder != PROJECT and PROJECT not in builder.parents,
            "Keep multi-GB SDK build output outside the OFE project")
    require(not {part.lower() for part in builder.parts} & {"arduino15", "arduino"},
            "Refusing to modify an Arduino installation/sketchbook")


def prepare(builder, baseline):
    separate_workspace(builder, baseline)
    validate_sdk(baseline, None, False)
    require(git(builder, "rev-parse", "HEAD").startswith(SOURCE_PINS["."]),
            "Wrong lib-builder commit; use 6671d0b")
    changed = set(git(builder, "diff", "HEAD", "--name-only").splitlines())
    require(not changed - {"configs/builds.json"}, "Builder has unrelated tracked changes")
    require("CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y" in
            (builder / "configs/defconfig.esp32p4_es").read_text(), "Wrong chip variant config")
    path = builder / "configs/builds.json"
    original = subprocess.check_output(["git", "-C", str(builder), "show", "HEAD:configs/builds.json"])
    document = json.loads(original)
    targets = [t for t in document["targets"] if t.get("chip_variant") == "esp32p4_es"]
    require(len(targets) == 1 and targets[0]["target"] == "esp32p4", "PRE-V3 target missing")
    targets[0]["features"].append(FEATURE)
    modified = (json.dumps(document, indent=2) + "\n").encode()
    require(path.read_bytes() in (original, modified), "builds.json has unrelated edits; preserve them")
    fragment = (builder / "configs" / ("defconfig." + FEATURE))
    data = (HERE / "sdkconfig.defaults").read_bytes()
    require(not fragment.exists() or fragment.read_bytes() == data, "Feature file has unrelated edits")
    # All validation precedes edits; only this explicitly selected builder is written.
    fragment.write_bytes(data)
    path.write_bytes(modified)
    print("Prepared cache-only PRE-V3 profile. Nothing installed or built.")
    print("After pinned source/dependency setup and IDF export, run:")
    print("python3 <OFE>/tools/boards/p4_high_perf_sdk/prepare.py check-sources --builder <BUILDER>")
    print("cd <BUILDER> && bash build.sh -s -t esp32p4_es")
    print("Output: <BUILDER>/out/tools/esp32-arduino-libs/esp32p4_es")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "check-sources", "check-sdk"))
    parser.add_argument("--builder", type=Path)
    parser.add_argument("--baseline-sdk", type=Path)
    parser.add_argument("--sdk", type=Path)
    args = parser.parse_args()
    try:
        if args.action == "prepare":
            require(args.builder is not None and args.baseline_sdk is not None,
                    "prepare needs --builder and --baseline-sdk")
            prepare(args.builder.resolve(), args.baseline_sdk.resolve())
        elif args.action == "check-sources":
            require(args.builder is not None, "check-sources needs --builder")
            check_sources(args.builder.resolve())
            print("Pinned source revisions/submodules and managed dependency versions checked; output still needs validation.")
        else:
            require(args.sdk is not None and args.baseline_sdk is not None,
                    "check-sdk needs --sdk and --baseline-sdk")
            baseline, sdk = args.baseline_sdk.resolve(), args.sdk.resolve()
            require(sdk != baseline, "Candidate must not be the installed baseline")
            validate_sdk(baseline, None, False)
            validate_sdk(sdk, baseline, True)
            digest = hashlib.sha256((sdk / "sdkconfig").read_bytes()).hexdigest()
            print(f"Cache-only SDK candidate passed structural checks; sdkconfig SHA256 {digest}")
            print("This structural check does not install the SDK or verify runtime performance/BLE/OTA.")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
