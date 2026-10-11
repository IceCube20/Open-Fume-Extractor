"""Discovery contracts: short captions without changing retained identities."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
MQTT = (ROOT / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"static (?:void|String) " + name + r"\(.*?\n}", MQTT, re.S)
    assert match is not None, name
    return match.group(0)


class MqttPresentationTests(unittest.TestCase):
    def test_module_entity_captions_leave_address_on_device(self):
        publisher = function("mqtt_publish_module_entity")
        self.assertNotIn('address_prefix', publisher)
        self.assertIn('entity_name.trim()', publisher)
        self.assertIn('json_escape(entity_name.c_str())', publisher)
        self.assertIn('const String base = ""', function("mqtt_publish_module_discovery"))
        self.assertNotIn('module_display_name(m)', function("mqtt_publish_module_discovery"))

    def test_module_alias_stays_in_device_name(self):
        self.assertIn('mqttName += module_display_name(m)', function("mqtt_module_device_json"))

    def test_entity_identity_and_topics_stay_unchanged(self):
        publisher = function("mqtt_publish_module_entity")
        self.assertIn('"mod_%02X_%s", m.addr, suffix', publisher)
        self.assertIn('object += full_suffix', publisher)
        self.assertIn('mqtt_module_topic(m.addr, "state")', publisher)
        self.assertIn('mqtt_module_topic(m.addr, "status")', publisher)
        self.assertIn('mqtt_module_device_json(m)', publisher)
        self.assertIn('mqtt_discovery_topic(component, master_device_id, full_suffix)', publisher)

    def test_stand_is_parking_for_both_jbc_transports(self):
        lines = [line for line in MQTT.splitlines()
                 if 'mqtt_publish_module_entity(m, "binary_sensor", "stand"' in line]
        self.assertEqual(len(lines), 2)
        self.assertTrue(all('mdi:car-brake-parking' in line for line in lines))

    def test_serial_sensor_is_published_for_master_and_all_module_types(self):
        self.assertIn('"sensor", "serial_number"', function("mqtt_publish_discovery"))
        self.assertIn('"sensor", "serial_number"', function("mqtt_publish_module_discovery"))
        self.assertIn('uid_hex(ESP.getEfuseMac())', function("mqtt_device_json"))
        self.assertIn('uid_hex(m.uid)', function("mqtt_module_device_json"))
        self.assertIn('uid_hex(m.uid)', function("mqtt_publish_module_state"))
        master_state = function("mqtt_publish_state")
        self.assertIn('master_uid.c_str(),\n           MASTER_FW_VERSION', master_state)

    def test_fae_stand_matches_web_connected_idle_semantics(self):
        state = function("mqtt_publish_module_state")
        self.assertIn('const bool linked = m.station_addr && (m.jbc_link_flags & FAST_FLAG_CONNECTED)', state)
        self.assertIn('(linked && !m.jbc_work_mask) ? "true" : "false"', state)
        self.assertIn('(linked && m.jbc_work_mask) ? "true" : "false"', state)


if __name__ == "__main__":
    unittest.main()
