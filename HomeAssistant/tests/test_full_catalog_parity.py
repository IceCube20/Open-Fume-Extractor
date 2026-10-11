"""Execute real firmware discovery branches against native inventory and metadata."""
import importlib.util
import json
import re
from pathlib import Path
import unittest

from jinja2 import Environment, StrictUndefined

HA = Path(__file__).resolve().parents[1]
loader = importlib.util.spec_from_file_location("full_parity_audit", HA / "tools/audit_parity.py")
audit = importlib.util.module_from_spec(loader)
loader.loader.exec_module(audit)


class FullCatalogTests(unittest.TestCase):
    def test_all_eight_fanio_channel_icons_and_explicit_overrides(self):
        cases = [case for case in audit.cases() if "eight channels" in case["id"] or "custom channel icons" in case["id"]]
        self.assertEqual(len(cases), 8)
        captured = audit.capture(cases)
        for case in cases:
            specs = audit.model.build_specs(audit.MASTER, case["state"], {audit.MODULE: case["definitions"]})
            discovery = {message["payload"]["uniq_id"]: message["payload"] for message in captured[case["id"]]}
            for prefix, start, default_icon in (("in", 19, "mdi:import"), ("out", 39, "mdi:export")):
                for number in range(1, 9):
                    entity_id = start + number
                    expected = default_icon
                    if number == 8 and "custom channel icons" in case["id"]:
                        expected = "mdi:gesture-tap-button" if prefix == "in" else "mdi:led-on"
                    with self.subTest(case=case["id"], channel=f"{prefix}{number}"):
                        self.assertEqual(specs[f"{audit.MODULE}:entity_{entity_id}_{prefix}{number}"].icon, expected)
                        self.assertEqual(discovery[f"test-master_mod_20_entity_{entity_id}"]["icon"], expected)

    def test_all_discovered_entities_have_matching_explicit_icons(self):
        for case in audit.cases():
            specs = audit.model.build_specs(audit.MASTER, case["state"], {audit.MODULE: case["definitions"]})
            for spec in specs.values():
                with self.subTest(case=case["id"], key=spec.key):
                    self.assertTrue(spec.icon and spec.icon.startswith("mdi:"))
        for case, messages in audit.capture(audit.cases()).items():
            for message in messages:
                with self.subTest(case=case, topic=message["topic"]):
                    self.assertTrue(message["payload"].get("icon", "").startswith("mdi:"))

    def test_time_counter_templates_and_raw_attributes(self):
        inputs = audit.cases()
        captures = audit.capture(inputs)
        env = Environment(undefined=StrictUndefined)
        time_count = 0
        for case in inputs:
            module = case["state"]["modules"][0]
            if not isinstance(module.get("ha_jbc"), dict):
                continue
            for message in captures[case["id"]]:
                meta = message["payload"]
                key = meta["uniq_id"].removeprefix("test-master_")[7:]
                if not re.match(r"jbc_p[1-4]_", key):
                    continue
                counter = audit.model.jbc_counter_metadata(key.split("_", 2)[2])
                if not counter or not counter[1]:
                    continue
                time_count += 1
                self.assertNotIn("unit_of_meas", meta)
                self.assertNotIn("stat_cla", meta)
                for raw in (0, 10, 60, 1440, 2450, 9600, 4294967295):
                    with self.subTest(case=case["id"], key=key, raw=raw):
                        module["ha_jbc"][key] = raw
                        spec = audit.model.build_specs(audit.MASTER, case["state"], {})[audit.MODULE + ":" + key]
                        rendered = env.from_string(meta["val_tpl"]).render(value_json={key: raw})
                        self.assertEqual(rendered, spec.value)
                        self.assertEqual(spec.attributes, {"counter_minutes": raw})
                        attributes = json.loads(env.from_string(meta["json_attr_tpl"]).render(value_json={key: raw}))
                        self.assertEqual(attributes, spec.attributes)
        self.assertGreater(time_count, 40)

    def test_complete_inventory_and_metadata_in_both_languages(self):
        results = audit.run()
        self.assertGreaterEqual(len(results), 70)
        for result in results:
            with self.subTest(case=result["case"]):
                self.assertEqual(result["native_only"], [])
                self.assertEqual(result["mqtt_only"], [])
                self.assertEqual(result["metadata_differences"], [])
                if result["case"].startswith("JBC USB four SOLD ports"):
                    self.assertLessEqual(result["mqtt_count"], 192)

    def test_each_counter_label_and_unit_matches_shared_catalogue(self):
        self.assertEqual(len(audit.COUNTERS), 73)
        for key, _, _, _, de, en, unit in audit.COUNTERS:
            with self.subTest(key=key):
                self.assertEqual(audit.model.jbc_counter_metadata(key), ((de, en), "min" if unit == '"min"' else None))
        self.assertIsNone(audit.model.jbc_counter_metadata("sold_special_counter_flags"))

    def test_actual_availability_templates_match_native_fields(self):
        inputs = audit.cases()
        captures = audit.capture(inputs)
        env = Environment(undefined=StrictUndefined)
        for case in inputs:
            master = case["wire"]["master"]
            module = case["state"]["modules"][0]
            specs = audit.model.build_specs(audit.MASTER, case["state"], {audit.MODULE: case["definitions"]})
            readings = {**module, **(module.get("ha_jbc") or {}),
                        "jbc_valid": (module.get("ha_jbc") or {}).get("valid", False),
                        "jbc_station_name": module.get("jbc_usb_station_name"),
                        "weller_link": module.get("weller_uart_age_sec", 65534) <= 10,
                        "feedback_valid": case["state"].get("output_module", {}).get("valid", False)}
            for definition in case["definitions"]:
                value = next(e for e in module["universal_entities"] if e["id"] == definition["id"])
                readings["entity_" + str(definition["id"])] = value["ascii"] if value["len"] else ""
            for message in captures[case["id"]]:
                meta = message["payload"]
                suffix = meta["uniq_id"].removeprefix("test-master_")
                if not master:
                    suffix = suffix[7:]
                key = audit.wire_key(suffix, master, module["type"])
                candidates = [s for s in specs.values() if s.uid == (audit.MASTER if master else audit.MODULE) and
                              (s.key == key or s.key.startswith(key + "_") and key.startswith("entity_"))]
                if not candidates:
                    # Dedicated filter telemetry replaces these profile keys.
                    continue
                actual = master or module["online"] or suffix == "online"
                for item in meta.get("availability", []):
                    if "value_template" in item:
                        actual = actual and env.from_string(item["value_template"]).render(value_json=readings) == "online"
                with self.subTest(case=case["id"], key=key):
                    self.assertEqual(bool(actual), bool(candidates[0].valid))

    def test_uint64_counter_is_not_rounded_to_float(self):
        state = audit.fixtures.fixture(9, "SF")
        core = state["modules"][0]["ha_jbc"]
        core["jbc_p1_sf_counter_tin_length"] = "9223372036854775809"
        specs = audit.model.build_specs(audit.MASTER, state, {})
        self.assertEqual(specs[audit.MODULE + ":jbc_p1_sf_counter_tin_length"].value, 9223372036854775809)
