import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MQTT = (ROOT / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(
    encoding="utf-8"
)
WEB = (ROOT / "OpenFumeExtractorMaster/src/WebStatus.inc.h").read_text(
    encoding="utf-8"
)


class WellerMqttFilterTests(unittest.TestCase):
    def test_weller_is_excluded_from_fanio_filter_telemetry(self):
        helper = re.search(
            r"static bool mqtt_module_has_fanio_filter_telemetry.*?\n}",
            MQTT,
            re.S,
        )
        self.assertIsNotNone(helper)
        self.assertIn("m.type != MODULE_WELLER_ZERO_SMOG", helper.group(0))
        self.assertGreaterEqual(
            MQTT.count("mqtt_module_has_fanio_filter_telemetry(m)"), 4
        )

    def test_fanio_discovery_follows_filter_mode(self):
        self.assertIn("static uint8_t mqtt_fanio_filter_mode", MQTT)
        self.assertIn(
            "const bool runtime_enabled = filter_mode == 1 || filter_mode == 3",
            MQTT,
        )
        self.assertIn(
            "const bool pressure_enabled = filter_mode == 2 || filter_mode == 3",
            MQTT,
        )
        self.assertIn('sig += F(":filter_mode:")', MQTT)
        self.assertIn("if (pressure_enabled)", MQTT)
        self.assertIn("if (runtime_enabled)", MQTT)

    def test_weller_uses_unified_filter_entities(self):
        for suffix in ("filter_lifetime", "filter_runtime", "filter_remaining"):
            self.assertIn(f'"sensor", "{suffix}"', MQTT)
        self.assertIn('mqtt_txt(" Filter-Wechselintervall", " Filter replacement interval")', MQTT)
        self.assertIn('mqtt_txt(" Filterbetriebszeit", " Filter operating time")', MQTT)
        self.assertIn('mqtt_txt(" Filter-Restlaufzeit", " Filter remaining time")', MQTT)

    def test_weller_remaining_time_is_clamped_at_zero(self):
        self.assertIn(
            "filter_lifetime_min > filter_runtime_min\n"
            "      ? filter_lifetime_min - filter_runtime_min\n"
            "      : 0;",
            MQTT,
        )
        self.assertIn('"filter_remaining_text\\\":\\\""', MQTT)

    def test_legacy_filter_time_discovery_is_not_published(self):
        self.assertNotIn(
            'mqtt_publish_module_entity(m, "sensor", "filter_time"', MQTT
        )

    def test_weller_web_card_uses_the_same_filter_terms(self):
        self.assertIn("function syncWellerFilterCard", WEB)
        self.assertIn("'Filterzustand':'Filter status'", WEB)
        self.assertIn("t('fan_filter_lifetime')", WEB)
        self.assertIn("t('fan_filter_runtime')", WEB)
        self.assertIn("t('fan_filter_remaining')", WEB)
        self.assertIn("Math.max(0,lifetime-runtime)", WEB)

    def test_weller_filter_controls_are_stacked_below_the_values(self):
        self.assertIn("actions.style.flexDirection='column'", WEB)
        self.assertIn("button.style.width='100%'", WEB)
        self.assertIn("button.style.order='1'", WEB)
        self.assertIn("document.querySelector('.fan-card .filter-actions button')", WEB)
        self.assertIn("button.style.fontSize=style?style.fontSize", WEB)
        self.assertIn("interval.style.order='2'", WEB)
        self.assertIn("interval.appendChild(select)", WEB)
        self.assertIn("groups[0].appendChild(swFact)", WEB)
        self.assertIn("['wfstat_','wfprog_','wfrun_','wfremain_']", WEB)
        self.assertIn("caption.style.marginBottom='10px'", WEB)


if __name__ == "__main__":
    unittest.main()
