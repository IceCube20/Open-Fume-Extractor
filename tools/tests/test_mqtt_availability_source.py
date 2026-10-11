"""Guard the lifecycle hooks around the native-tested availability cache."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(encoding="utf-8")
CONFIG = (ROOT / "OpenFumeExtractorMaster/src/MasterBuildConfig.h").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"^static [^\n]+\b" + name + r"\([^\n]*\) \{[\s\S]*?^}", SOURCE, re.M)
    if not match:
        raise AssertionError(name)
    return match.group(0)


class MqttAvailabilitySourceTests(unittest.TestCase):
    def test_online_and_offline_share_the_success_only_cache(self):
        helper = function("mqtt_publish_module_availability")
        self.assertIn("!mqtt_client.connected() || !mqtt_availability_cache.needsPublish(addr, online)", helper)
        self.assertRegex(helper, r'if \(mqtt_publish_measured\([^\n]+true\)\)\s+mqtt_availability_cache.published\(addr, online\);')
        self.assertIn('mqtt_publish_module_availability(m.addr, m.online);', function("mqtt_publish_module_state"))
        self.assertIn('mqtt_publish_module_availability(addr, false);', function("mqtt_publish_module_offline"))

    def test_new_sessions_invalidate_before_initial_announcements(self):
        connected = function("mqtt_connect_if_needed").split("if (ok) {", 1)[1].split("} else {", 1)[0]
        self.assertIn("mqtt_availability_cache.reset();", connected)
        self.assertLess(connected.index("mqtt_availability_cache.reset();"), connected.index("mqtt_publish_measured"))

    def test_cleanup_invalidates_only_removed_current_namespace(self):
        cleanup = function("mqtt_cleanup_tick")
        self.assertIn("if (removed_from_registry && !base_changed) mqtt_availability_cache.forget(old_module.addr);", cleanup)
        stage = cleanup.split("if (mqtt_manifest_cleanup_stage == 2)", 1)[1]
        self.assertLess(stage.index('old_module.addr, "status"'), stage.index("mqtt_availability_cache.forget"))

    def test_forced_shutdown_does_not_deduplicate(self):
        shutdown = function("mqtt_publish_all_availability_offline")
        self.assertNotIn("mqtt_availability_cache", shutdown)
        self.assertIn('mqtt_publish_measured((root + "status").c_str(), "offline", true);', shutdown)
        self.assertIn('mqtt_publish_measured((root + path).c_str(), "offline", true);', shutdown)

    def test_live_data_cadence_and_state_payloads_stay_unchanged(self):
        for name, value in (("SERVICE", 10), ("STATE_PUBLISH", 350), ("MODULE_PUBLISH_GAP", 25)):
            self.assertIn(f"#define MASTER_MQTT_{name}_MS {value}UL", CONFIG)
        offline = function("mqtt_publish_module_offline")
        self.assertIn('mqtt_publish_measured(mqtt_module_topic(addr, "state").c_str(), json, true);', offline)
        self.assertIn('mqtt_publish_measured(mqtt_module_topic(m.addr, "state").c_str(), json.c_str(), true);', function("mqtt_publish_module_state"))


if __name__ == "__main__":
    unittest.main()
