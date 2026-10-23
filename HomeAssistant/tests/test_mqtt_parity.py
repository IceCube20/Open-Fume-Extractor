"""Compare HA captions/platforms against the actual firmware discovery calls."""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("parity_model", ROOT / "HomeAssistant/custom_components/open_fume_extractor/model.py")
model = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = model
spec.loader.exec_module(model)
MQTT = (ROOT / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(encoding="utf-8")
MASTER, MODULE = "0000000012345678", "2000000012345678"


def fixture(typ=3, family="DDE"):
    port = {"valid": True, "temperature": 900, "selected_temp": 900, "power_permille": 400}
    core = {"valid": True, "jbc_link": True, "jbc_model": family, "jbc_family": model.jbc_family(family),
            "jbc_port_count": 1, "jbc_station_error_text": "OK", "jbc_connect_mode": "MONITOR",
            "jbc_p1_valid": True, "jbc_p1_state": "WORK", "jbc_p1_temp_c": 350,
            "jbc_p1_set_temp_c": 370, "jbc_p1_power_permille": 550, "jbc_p1_tool_name": "T245",
            "jbc_p1_error_text": "OK", "jbc_p1_future_mode": "HIBERNATION", "jbc_p1_transition_countdown_s": 125,
            "jbc_p1_flow_permille": 550, "jbc_p1_time_to_stop_ds": 1251,
            "jbc_cleaner_mode": "DETECTION", "jbc_motors": True, "jbc_door_open": False,
            "jbc_heater": True, "jbc_selected_power_permille": 650, "jbc_active_zones": 2, "jbc_time_to_stop_ds": 60,
            "jbc_suction_level": "HIGH", "jbc_continuous": True, "jbc_intake_work": True, "jbc_intake_stand": False,
            "jbc_feeding": True, "jbc_program": 1, "jbc_speed_tenth_mm_s": 125, "jbc_length_tenth_mm": 45,
            "jbc_tool_enabled": True, **{f"jbc_tc{n}_temp_c": 100 for n in range(1, 5)}}
    module = {"uid": MODULE, "type": typ, "addr": 32, "online": True, "telemetry_valid": True, "caps": 4 | 8 | 32,
              "fw": "1.0RC", "transport": "rs485", "station_type": "DDE", "station_addr": 0x12, "jbc_link_flags": 1,
              "display_backlight_supported": typ == 6, "display_backlight_mode": 1,
              "display_backlight_timeout_min": 5, "display_backlight_on": True,
              "jbc_device_id": "41424300", "jbc_usb_model": family, "jbc_usb_port_count": 1,
              "jbc_usb_ports": [port], "ha_jbc": core if typ == 9 else None,
              "fanio_filter_mode": 3, "fanio_filter_runtime_valid": True,
              "fanio_filter_runtime_minutes": 2450, "fanio_filter_lifetime_minutes": 4320,
              "weller_uart_age_sec": 0, "weller_filter_status": 1, "weller_filter_runtime_minutes": 2450,
              "weller_programmed_filter_minutes": 4320}
    return {"developer_mode": True, "modules": [module], "modules_count": 1, "alarm_text": "OK", "uptime_ms": 10000, "heap_free": 179932,
            "heap_min": 100000, "loop_max_ms": 25, "work_mask": 0,
            "output_module": {"valid": True, "rpm": 1200, "power": 600, "fault_text": "OK"}, "master_fw": "2.0.35RC",
            "master_ip": "192.168.1.2", "datetime": "2026-10-09", "mqtt_state": 0, "mqtt_state_text": "connected",
            "jbc": {"suction_level": 0, "stat_error": 0, "stat_error_text": "OK"},
            "status_led_enabled": True, "status_led_brightness": 80,
            "ha_routes": {"main_input": {"value": "No input", "options": ["No input"]},
                          "main_output": {"value": "Auto", "options": ["Auto"]}}}


class ParityTests(unittest.TestCase):
    def test_actual_fae_discovery_inventory_categories_icons_and_disconnection(self):
        node = shutil.which("node")
        bundled = Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe"
        if not node and bundled.exists():
            node = str(bundled)
        if not node:
            self.skipTest("Node.js is needed to execute the firmware publisher")
        result = subprocess.run([node, str(Path(__file__).with_name("capture_mqtt_fae.cjs"))],
                                capture_output=True, text=True, check=True, timeout=30)
        mapping = {"firmware": "fw", "cpu": "module_cpu_load_pct", "heap": "module_heap_free",
                   "uptime": "module_uptime_s", "heap_min": "module_heap_min", "loop_max": "module_loop_max_ms",
                   "work": "jbc_work_mask", "stand": "jbc_stand_mask", "station": "station_type", "device_id": "jbc_device_id"}
        for captured in json.loads(result.stdout):
            state = {**fixture(1), "ui_lang": captured["lang"]}
            module = state["modules"][0]
            module.update(addr=16, caps=1, eco_mode=True)
            specs = model.build_specs(MASTER, state, {})
            native = {s.key: s for s in specs.values() if s.uid == MODULE}
            mqtt = {entry["payload"]["uniq_id"].removeprefix("test-master_mod_10_"): entry
                    for entry in captured["discovery"]}
            self.assertEqual(set(native), {mapping.get(key, key) for key in mqtt})
            for suffix, entry in mqtt.items():
                actual, meta = native[mapping.get(suffix, suffix)], entry["payload"]
                with self.subTest(lang=captured["lang"], suffix=suffix):
                    self.assertEqual(meta["name"], actual.name)
                    self.assertEqual(entry["topic"].split("/")[1], actual.platform)
                    self.assertEqual(meta.get("ent_cat") == "diagnostic", actual.diagnostic)
                    for mqtt_key, native_key in (("icon", "icon"), ("dev_cla", "device_class"),
                                                 ("stat_cla", "state_class"), ("unit_of_meas", "unit")):
                        self.assertEqual(meta.get(mqtt_key), getattr(actual, native_key))
            self.assertEqual(mqtt["reboot"]["payload"]["pl_prs"], "RESTART")
            self.assertEqual(mqtt["reboot"]["payload"]["cmd_t"], "ofe/cmd/module/10/reboot")
            self.assertNotIn("stat_t", mqtt["reboot"]["payload"])
            for case in captured["states"]:
                module.update(station_addr=case["address"], jbc_link_flags=int(case["connected"]),
                              jbc_work_mask=case["work"], jbc_device_id="84495820260912143910F283".encode().hex() if case["cached"] else "")
                specs = model.build_specs(MASTER, state, {})
                for key, field in (("jbc_link", "jbc_link"), ("jbc_work_mask", "work_active"),
                                   ("jbc_stand_mask", "stand_active"), ("station_type", "station"),
                                   ("jbc_device_id", "device_id")):
                    self.assertTrue(specs[MODULE + ":" + key].valid)
                    self.assertEqual(specs[MODULE + ":" + key].value, case["state"][field])
            module["online"] = False
            specs = model.build_specs(MASTER, state, {})
            for key in ("jbc_link", "jbc_work_mask", "jbc_stand_mask", "station_type", "jbc_device_id"):
                self.assertFalse(specs[MODULE + ":" + key].valid)

    def test_real_api_exports_profile_icons(self):
        source = (ROOT / "OpenFumeExtractorMaster/src/WebStatus.inc.h").read_text(encoding="utf-8")
        function = source.split("static void append_universal_entity_defs_json", 1)[1].split("\n}", 1)[0]
        self.assertIn('"icon"', function)
        definition = {"id": 20, "key": "valve", "type": "switch", "mode": "rw",
                      "label": "Valve", "meta": {"icon": "mdi:valve"}}
        self.assertEqual(model.profile_spec(MODULE, definition, {}, True).icon, "mdi:valve")

    def test_literal_module_discovery_metadata_matches_native(self):
        all_specs = {}
        for typ, family in ((1, "DDE"), (3, "DDE"), (5, "DDE"), (6, "DDE"),
                            (9, "DDE"), (9, "CLMU"), (9, "PHBE"), (9, "F2"), (9, "SF")):
            all_specs.update(model.build_specs(MASTER, {**fixture(typ, family), "ui_lang": "en"}, {}))
        checked = 0
        for line in MQTT.splitlines():
            call = re.search(r'mqtt_publish_module_entity\(m, "([^"]+)", "([^"]+)", base \+ String\(mqtt_txt\("([^"]*)", "([^"]*)"\)\)', line)
            if not call:
                continue
            tokens = re.findall(r'"(?:\\.|[^"\\])*"', line)
            extra = next((json.loads(token) for token in tokens if token.startswith('\",\\\"')), None)
            if extra is None:
                continue
            metadata = json.loads("{" + extra.lstrip(",") + "}")
            platform, suffix, _, name = call.groups()
            candidates = [s for s in all_specs.values() if s.uid == MODULE and s.name == name.strip() and s.platform == platform]
            self.assertTrue(candidates, suffix)
            for actual in candidates:
                with self.subTest(suffix=suffix):
                    for mqtt_key, native_key in (("icon", "icon"), ("dev_cla", "device_class"),
                                                 ("stat_cla", "state_class"), ("unit_of_meas", "unit")):
                        if mqtt_key in metadata:
                            self.assertEqual(getattr(actual, native_key), metadata[mqtt_key])
            checked += 1
        self.assertGreater(checked, 25)

    def test_dynamic_jbc_port_captions_icons_and_units(self):
        mapping = {"temp_suffix": "temperature", "set_suffix": "selected_temperature", "power_suffix": "power",
                   "future_suffix": "future_mode", "countdown_suffix": "countdown", "flow_suffix": "flow",
                   "stop_suffix": "time_to_stop", "error_suffix": "error", "tool_suffix": "tool", "state_suffix": "state",
                   "selected_flow": "selected_flow"}
        calls = re.findall(r'mqtt_publish_module_entity\(m, "sensor", (\w+)\.c_str\(\), mqtt_jbc_port_name\(base, i, "([^"]+)", "([^"]+)"\)([^\n]*)', MQTT)
        self.assertGreaterEqual(len(calls), 10)
        for lang in ("de", "en"):
            for family in ("DDE", "JTSE"):
                specs = model.build_specs(MASTER, {**fixture(9, family), "ui_lang": lang}, {})
                for variable, de, en, tail in calls:
                    key = MODULE + ":jbc_p1_" + mapping[variable]
                    if key not in specs:
                        continue
                    actual = specs[key]
                    self.assertEqual(actual.name, "Port 1 " + (de if lang == "de" else en))
                    icon = re.search(r'mdi:[a-z0-9-]+', tail)
                    if icon:
                        self.assertEqual(actual.icon, icon.group())
                    if r'unit_of_meas\":\"%' in tail:
                        self.assertEqual(actual.unit, "%")

    def test_shared_diagnostics_are_not_controls_or_operational_sensors(self):
        specs = model.build_specs(MASTER, fixture(), {})
        for key in ("master_fw", "serial_number", "heap_free", "cpu_load_pct", "wifi_rssi", "master_ip", "mqtt_state_text"):
            self.assertTrue(specs[MASTER + ":" + key].diagnostic, key)
        for key in ("fw", "serial_number", "module_heap_free", "module_cpu_load_pct", "module_uptime_s", "online"):
            self.assertTrue(specs[MODULE + ":" + key].diagnostic, key)
        for key in ("status_led_enabled", "main_input", "main_output", "modules_count", "alarm_count"):
            self.assertFalse(specs[MASTER + ":" + key].diagnostic, key)

    def test_disabled_filter_has_no_descriptor_mode_entity(self):
        state = fixture()
        state["modules"][0]["fanio_filter_mode"] = 0
        definition = {"id": 67, "key": "filter_mode", "type": "number", "mode": "ro", "label": "Filter mode", "meta": {}}
        specs = model.build_specs(MASTER, state, {MODULE: [definition]})
        self.assertNotIn(MODULE + ":entity_67_filter_mode", specs)
        self.assertNotIn(MODULE + ":filter_mode", specs)

    def test_all_literal_master_discovery_captions_and_platforms(self):
        mapping = {"output": "output_enabled", "power": "output_power", "modules": "modules_count",
                   "jbc_error": "stat_error", "jbc_error_text": "stat_error_text", "ip": "master_ip",
                   "mqtt_state": "mqtt_state_text", "heap": "heap_free", "cpu": "cpu_load_pct", "firmware": "master_fw",
                   "afterrun_stand": "delay_stand", "afterrun_work": "delay_work", "selected_flow": "custom_power",
                   "continuous_suction": "continuous", "suction_level": "level", "uptime": "uptime_ms",
                   "loop_max": "loop_max_ms", "eco": "power_save_active"}
        calls = re.findall(r'mqtt_publish_discovery_(?:entity|control)\("([^"]+)", "([^"]+)", (mqtt_txt\("[^"]*", "[^"]*"\)|"Firmware")', MQTT)
        self.assertGreater(len(calls), 25)
        for lang in ("de", "en"):
            specs = model.build_specs(MASTER, {**fixture(), "ui_lang": lang}, {})
            for platform, suffix, name in calls:
                labels = re.findall(r'"([^"]*)"', name)
                expected = labels[0 if lang == "de" or len(labels) == 1 else 1]
                with self.subTest(lang=lang, suffix=suffix):
                    actual = specs[MASTER + ":" + mapping.get(suffix, suffix)]
                    self.assertEqual(actual.platform, platform)
                    self.assertEqual(actual.name, expected)

    def test_all_literal_module_discovery_captions_and_platforms(self):
        mapping = {"firmware": "fw", "cpu": "module_cpu_load_pct", "heap": "module_heap_free", "uptime": "module_uptime_s",
                   "heap_min": "module_heap_min", "loop_max": "module_loop_max_ms",
                   "work": "jbc_work_mask", "stand": "jbc_stand_mask", "station": "station_type", "device_id": "jbc_device_id",
                   "jbc_model": "jbc_usb_model", "jbc_station_error": "jbc_usb_station_error", "jbc_station_name": "jbc_usb_station_name",
                   "speed": "weller_speed", "fan": "weller_fan", "light": "weller_light", "filter": "weller_filter",
                   "filter_runtime": "filter_operating_time", "power": "main_power", "brightness": "brightness", "language": "display_language",
                   "theme": "display_theme", "screensaver": "display_screensaver_min",
                   "jbc_cleaner_mode": "jbc_p1_cleaning_mode", "jbc_motors": "jbc_p1_cl_motors_on", "jbc_door": "jbc_p1_cl_door_open",
                   "jbc_heater": "jbc_p1_heater", "jbc_selected_power": "jbc_p1_ph_selected_power", "jbc_active_zones": "jbc_p1_ph_active_zones",
                   "jbc_suction_level": "jbc_p1_suction_level", "jbc_intake_work": "jbc_p1_fe_intake_work", "jbc_intake_stand": "jbc_p1_fe_intake_stand",
                   "jbc_feeding": "jbc_p1_sf_feeding_state", "jbc_program": "jbc_p1_sf_feeding_selected_program",
                   "jbc_speed": "jbc_p1_sf_speed_tenth_mm_s", "jbc_length": "jbc_p1_sf_length_tenth_mm", "jbc_tool_enabled": "jbc_p1_sf_tool_enabled"}
        calls = re.findall(r'mqtt_publish_module_entity\(m, "([^"]+)", "([^"]+)", base \+ String\(mqtt_txt\("([^"]*)", "([^"]*)"\)\)', MQTT)
        self.assertGreater(len(calls), 40)
        for lang in ("de", "en"):
            all_specs = {}
            for typ, family in ((1, "DDE"), (3, "DDE"), (5, "DDE"), (6, "DDE"),
                                (9, "DDE"), (9, "CLMU"), (9, "PHBE"), (9, "F2"), (9, "SF")):
                all_specs.update(model.build_specs(MASTER, {**fixture(typ, family), "ui_lang": lang}, {}))
            for platform, suffix, de, en in calls:
                with self.subTest(lang=lang, suffix=suffix):
                    actual = all_specs[MODULE + ":" + mapping.get(suffix, suffix)]
                    self.assertEqual(actual.platform, platform)
                    self.assertEqual(actual.name, (de if lang == "de" else en).strip())

    def test_shared_jbc_projection_overrides_conflicting_raw_cache(self):
        state = fixture(9)
        specs = model.build_specs(MASTER, state, {})
        for key, value in (("temperature", 350), ("selected_temperature", 370), ("power", 55),
                           ("countdown", "2 min 05 s"), ("future_mode", "HIBERNATION")):
            self.assertEqual(specs[MODULE + ":jbc_p1_" + key].value, value)
        state["modules"][0]["ha_jbc"]["jbc_p1_set_temp_c"] = -1
        self.assertFalse(model.build_specs(MASTER, state, {})[MODULE + ":jbc_p1_selected_temperature"].valid)

    def test_canonical_class_change_removes_unsupported_entities(self):
        for family in ("JTSE", "CLMU", "PHBE", "F2", "SF"):
            specs = model.build_specs(MASTER, fixture(9, family), {})
            self.assertNotIn(MODULE + ":jbc_p1_future_mode", specs)
            if family != "JTSE":
                self.assertNotIn(MODULE + ":jbc_p1_temperature", specs)

    def test_new_controls_keep_wire_values_language_independent(self):
        state = {**fixture(), "ui_lang": "de", "jbc": {"suction_level": 0}}
        specs = model.build_specs(MASTER, state, {})
        self.assertEqual(specs[MASTER + ":level"].value, "Hoch")
        self.assertEqual(specs[MASTER + ":level"].option_values, ("0", "1", "2", "3"))
        self.assertEqual(specs[MASTER + ":status_led_brightness"].params, {"setting": "brightness"})
        self.assertEqual(specs[MASTER + ":main_output"].option_values, ("Auto",))

    def test_duration_and_heap_values_match_mqtt_not_just_display_precision(self):
        specs = model.build_specs(MASTER, {**fixture(), "afterrun_ms": 90000}, {})
        self.assertEqual(specs[MASTER + ":afterrun"].value, "2m")
        self.assertIsNone(specs[MASTER + ":afterrun"].unit)
        self.assertEqual(specs[MASTER + ":heap_free"].value, 176)
        self.assertEqual(model.filter_time(4320), "3d")
        self.assertEqual(model.filter_time(0), "0m")

    def test_writable_text_is_a_control_not_an_unwritable_sensor(self):
        definition = {"id": 20, "key": "name", "type": "text", "mode": "rw", "label": "Alias", "meta": {}}
        actual = model.profile_spec(MODULE, definition, {"ascii": "Hello", "len": 5}, True)
        self.assertEqual(actual.platform, "text")
        self.assertEqual(actual.action, "entity")
        definition["mode"] = "ro"
        self.assertEqual(model.profile_spec(MODULE, definition, {"ascii": "Hello", "len": 5}, True).platform, "sensor")

    def test_profile_aliases_are_never_retranslated(self):
        definition = {"id": 20, "key": "valve", "type": "switch", "mode": "rw", "label": "My valve", "meta": {}}
        for lang in ("de", "en"):
            self.assertEqual(model.build_specs(MASTER, {**fixture(), "ui_lang": lang}, {MODULE: [definition]})[MODULE + ":entity_20_valve"].name, "My valve")

    def test_continuous_control_is_the_setting_not_aggregated_activity(self):
        specs = model.build_specs(MASTER, {**fixture(), "continuous": True, "jbc": {"continuous": False}}, {})
        self.assertFalse(specs[MASTER + ":continuous"].value)


if __name__ == "__main__":
    unittest.main()
