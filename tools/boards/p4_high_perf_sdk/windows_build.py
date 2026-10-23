"""Build/export a separate P4 SDK using native Windows tools; never install/flash."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("p4_bootstrap", HERE / "bootstrap.py")
BOOT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BOOT)
SDK = BOOT.SDK
META_SPEC = importlib.util.spec_from_file_location("p4_windows_metadata", HERE / "windows_metadata.py")
META = importlib.util.module_from_spec(META_SPEC)
META_SPEC.loader.exec_module(META)
JQ_URL = "https://github.com/jqlang/jq/releases/download/jq-1.8.1/jq-windows-amd64.exe"
JQ_SHA256 = "23cb60a1354eed6bcc8d9b9735e8c7b388cd1fdcb75726b93bc299ef22dd9334"


def host_tools(root):
    target = root / "bin/jq.exe"
    if target.exists():
        SDK.require(hashlib.sha256(target.read_bytes()).hexdigest() == JQ_SHA256,
                    "Preserving unexpected jq executable")
    else:
        with urllib.request.urlopen(JQ_URL, timeout=120) as response:
            data = response.read()
        SDK.require(hashlib.sha256(data).hexdigest() == JQ_SHA256, "jq download checksum mismatch")
        BOOT.owned_file(target, data)
    python = (root / "python/Scripts/python.exe").as_posix()
    BOOT.owned_file(root / "bin/python3", f'#!/usr/bin/env bash\nexec "{python}" "$@"\n'.encode())
    print("Local jq verified; python3 shim targets only the dedicated P4 environment.", flush=True)


def environment(root, host):
    compiler = Path(os.environ["LOCALAPPDATA"]) / "Arduino15/packages/esp32/tools/esp-rv32/2601/bin"
    directories = [root / "bin", root / "python/Scripts", root / "builder/esp-idf/tools",
                   compiler, root / "tools/tools/cmake/3.30.2/bin", host / "ninja/1.12.1",
                   Path("C:/Program Files/Git/usr/bin"), Path("C:/Program Files/Git/cmd")]
    for path in directories:
        SDK.require(path.is_dir(), f"Build tool directory missing: {path}")
    env = os.environ.copy()
    # Never activate the installed IDF 6.0 profile or inherit its local registry.
    for name in ("IDF_COMPONENT_LOCAL_STORAGE_URL", "IDF_COMPONENT_STORAGE_URL", "PYTHONPATH"):
        env.pop(name, None)
    env.update(IDF_PATH=(root / "builder/esp-idf").as_posix(),
               IDF_TOOLS_PATH=(root / "tools").as_posix(),
               IDF_PYTHON_ENV_PATH=str(root / "python"), ESP_IDF_VERSION="5.5",
               IDF_TARGET="esp32p4", CHIP_VARIANT="esp32p4_es", IDF_CCACHE_ENABLE="0",
               IDF_PY_BUILD_JOBS="2",
               IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS="0", PYTHONUTF8="1")
    env["PATH"] = os.pathsep.join(map(str, directories)) + os.pathsep + env["PATH"]
    return env


def defaults(builder):
    document = json.loads((builder / "configs/builds.json").read_text())
    target = next(t for t in document["targets"] if t.get("chip_variant") == "esp32p4_es")
    features = ["common", "esp32p4_es", "debug_default"] + target["features"] + target["idf_libs"]
    SDK.require(SDK.FEATURE in features, "Prepare the cache profile before configuring")
    return ";".join("configs/defconfig." + feature for feature in features)


def export_libraries(root, env):
    builder = root / "builder"
    output = (builder / "out/tools/esp32-arduino-libs/esp32p4_es").resolve()
    SDK.require(output.is_relative_to(root.resolve()), "SDK output escapes the owned workspace")
    SDK.check_sources(builder)
    SDK.require((builder / "build/arduino-lib-builder.elf").is_file(), "Finish the SDK build first")
    SDK.require(" " not in str(root), "Upstream Bash exporter requires a workspace without spaces")
    original = SDK.git(builder, "show", "HEAD:tools/copy-libs.sh") + "\n"
    script = builder / "tools/ofe-copy-libs-windows-v3.sh"
    BOOT.owned_file(script, META.exporter_script(original).encode())
    env = dict(env, OFE_WINDOWS_METADATA=(HERE / "windows_metadata.py").as_posix())
    # The upstream exporter writes only under the verified workspace's builder/out.
    command = ["C:/Program Files/Git/bin/bash.exe", script.as_posix(),
               "esp32p4", "esp32p4_es", "qio", "", ""]
    subprocess.run(command, cwd=builder, env=env, check=True)
    META.publish_memory_variant(builder)


def finish_sdk(root, env, skip_export=False):
    builder = root / "builder"
    candidate = builder / "out/tools/esp32-arduino-libs/esp32p4_es"
    if skip_export:
        SDK.check_sources(builder)
        for relative in ("sdkconfig", "flags/ld_libs", "lib/libesp_system.a", "qio_qspi/include/sdkconfig.h"):
            SDK.require((candidate / relative).is_file(), f"Complete the common SDK export first: {relative}")
        SDK.require((candidate / "sdkconfig").read_bytes() == (builder / "sdkconfig").read_bytes(),
                    "Exported configuration differs; rebuild and export first")
    else:
        export_libraries(root, env)
    # Native jq CRLF output can make the upstream target comparison miss this
    # move. Handle it with parsed JSON, including on an interrupted-export retry.
    META.publish_memory_variant(builder)
    python = root / "python/Scripts/python.exe"
    idf = builder / "esp-idf/tools/idf.py"
    target = next(t for t in json.loads((builder / "configs/builds.json").read_text())["targets"]
                  if t.get("chip_variant") == "esp32p4_es")
    features = ["common", "esp32p4_es", "debug_default"] + target["features"]

    def variant(kind, fragments, action):
        name = kind + "-" + "-".join(fragments)
        directory = builder / ("build-" + name)
        configs = ";".join("configs/defconfig." + feature for feature in features + fragments)
        command = [str(python), str(idf), "-B", str(directory), "-DIDF_TARGET=esp32p4",
                   "-DSDKCONFIG=" + (builder / ("sdkconfig-" + name)).as_posix(),
                   "-DSDKCONFIG_DEFAULTS=" + configs, action]
        subprocess.run(command, cwd=builder, env=env, check=True)
        return directory

    (candidate / "bin").mkdir(exist_ok=True)
    for fragments in target["bootloaders"]:
        if fragments == target["idf_libs"]:
            directory = builder / "build"
        else:
            directory = variant("boot", fragments, "bootloader")
        sdkconfig = builder / ("sdkconfig" if directory.name == "build" else "sdkconfig-" + directory.name[6:])
        config = SDK.config(sdkconfig.read_text())
        mode = config["CONFIG_LIB_BUILDER_FLASHMODE"].strip('"')
        frequency = config["CONFIG_LIB_BUILDER_FLASHFREQ"].strip('"')
        shutil.copy2(directory / "bootloader/bootloader.elf", candidate / f"bin/bootloader_{mode}_{frequency}.elf")

    commit = SDK.git(builder / "esp-idf", "rev-parse", "--short", "HEAD")
    tags = SDK.git(builder / "esp-idf", "tag", "--points-at", "HEAD")
    for fragments in target["mem_variants"]:
        directory = variant("mem", fragments, "reconfigure")
        cmake = root / "tools/tools/cmake/3.30.2/bin/cmake.exe"
        subprocess.run([str(cmake), "--build", str(directory), "--parallel", "2", "--target",
                        "esp-idf/spi_flash/libspi_flash.a"], cwd=builder, env=env, check=True)
        config = SDK.config((builder / ("sdkconfig-" + directory.name[6:])).read_text())
        mode = config["CONFIG_LIB_BUILDER_FLASHMODE"].strip('"')
        destination = candidate / (mode + "_qspi")
        (destination / "include").mkdir(parents=True, exist_ok=True)
        header = (directory / "config/sdkconfig.h").read_bytes()
        header += f'\n#define CONFIG_ARDUINO_IDF_COMMIT "{commit}"\n#define CONFIG_ARDUINO_IDF_BRANCH "{tags}"\n'.encode()
        BOOT.owned_file(destination / "include/sdkconfig.h", header)
        shutil.copy2(directory / "esp-idf/spi_flash/libspi_flash.a", destination / "libspi_flash.a")

    subprocess.run([str(python), str(idf), "srmodels_bin"], cwd=builder, env=env, check=True)
    (candidate / "esp_sr").mkdir(exist_ok=True)
    shutil.copy2(builder / "build/srmodels/srmodels.bin", candidate / "esp_sr/srmodels.bin")
    shutil.copy2(builder / "partitions.csv", candidate / "esp_sr/partitions.csv")
    records = []
    names = {".": "lib-builder", "esp-idf": "esp-idf", "components/arduino": "arduino",
             "components/arduino_tinyusb/tinyusb": "tinyusb"}
    for relative, name in names.items():
        records.append(name + ": " + SDK.git(builder / relative, "rev-parse", "HEAD"))
    from ruamel.yaml import YAML
    for component in sorted((builder / "managed_components").iterdir()):
        manifest = component / "idf_component.yml"
        if manifest.is_file():
            version = YAML(typ="safe").load(manifest.read_text(encoding="utf-8")).get("version")
            SDK.require(version is not None, f"Missing built component version: {component}")
            records.append(component.name + ": " + str(version))
    BOOT.owned_file(candidate / "versions.txt", ("\n".join(records) + "\n").encode())
    SDK.check_versions(candidate / "versions.txt")
    print(f"Exported native SDK candidate: {candidate}. Not installed or hardware-tested.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("host-tools", "configure", "build", "export-libs", "finish-sdk"))
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--host-tools", type=Path, default=Path("C:/Espressif/tools"))
    parser.add_argument("--skip-export", action="store_true", help="Resume finish-sdk after a completed common export")
    args = parser.parse_args()
    if args.skip_export and args.action != "finish-sdk":
        parser.error("--skip-export is only valid for finish-sdk")
    root = BOOT.workspace(args.workspace)
    if args.action == "host-tools":
        host_tools(root)
        return
    builder = root / "builder"
    env = environment(root, args.host_tools)
    if args.action == "export-libs":
        export_libraries(root, env)
        return
    if args.action == "finish-sdk":
        finish_sdk(root, env, args.skip_export)
        return
    python = root / "python/Scripts/python.exe"
    idf = builder / "esp-idf/tools/idf.py"
    command = [str(python), str(idf), "-DIDF_TARGET=esp32p4",
               "-DSDKCONFIG_DEFAULTS=" + defaults(builder)]
    if args.action == "configure":
        command += ["reconfigure"]
    else:
        SDK.check_sources(builder)
        command += ["build"]
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=builder, env=env, check=True)


if __name__ == "__main__":
    main()
