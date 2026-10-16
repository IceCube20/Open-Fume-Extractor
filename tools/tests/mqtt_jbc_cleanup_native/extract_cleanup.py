"""Compile the real manifest codec and JBC cleanup, not a second implementation."""
from pathlib import Path
import re
import sys

source = (Path(__file__).resolve().parents[3] / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"^static [^\n]+\b" + name + r"\([^;{}]*\) \{.*?^}", source, re.M | re.S)
    if not match:
        raise ValueError(name)
    return match.group(0)


types = source[source.index("static const uint32_t MQTT_HA_MANIFEST_MAGIC"):source.index("static void* mqtt_manifest_alloc")]
functions = [function(name) for name in (
    "mqtt_manifest_component_code", "mqtt_manifest_component_text",
    "mqtt_manifest_static_key", "mqtt_manifest_static_suffix",
    "mqtt_manifest_encode_entity", "mqtt_manifest_entity_equal",
    "mqtt_manifest_find_module", "mqtt_manifest_find_module_const",
    "mqtt_manifest_module_contains", "mqtt_discovery_topic_for_namespace", "mqtt_topic_for_base_namespace",
    "mqtt_manifest_clear_discovery_entity", "mqtt_clear_module_entity",
    "mqtt_manifest_clear_old_module_value", "mqtt_manifest_finish_cleanup", "mqtt_registry_has_addr",
)]
cleanup = source[source.index("struct MqttJbcCleanupState"):source.index("static void mqtt_clear_module_state")]
Path(sys.argv[1]).write_text(types + "\n" + "\n\n".join(functions) + "\n" + cleanup + "\n" + function("mqtt_cleanup_tick"), encoding="utf-8")
