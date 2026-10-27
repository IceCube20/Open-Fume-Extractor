"""Build the pinned S3 SDK in an owned workspace; no installation or flashing."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

HERE = Path(__file__).resolve().parent
P4_ONLY_COMPONENTS = {
    "espressif__eppp_link",
    "espressif__esp_hosted",
    "espressif__esp_serial_slave_link",
    "espressif__esp_wifi_remote",
}
SPEC = importlib.util.spec_from_file_location("s3_sdk_prepare", HERE / "prepare.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)


def run(command, cwd=None, env=None):
    print("+ " + " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)


def git(source, *args):
    return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()


def fetch(root):
    SDK.fetch_builder(root)
    for relative, pin in SDK.PROFILE["sources"].items():
        source = root / "builder" / relative
        if relative == ".":
            continue
        if not (source / ".git").exists():
            SDK.require(not source.exists() or not any(source.iterdir()), f"Preserving directory: {source}")
            run(["git", "init", source])
            git(source, "config", "core.longpaths", "true")
            git(source, "config", "core.autocrlf", "false")
            git(source, "remote", "add", "origin", f"https://github.com/{pin['repository']}.git")
            run(["git", "-C", source, "fetch", "--depth", "1", "origin", pin["commit"]])
            run(["git", "-C", source, "checkout", "--detach", "FETCH_HEAD"])
        SDK.require(git(source, "rev-parse", "HEAD") == pin["commit"], f"Wrong source revision: {relative}")
        run(["git", "-C", source, "-c", "core.longpaths=true", "submodule", "update",
             "--init", "--recursive", "--depth", "1", "--jobs", "2"])
    SDK.owned_file(root / "source-pins.json", (json.dumps(SDK.PROFILE["sources"], indent=2) + "\n").encode())


def pin_components(root):
    builder = root / "builder"
    versions = SDK.check_versions(root / "baseline-versions.txt")
    dependencies = {name.replace("__", "/", 1): {"version": "==" + value, "require": "no"}
                    for name, value in versions.items()
                    if "__" in name and value and name not in P4_ONLY_COMPONENTS}
    component = builder / "components/ofe_sdk_version_pins"
    SDK.owned_file(component / "CMakeLists.txt", b"idf_component_register()\n")
    pin_manifest = component / "idf_component.yml"
    if pin_manifest.exists():
        previous = {name.replace("__", "/", 1): {"version": "==" + value, "require": "no"}
                    for name, value in versions.items() if "__" in name and value}
        SDK.require(json.loads(pin_manifest.read_text()).get("dependencies") == previous,
                    f"Preserving unexpected generated pins: {pin_manifest}")
        pin_manifest.write_text(json.dumps(
            {"targets": ["esp32s3"], "dependencies": dependencies}, indent=2) + "\n")
    else:
        SDK.owned_file(pin_manifest, (json.dumps(
            {"targets": ["esp32s3"], "dependencies": dependencies}, indent=2) + "\n").encode())
    for source, patches in ((builder / "esp-idf", [builder / "patches/esp32s2_i2c_ll_master_init.diff",
                                                builder / "patches/lwip_max_tcp_pcb.diff"]),
                            (builder / "components/arduino_tinyusb/tinyusb",
                             sorted((builder / "patches/tinyusb").glob("*.diff")))):
        for patch in patches:
            SDK.require(patch.is_file(), f"Missing upstream patch: {patch}")
            applied = subprocess.run(["git", "-C", str(source), "apply", "--reverse", "--check", str(patch)],
                                     capture_output=True).returncode == 0
            if not applied:
                run(["git", "-C", source, "apply", "--check", patch])
                run(["git", "-C", source, "apply", patch])


def environment(root):
    env = os.environ.copy()
    for key in ("PYTHONPATH", "IDF_COMPONENT_LOCAL_STORAGE_URL", "IDF_COMPONENT_STORAGE_URL"):
        env.pop(key, None)
    env.update(IDF_PATH=(root / "builder/esp-idf").as_posix(),
               IDF_TOOLS_PATH=(root / "tools").as_posix(),
               IDF_PYTHON_ENV_PATH=(root / "python").as_posix(),
               PIP_CACHE_DIR=(root / "pip-cache").as_posix(),
               IDF_COMPONENT_CACHE_PATH=(root / "component-cache").as_posix(),
               ESP_ROM_ELF_DIR=(root / "tools/tools/esp-rom-elfs/20241011").as_posix(),
               GIT_CONFIG_GLOBAL=(root / ".gitconfig").as_posix(),
               IDF_TARGET="esp32s3", IDF_CCACHE_ENABLE="0", ESP_IDF_VERSION="5.4",
               IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS="0", PYTHONUTF8="1")
    paths = [root / "python/Scripts", root / "builder/esp-idf/tools"]
    for tools in (root / "tools/tools").glob("*"):
        for version in tools.iterdir():
            for executable in version.rglob("*.exe"):
                if executable.name in ("cmake.exe", "ninja.exe", "xtensa-esp32s3-elf-gcc.exe",
                                       "xtensa-esp-elf-gcc.exe", "riscv32-esp-elf-gcc.exe"):
                    paths.append(executable.parent)
    paths += [Path("C:/Program Files/Git/cmd"), Path("C:/Program Files/Git/usr/bin")]
    env["PATH"] = os.pathsep.join(map(str, dict.fromkeys(paths))) + os.pathsep + env["PATH"]
    return env


def setup(root):
    env = environment(root)
    tools = root / "builder/esp-idf/tools/idf_tools.py"
    SDK.require(tools.is_file(), "Fetch sources first")
    run(["git", "config", "--global", "--add", "safe.directory", "*"], env=env)
    # Only install tools used by an S3 firmware build; IDF's default package
    # also pulls in unrelated RISC-V, debugger, OpenOCD and flashing tools.
    run([sys.executable, tools, "install", "xtensa-esp-elf", "cmake", "ninja", "idf-exe", "esp-rom-elfs",
         "--targets=esp32s3"], env=env)
    base_python = Path(sys.base_prefix) / "python.exe"
    SDK.require(base_python.is_file(), f"Base Python missing: {base_python}")
    run([base_python, tools, "install-python-env", "--features=core"], env=env)


def build(root, profile, action, jobs):
    builder = root / "builder"
    build_dir = root / f"build-{profile}-r5"
    for relative, pin in SDK.PROFILE["sources"].items():
        SDK.require(git(builder / relative, "rev-parse", "HEAD") == pin["commit"],
                    f"Unexpected source revision: {relative}")
    defaults = root / f"{profile}-r5.defaults"
    SDK.require(defaults.is_file(), "Prepare defaults first")
    env = environment(root)
    python = root / "python/Scripts/python.exe"
    idf = builder / "esp-idf/tools/idf.py"
    sdkconfig = root / f"sdkconfig-{profile}-r5"
    run([python, idf, "-B", build_dir, "-DIDF_TARGET=esp32s3",
         "-DSDKCONFIG=" + sdkconfig.as_posix(),
         "-DSDKCONFIG_DEFAULTS=" + defaults.as_posix(), "reconfigure"], cwd=builder, env=env)
    header = build_dir / "config/sdkconfig.h"
    baseline = SDK.parse_config((root / "baseline-qio_opi-sdkconfig.h").read_text(), True)
    generated = SDK.parse_config(header.read_text(), True)
    # Exporter adds these metadata fields after compilation, not through Kconfig.
    baseline = {key: value for key, value in baseline.items() if key not in (
        "CONFIG_ARDUINO_IDF_COMMIT", "CONFIG_ARDUINO_IDF_BRANCH")}
    if profile == "candidate":
        baseline.pop(SDK.FLAG)
    differences = {key: [baseline.get(key), generated.get(key)]
                   for key in baseline.keys() | generated.keys() if baseline.get(key) != generated.get(key)}
    (root / f"config-diff-{profile}-r5.json").write_text(json.dumps(differences, indent=2) + "\n")
    SDK.require(not differences, f"Configuration differs; inspect config-diff-{profile}-r5.json before building")
    if action == "build":
        cmake = shutil.which("cmake.exe", path=env["PATH"])
        SDK.require(cmake is not None, "CMake missing from isolated environment")
        run([cmake, "--build", build_dir, "--parallel", str(jobs)], cwd=builder, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("fetch", "pin-components", "setup", "configure", "build"))
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--profile", choices=("baseline", "candidate"), default="candidate")
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=2)
    args = parser.parse_args()
    root = SDK.workspace(args.workspace)
    os.environ["GIT_CONFIG_GLOBAL"] = str(root / ".gitconfig")
    if args.action == "fetch":
        fetch(root)
    elif args.action == "pin-components":
        pin_components(root)
    elif args.action == "setup":
        setup(root)
    else:
        build(root, args.profile, args.action, args.jobs)


if __name__ == "__main__":
    main()
