"""MQTT and native alarms share a visible text sensor and lossless list."""
import importlib.util
import json
from pathlib import Path
import re
import sys
import unittest
from jinja2 import Environment

ROOT = Path(__file__).resolve().parents[2]
loader = importlib.util.spec_from_file_location("alarm_model", ROOT / "HomeAssistant/custom_components/open_fume_extractor/model.py")
model = importlib.util.module_from_spec(loader)
sys.modules[loader.name] = model
loader.loader.exec_module(model)
MQTT = (ROOT / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(encoding="utf-8")
MASTER = "0000000012345678"


class AlarmListsTest(unittest.TestCase):
    def test_native_matches_actual_mqtt_attribute_template(self):
        literal = next(json.loads(raw) for raw in re.findall(r'F\(("(?:[^"\\]|\\.)*")\)', MQTT)
                       if "json_attr_tpl" in raw and "'alarms'" in raw)
        template = json.loads('{"json_attr_t":"state' + literal + '}')["json_attr_tpl"]
        for alarms in ([], ["0x10 JBC FAE Bus DDE: JBC-Station getrennt"],
                       ["0x10 Alias; mit Trennzeichen: Fehler", "0x20 Lüfter: " + "Warnung " * 100]):
            with self.subTest(count=len(alarms)):
                state = {"alarms": alarms, "alarm_count": len(alarms), "alarm_text": "; ".join(alarms)}
                specs = model.build_specs(MASTER, state, {})
                native = specs[MASTER + ":alarm_count"]
                mqtt = json.loads(Environment().from_string(template).render(value_json=state))
                self.assertEqual(native.attributes, mqtt)
                self.assertEqual(native.attributes["alarms"], alarms)
                self.assertEqual(native.value, len(alarms))
                text = specs[MASTER + ":alarm_text"]
                self.assertEqual(text.attributes, mqtt)
                self.assertEqual(text.value, ("; ".join(alarms) or "OK")[:255])
                self.assertEqual(text.icon, "mdi:bell-alert-outline")
                self.assertFalse(text.diagnostic)

    def test_native_invalid_or_missing_list_is_empty(self):
        for alarms in (None, "not a list", {}, [None, 12, "", "Valid alarm"]):
            spec = model.build_specs(MASTER, {"alarms": alarms}, {})[MASTER + ":alarm_count"]
            self.assertEqual(spec.attributes["alarms"], ["Valid alarm"] if isinstance(alarms, list) else [])

    def test_mqtt_text_template_matches_native_and_legacy_payload(self):
        match = re.search(r'mqtt_publish_discovery_entity\("sensor", "alarm_text", mqtt_txt\("Alarm Text", "Alarm Text"\), ("(?:[^"\\]|\\.)*")\);', MQTT)
        self.assertIsNotNone(match)
        template = Environment().from_string(json.loads(match[1]))
        for alarms in ([], ["0x10 DDE: Station getrennt"], ["Lüfter: " + "Warnung " * 100, "0x20 Drucksensor: Fehler"]):
            state = {"alarms": alarms, "alarm_count": len(alarms)}
            native = model.build_specs(MASTER, state, {})[MASTER + ":alarm_text"]
            self.assertEqual(template.render(value_json=state), native.value)
        self.assertIn('!strcmp(suffix, "alarm_count") || !strcmp(suffix, "alarm_text")', MQTT)
        self.assertNotIn('mqtt_discovery_topic("sensor", master_device_id, "alarm_text").c_str(), "", true)', MQTT)
        for state in ({"alarm_text": "Legacy alarm"}, {"alarm_text": "", "alarms": ["Current alarm"]},
                      {"alarm_text": None, "alarms": []}, {}):
            expected = (state.get("alarm_text") or "; ".join(state.get("alarms", [])) or "OK")[:255]
            self.assertEqual(template.render(value_json=state), expected)
        publish = MQTT.split("static void mqtt_publish_state(", 1)[1].split("static void mqtt_loop(", 1)[0]
        self.assertIn('state += ",\\\"alarm_text\\\":\\\""; state += mqtt_alarm_text_json(alarms.text);', publish)
        self.assertIn("alarms.strings_json.c_str(),", publish)
        self.assertLess(publish.index("mqtt_alarm_text_json(alarms.text)"), publish.index('mqtt_publish_measured(mqtt_topic_path("state")'))

    def test_native_legacy_master_text_fallback(self):
        spec = model.build_specs(MASTER, {"alarm_text": "Legacy alarm"}, {})[MASTER + ":alarm_text"]
        self.assertEqual(spec.value, "Legacy alarm")
        self.assertEqual(spec.attributes["alarms"], [])


if __name__ == "__main__":
    unittest.main()
