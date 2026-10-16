"""Integration contracts for the bounded, production-tested JBC MQTT sweep."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(encoding="utf-8")


class MqttJbcCleanupSourceTests(unittest.TestCase):
    def test_absent_station_cannot_republish_cached_ports_or_controls(self):
        discovery = SOURCE.split("static void mqtt_publish_jbc_usb_core_discovery(", 1)[1].split(
            "static void mqtt_publish_module_discovery(", 1)[0]
        gate = "if (m.online && c.valid && !c.linked) return;"
        common, station = discovery.split(gate)
        for key in ("jbc_link", "work", "stand"):
            self.assertIn(f'"binary_sensor", "{key}"', common)
        self.assertIn("mqtt_publish_jbc_usb_control_discovery(m);", station)
        self.assertNotIn("mqtt_publish_jbc_usb_control_discovery(m);", common)
        signature = SOURCE.split("static String mqtt_module_discovery_signature_now()", 1)[1].split(
            "static void mqtt_publish_discovery_custom", 1)[0]
        self.assertIn("sig += core.linked ? '1' : '0';", signature)

    def test_confirmation_is_serviced_even_when_discovery_signature_is_unchanged(self):
        loop = SOURCE.split("static void mqtt_loop() {", 1)[1]
        self.assertIn("mqtt_jbc_cleanup_needs_confirmation()) mqtt_publish_discovery();", loop)

    def test_failed_discovery_does_not_schedule_a_destructive_cleanup(self):
        publish = SOURCE.split("static void mqtt_publish_discovery() {", 1)[1].split("static bool mqtt_payload_bool", 1)[0]
        failed, success = publish.split("if (mqtt_discovery_publish_failed)", 1)[1].split("} else {", 1)
        self.assertNotIn("mqtt_jbc_cleanup_schedule()", failed)
        self.assertIn("if (!mqtt_discovery_publish_failed) mqtt_jbc_cleanup_schedule();", publish)
        self.assertLess(publish.index("mqtt_jbc_cleanup_schedule()"), publish.index("mqtt_manifest_end_capture_and_reconcile()"))

    def test_cleanup_is_not_a_synchronous_full_discovery_sweep(self):
        self.assertIn("mqtt_jbc_cleanup_tick();", SOURCE.split("static void mqtt_cleanup_tick() {", 1)[1])
        self.assertNotIn("mqtt_clear_jbc_usb_incompatible_discovery", SOURCE)
        self.assertRegex(SOURCE, r"static void mqtt_clear_module_entity\([^;{]+\) \{\s*char full_suffix\[96\]")


if __name__ == "__main__":
    unittest.main()
