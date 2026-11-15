"""Compile actual MCU metadata and all base discovery calls, not device stubs."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
TEST = Path(__file__).resolve().parent
OUT = ROOT / "tools/tests/build-checks/mcu-mqtt-discovery-native"
VC = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207")
SDK = Path("C:/Program Files (x86)/Windows Kits/10")


def run():
    OUT.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["INCLUDE"] = ";".join(map(str, (VC / "include", SDK / "Include/10.0.26100.0/ucrt", SDK / "Include/10.0.26100.0/shared", SDK / "Include/10.0.26100.0/um")))
    env["LIB"] = ";".join(map(str, (VC / "lib/x64", SDK / "Lib/10.0.26100.0/ucrt/x64", SDK / "Lib/10.0.26100.0/um/x64")))
    topics = {}
    for variant, serial, device_id in (("S3", "0000ECF7384AEC24", "open-fume-extractor-384aec24"),
                                      ("P4", "0000ACFFE1A0ED30", "open-fume-extractor-e1a0ed30")):
        src = ROOT / "OpenFumeExtractorMaster" / ("OpenFumeExtractorMCU_" + variant) / "src"
        source = (src / "MasterMqtt.inc.h").read_text(encoding="utf-8-sig")
        config = (src / "MasterBuildConfig.h").read_text(encoding="utf-8-sig")
        network = (src / "MasterNetwork.inc.h").read_text(encoding="utf-8-sig")
        (OUT / "identity.inc.h").write_text(config[config.index("#define MASTER_FW_MAJOR"):config.index('extern "C" inline const volatile char OFE_MASTER_FW_SIGNATURE')], encoding="utf-8")
        functions = []
        for signature in ("static bool mqtt_legacy_shared_root(", "static bool mqtt_factory_topic_root(", "static String mqtt_normalized_base_topic(", "static String mqtt_client_identity("):
            start = network.index(signature)
            functions.append(network[start:network.index("\n}", start) + 2])
        for signature in ("static String mqtt_topic_path(", "static String mqtt_module_topic(", "static String mqtt_module_command_leaf(",
                          "static String mqtt_topic_for_base_namespace(", "static bool mqtt_manifest_clear_old_module_value(",
                          "static String mqtt_device_json()", "static void mqtt_publish_discovery_entity(", "static void mqtt_publish_discovery_control(",
                          "static void mqtt_callback("):
            start = source.index(signature)
            functions.append(source[start:source.index("\n}", start) + 2])
        start = source.index('  mqtt_publish_discovery_entity("binary_sensor", "output"')
        end = source.index("  uint8_t order[ModuleRegistry::MAX_MODULES];", start)
        (OUT / "discovery.inc.h").write_text("\n".join(functions) + "\nstatic void publish_all() {\n" + source[start:end] + "\n}\n", encoding="utf-8")
        exe = OUT / (variant + ".exe")
        command = [str(VC / "bin/Hostx64/x64/cl.exe"), "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/W3", "/O2", "/D_CRT_SECURE_NO_WARNINGS"]
        if variant == "P4":
            command.append("/DCONFIG_IDF_TARGET_ESP32P4=1")
        command += ["/I" + str(TEST / "stubs"), "/I" + str(ROOT / "tools/tests/master_runtime_native/buffer_stubs"), "/I" + str(src), "/I" + str(OUT), str(TEST / "test.cpp"), "/Fo" + str(OUT) + "/", "/Fe" + str(exe)]
        subprocess.run(command, env=env, check=True)
        captured = subprocess.run([str(exe)], capture_output=True, encoding="utf-8", check=True)
        messages = [json.loads(line) for line in captured.stdout.splitlines()]
        for message in messages:
            device = message["dev"]
            assert device["ids"] == [device_id]
            assert device["name"] == "0x01 Open Fume Extractor MCU-" + variant
            assert device["mdl"] == "MCU-" + variant
            assert device["sn"] == serial
            assert device["mf"] == "IceCube20" and device["sw"]
            assert device["sw"].endswith("RC") and "Developer" not in device["sw"]
            assert device["cu"] == "http://192.0.2.1/"
            assert message["uniq_id"].startswith(device["ids"][0] + "_")
            for key in ("stat_t", "cmd_t", "avty_t", "json_attr_t"):
                if key in message:
                    assert message[key].startswith("open-fume-extractor/" + serial + "/"), (key, message[key])
                    topics.setdefault(variant, set()).add(message[key])
            for availability in message.get("availability", []):
                assert availability["topic"].startswith("open-fume-extractor/" + serial + "/")
        print(f"PASS: MCU-{variant}: {len(messages)} real discovery messages with valid MCU device metadata; DE/EN, developer on/off, internal/PSRAM")
    assert topics["S3"].isdisjoint(topics["P4"])
    print("PASS: serial-specific state/command/availability/module roots; legacy/custom migration; no shared-root deletes; HA identifiers unchanged")


if __name__ == "__main__":
    run()
