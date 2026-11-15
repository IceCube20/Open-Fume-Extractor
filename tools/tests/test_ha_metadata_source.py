"""Keep optional HA metadata reuse out of live-state and MQTT task paths."""
from pathlib import Path
import unittest

SRC = Path(__file__).resolve().parents[2] / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3" / "src"


class HaMetadataSourceTests(unittest.TestCase):
    def test_cache_is_external_only_with_fresh_fallback(self):
        source = (SRC / "HaMetadataCache.h").read_text(encoding="utf-8")
        self.assertIn("key_.reservePsram(256) && value_.reservePsram(256)", source)
        self.assertNotIn("reserveInternal", source)
        self.assertNotIn("reservePreferred", source)
        self.assertIn("memcmp(", source)
        self.assertIn("build(destination);", source)

    def test_only_metadata_uses_cache(self):
        status = (SRC / "WebStatus.inc.h").read_text(encoding="utf-8")
        mqtt = (SRC / "MasterMqtt.inc.h").read_text(encoding="utf-8")
        self.assertEqual(status.count(".appendTo("), 1)
        self.assertEqual(mqtt.count(".appendTo("), 1)
        projection = mqtt.split("static void mqtt_append_jbc_usb_state(", 1)[1]
        self.assertNotIn("HaMetadataCache", projection)
        state = status.split("static bool build_state_json(", 1)[1]
        self.assertNotIn("HaMetadataCache", state)
        self.assertIn("if (include_ha) ha_append_route_options(json);", state)
        self.assertIn("if (include_universal_descriptor) append_universal_entity_defs_json(json, m);", state)

    def test_route_key_excludes_measurements(self):
        mqtt = (SRC / "MasterMqtt.inc.h").read_text(encoding="utf-8")
        key = mqtt.split("static void ha_route_metadata_key(", 1)[1].split("static void ha_append_route_options(", 1)[0]
        for live in ("io_output_mask", "io_input_mask", "uptime", "millis()", "module_cpu", "timeout_count"):
            self.assertNotIn(live, key)
        for metadata in ("m.label", "m.uid", "m.addr", "m.caps", "m.type", "m.online",
                         "m.io_in1_alias", "m.io_in2_alias", "m.universal_descriptor_valid",
                         "m.universal_descriptor", "web_lang", "registry.count()",
                         "mainInputSourceType()", "mainInputSourceAddr()", "mainInputSourceBit()",
                         "preferredOutputAddr()"):
            self.assertIn(metadata, key)


if __name__ == "__main__":
    unittest.main()
