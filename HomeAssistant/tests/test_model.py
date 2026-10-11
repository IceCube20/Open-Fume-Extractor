"""Executable mapping tests, independent of a Home Assistant installation."""
import importlib.util
from pathlib import Path
import sys
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ofe_model_test", ROOT / "custom_components/open_fume_extractor/model.py")
model = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = model
SPEC.loader.exec_module(model)
MASTER = "0000000012345678"
MODULE = "2000000012345678"


def definition(typ="number", mode="rw", **meta):
    return {"id": 20, "key": "power", "type": typ, "mode": mode, "label": "Power", "meta": meta}


class ModelTests(unittest.TestCase):
    def test_led_sensors_use_reported_events_and_web_styles(self):
        specs = model.build_specs(MASTER, {"ui_lang": "de", "master_led_ofe_event": 1,
            "master_led_evt_event": 10, "modules": [{"uid": MODULE, "online": True,
            "led_status_valid": True, "led_ofe_event": 1, "led_evt_event": 6}]}, {})
        self.assertEqual(specs[MASTER + ":led_evt"].value, "Nachlauf")
        for uid in (MASTER, MODULE):
            led = specs[uid + ":led_ofe"]
            self.assertTrue(led.valid)
            self.assertEqual(led.attributes["ofe_led_color"], "#00ff00")
            self.assertEqual(led.attributes["ofe_led_effect"], "greenwhite")
            self.assertEqual(led.attributes["ofe_led_period_ms"], 3200)
            self.assertEqual(led.icon, "mdi:led-on")
            self.assertIsNone(led.state_class)
        self.assertEqual(specs[MODULE + ":led_evt"].attributes["ofe_led_effect"], "whitebreath")

    def test_led_eco_changes_only_ofe_and_disabled_leds_are_off(self):
        state = {"master_led_ofe_event": 1, "master_led_evt_event": 12, "power_save_active": True,
                 "modules": [{"uid": MODULE, "online": True, "led_status_valid": True,
                              "led_ofe_event": 2, "led_evt_event": 8, "eco_mode": True}]}
        specs = model.build_specs(MASTER, state, {})
        for uid in (MASTER, MODULE):
            self.assertEqual(specs[uid + ":led_ofe"].value, "Power saving")
            self.assertEqual(specs[uid + ":led_ofe"].attributes["ofe_led_effect"], "purplewhite")
            self.assertFalse(specs[uid + ":led_evt"].attributes["ofe_led_eco"])
        state["status_led_enabled"] = False
        specs = model.build_specs(MASTER, state, {})
        for uid in (MASTER, MODULE):
            for channel in ("ofe", "evt"):
                led = specs[uid + ":led_" + channel]
                self.assertTrue(led.valid)
                self.assertEqual(led.value, "Off")
                self.assertEqual(led.attributes["ofe_led_effect"], "off")
                self.assertFalse(led.attributes["ofe_led_enabled"])

    def test_leds_missing_invalid_or_offline_are_not_invented(self):
        self.assertNotIn(MASTER + ":led_ofe", model.build_specs(MASTER, {}, {}))
        for event in (-1, 14, None, True, "1"):
            with self.subTest(event=event):
                led = model.build_specs(MASTER, {"master_led_ofe_event": event}, {})[MASTER + ":led_ofe"]
                self.assertFalse(led.valid)
                self.assertIsNone(led.attributes["ofe_led_event"])
        for online, valid in ((False, True), (True, False)):
            specs = model.build_specs(MASTER, {"modules": [{"uid": MODULE, "online": online,
                "led_status_valid": valid, "led_ofe_event": 1}]}, {})
            self.assertFalse(specs[MODULE + ":led_ofe"].valid)

    def test_all_led_events_are_read_only_with_valid_styles(self):
        for event in range(14):
            spec = model.build_specs(MASTER, {"master_led_evt_event": event}, {})[MASTER + ":led_evt"]
            self.assertTrue(spec.valid)
            self.assertEqual(spec.platform, "sensor")
            self.assertIsNone(spec.action)
            self.assertRegex(spec.attributes["ofe_led_color"], r"^#[0-9a-f]{6}$")
            self.assertEqual(spec.attributes["ofe_led_event"], event)

    def test_led_styles_match_master_web_event_mapping(self):
        source = (ROOT.parent / "OpenFumeExtractorMaster/src/WebStatus.inc.h").read_text(encoding="utf-8-sig")
        styles = re.findall(r"case (\d+):return \{c:'(#[0-9a-f]+)',kind:'([^']+)',(period|step):(\d+)", source)
        self.assertEqual(len(styles), 13)
        for event, color, effect, timing, milliseconds in styles:
            led = model.build_specs(MASTER, {"master_led_evt_event": int(event)}, {})[MASTER + ":led_evt"]
            self.assertEqual(led.attributes["ofe_led_color"], color)
            self.assertEqual(led.attributes["ofe_led_effect"], effect)
            self.assertEqual(led.attributes["ofe_led_period_ms"], int(milliseconds) * (2 if timing == "step" else 1))

    def test_feedback_uses_integer_percent_and_zero_display_precision(self):
        for raw, expected in ((0, 0), (554, 55), (555, 56), (556, 56), (1000, 100)):
            spec = model.build_specs(MASTER, {"output_module": {"valid": True, "power": raw}}, {})[MASTER + ":feedback_power"]
            self.assertEqual(spec.value, expected)
            self.assertIsInstance(spec.value, int)
            self.assertEqual(spec.precision, 0)
            self.assertEqual(spec.icon, "mdi:fan")

    def test_fanio_runtime_reset_only_for_runtime_modes(self):
        for typ in (2, 3):
            for mode in (0, 1, 2, 3):
                for online in (False, True):
                    specs = self.module_specs({"type": typ, "fanio_filter_mode": mode, "online": online})
                    allowed = typ == 3 and mode in (1, 3)
                    self.assertEqual(MODULE + ":reset_filter" in specs, allowed)
                    if allowed:
                        reset = specs[MODULE + ":reset_filter"]
                        self.assertEqual(reset.valid, online)
                        self.assertEqual(reset.action, "fanio_reset_filter")
                        self.assertEqual(reset.icon, "mdi:air-filter")

    def test_duration_does_not_round_large_integer_minutes(self):
        self.assertEqual(model.filter_time("9223372036854775809"), "6405119470038038d 18h 9m")
        for unknown in (None, "invalid", "nan", -1):
            self.assertIsNone(model.filter_time(unknown))

    def test_developer_diagnostics_omitted_but_cpu_and_uptime_remain(self):
        for enabled in (False, None, True):
            state = {"heap_free": 102400, "heap_min": 102400, "loop_max_ms": 10,
                     "cpu_load_pct": 30, "modules": [{"uid": MODULE, "online": True,
                     "telemetry_valid": True, "module_heap_free": 204800,
                     "module_heap_min": 102400, "module_loop_max_ms": 20}]}
            if enabled is not None:
                state["developer_mode"] = enabled
            specs = model.build_specs(MASTER, state, {})
            for uid, keys in ((MASTER, ("heap_free", "heap_min", "loop_max_ms")),
                              (MODULE, ("module_heap_free", "module_heap_min", "module_loop_max_ms"))):
                for key in keys:
                    self.assertEqual(uid + ":" + key in specs, enabled is True)
            self.assertIn(MASTER + ":cpu_load_pct", specs)
            self.assertIn(MODULE + ":module_uptime_s", specs)

    def test_jbc_control_projection_is_authoritative_and_preserves_write_ids(self):
        controls = [{"id": 16, "platform": "number", "name": "Port 2 Set temperature",
                     "unit": "\u00b0C", "min": 90, "max": 450, "step": 1, "icon": "mdi:thermometer"},
                    {"id": 18, "platform": "switch", "name": "Port 2 Temperature levels", "icon": "mdi:thermometer-check"},
                    {"id": 19, "platform": "select", "name": "Port 2 Selected level", "icon": "mdi:format-list-numbered"}]
        core = {"valid": True, "jbc_link": True, "jbc_family": "SOLD", "controls": controls,
                "jbc_cfg_16": 350, "jbc_cfg_18": 0, "jbc_cfg_19": 2}
        specs = self.module_specs({"type": 9, "ha_jbc": core})
        temp = specs[MODULE + ":jbc_cfg_16"]
        self.assertEqual((temp.action, temp.params, temp.group, temp.value),
                         ("jbc_usb", {"control": 16}, "port_2", 350))
        self.assertEqual((temp.minimum, temp.maximum, temp.step, temp.number_mode), (90, 450, 1, "box"))
        self.assertFalse(specs[MODULE + ":jbc_cfg_18"].value)
        level = specs[MODULE + ":jbc_cfg_19"]
        self.assertEqual((level.value, level.options, level.option_values),
                         ("Level 3", ("Level 1", "Level 2", "Level 3"), ("0", "1", "2")))
        self.assertNotIn(MODULE + ":jbc_cfg_17", specs)
        for changes in ({"jbc_link": False}, {"valid": False}, {"jbc_cfg_19": -1}, {"jbc_cfg_19": 3}):
            specs = self.module_specs({"type": 9, "ha_jbc": {**core, **changes}})
            self.assertFalse(specs[MODULE + ":jbc_cfg_19"].valid)

    def test_uptime_matches_mqtt_rounding_and_omits_zero_components(self):
        for seconds, text in ((0, "0s"), (59, "59s"), (60, "1m"), (89, "1m"),
                              (90, "2m"), (3599, "1h"), (3600, "1h"),
                              (3660, "1h 1m"), (86400, "1d"),
                              (90060, "1d 1h 1m"), (49 * 86400, "49d")):
            with self.subTest(seconds=seconds):
                self.assertEqual(model.uptime_text(seconds), text)
                self.assertEqual(model.uptime_text(seconds * 1000 + 999, 1000), text)

    def test_invalid_uptime_is_unknown(self):
        for value in (None, "bad", "nan", "inf", -1):
            self.assertIsNone(model.uptime_text(value))

    def test_master_and_module_uptime_keep_identity_but_use_text_states(self):
        state = {"uptime_ms": 90060000, "modules": [{"uid": MODULE, "online": True,
                 "telemetry_valid": True, "module_uptime_s": 90060}]}
        specs = model.build_specs(MASTER, state, {})
        for key in (MASTER + ":uptime_ms", MODULE + ":module_uptime_s"):
            self.assertEqual(specs[key].value, "1d 1h 1m")
            self.assertEqual(specs[key].platform, "sensor")
            self.assertIsNone(specs[key].unit)
            self.assertIsNone(specs[key].device_class)
            self.assertIsNone(specs[key].state_class)
        state["modules"][0]["online"] = False
        self.assertFalse(model.build_specs(MASTER, state, {})[MODULE + ":module_uptime_s"].valid)

    def test_fae_and_usb_use_same_stand_icon(self):
        for typ in (1, 9):
            specs = self.module_specs({"type": typ, "jbc_link_flags": 1, "station_addr": 0x12})
            self.assertEqual(specs[MODULE + ":jbc_stand_mask"].icon, "mdi:car-brake-parking")
            self.assertEqual(specs[MODULE + ":jbc_work_mask"].icon, "mdi:soldering-iron")

    def test_numeric_is_not_rescaled(self):
        spec = model.profile_spec(MODULE, definition(scale="10"), {"ascii": "55", "len": 4}, True)
        self.assertEqual(spec.value, 55)

    def test_formatted_filter_time_is_authoritative(self):
        spec = model.profile_spec(MODULE, definition("sensor", "ro", tb="m", tf="dhm", unit="min"),
                                  {"ascii": "2450", "display_text": "1d 16h 50m", "len": 4}, True)
        self.assertEqual(spec.value, "1d 16h 50m")
        self.assertIsNone(spec.unit)

    def test_time_number_stays_numeric(self):
        spec = model.profile_spec(MODULE, definition(tf="dhm", unit="min"),
                                  {"ascii": "60", "display_text": "1h 0m", "len": 4}, True)
        self.assertEqual(spec.value, 60)
        self.assertEqual(spec.unit, "min")

    def test_disabled_duration_formatter_keeps_numeric_sensor_and_unit(self):
        for meta in ({"tf": "none", "tb": "m"}, {"tf": "dhm", "tb": "none"}, {"tf": "dhm"}):
            spec = model.profile_spec(MODULE, definition("sensor", "ro", unit="min", **meta),
                                      {"ascii": "60", "display_text": "60", "len": 2}, True)
            self.assertEqual(spec.value, 60)
            self.assertEqual(spec.unit, "min")

    def test_select_uses_backing_value_not_first_option(self):
        spec = model.profile_spec(MODULE, definition("select", options="8 h|3 days|6 days 16 h", values="48|432|960"),
                                  {"ascii": "9600", "selected_value": "960", "len": 4}, True)
        self.assertEqual(spec.value, "6 days 16 h")
        self.assertEqual(spec.option_values[-1], "960")

    def test_duplicate_option_labels_cannot_be_written(self):
        spec = model.profile_spec(MODULE, definition("select", options="Same|Same", values="1|2"),
                                  {"ascii": "1", "len": 4}, True)
        self.assertEqual(spec.platform, "sensor")
        self.assertIsNone(spec.action)

    def test_readonly_switch_and_access_override(self):
        spec = model.profile_spec(MODULE, definition("switch", access="ro"),
                                  {"value_bool": True, "len": 1}, True)
        self.assertEqual(spec.platform, "binary_sensor")
        self.assertIsNone(spec.action)
        self.assertTrue(spec.value)

    def test_writeonly_switch_is_available_without_readback(self):
        spec = model.profile_spec(MODULE, definition("switch", "wo"), {}, True)
        self.assertTrue(spec.valid)
        self.assertEqual(spec.action, "entity")
        self.assertIsNone(spec.value)

    def test_missing_readback_is_unavailable(self):
        spec = model.profile_spec(MODULE, definition(), {}, True)
        self.assertFalse(spec.valid)

    def test_bad_numeric_bounds_disable_writes(self):
        for meta in ({"min": "nan"}, {"min": 100, "max": 10}, {"step": 0}):
            spec = model.profile_spec(MODULE, definition(**meta), {"ascii": "50", "len": 1}, True)
            self.assertIsNone(spec.action)
            self.assertEqual(spec.platform, "sensor")

    def test_nonfinite_value_is_unknown(self):
        self.assertIsNone(model.numeric("inf"))
        self.assertIsNone(model.numeric("NaN"))

    def test_master_power_units_and_level_mapping(self):
        specs = model.build_specs(MASTER, {"output_power": 550, "jbc": {"suction_level": 0}}, {})
        self.assertEqual(specs[MASTER + ":output_power"].value, 55)
        self.assertEqual(specs[MASTER + ":level"].value, "High")
        self.assertEqual(specs[MASTER + ":level"].option_values, ("0", "1", "2", "3"))

    def test_power_limits_follow_selected_output(self):
        specs = model.build_specs(MASTER, {"afterrun_power_min": 300, "afterrun_power_max": 800}, {})
        self.assertEqual(specs[MASTER + ":custom_power"].minimum, 30)
        self.assertEqual(specs[MASTER + ":custom_power"].maximum, 80)

    def test_all_32_entities_and_extended_io_channels(self):
        defs = [{**definition("switch"), "id": n, "key": f"out{n}"} for n in range(20, 52)]
        mod = {"uid": MODULE, "online": True, "universal_entities_valid": True,
               "universal_entities": [{"id": n, "len": 1, "value_bool": True} for n in range(20, 52)]}
        specs = model.build_specs(MASTER, {"modules": [mod]}, {MODULE: defs})
        self.assertEqual(sum(s.action == "entity" for s in specs.values()), 32)
        self.assertIn(MODULE + ":entity_51_out51", specs)

    def test_offline_only_affects_module_and_removed_profile_disappears(self):
        mod = {"uid": MODULE, "online": False, "universal_entities_valid": True}
        specs = model.build_specs(MASTER, {"modules": [mod]}, {MODULE: [definition()]})
        self.assertFalse(specs[MODULE + ":entity_20_power"].valid)
        self.assertTrue(specs[MODULE + ":online"].valid)
        self.assertFalse(specs[MODULE + ":online"].value)
        self.assertTrue(specs[MASTER + ":continuous"].valid)
        self.assertNotIn(MODULE + ":entity_20_power", model.build_specs(MASTER, {"modules": [mod]}, {MODULE: []}))

    def test_identity_ignores_alias_and_address(self):
        first = model.build_specs(MASTER, {"modules": [{"uid": MODULE, "addr": 32, "name": "Old"}]}, {})
        second = model.build_specs(MASTER, {"modules": [{"uid": MODULE, "addr": 33, "name": "New"}]}, {})
        self.assertEqual(set(first), set(second))

    def test_disabled_fanio_runtime_is_not_exposed(self):
        d = {**definition("sensor", "ro"), "key": "filter_runtime"}
        mod = {"uid": MODULE, "type": 3, "fanio_filter_mode": 2}
        self.assertNotIn(MODULE + ":entity_20_filter_runtime", model.build_specs(MASTER, {"modules": [mod]}, {MODULE: [d]}))

    def test_reusing_slot_for_another_key_does_not_rebind_automations(self):
        first = model.profile_spec(MODULE, definition(), {"ascii": "50", "len": 4}, True)
        renamed = model.profile_spec(MODULE, {**definition(), "label": "Alias changed"}, {"ascii": "50", "len": 4}, True)
        replaced = model.profile_spec(MODULE, {**definition(), "key": "other_output"}, {"ascii": "50", "len": 4}, True)
        self.assertEqual(first.id, renamed.id)
        self.assertNotEqual(first.id, replaced.id)

    def module_specs(self, module, definitions=()):
        module = {"uid": MODULE, "online": True, **module}
        return model.build_specs(MASTER, {"modules": [module]}, {MODULE: list(definitions)})

    def test_jbc_fae_disconnected_station_not_reported_as_work(self):
        specs = self.module_specs({"type": 1, "jbc_link_flags": 1, "station_addr": 0, "jbc_work_mask": 1})
        self.assertFalse(specs[MODULE + ":jbc_link"].value)
        self.assertTrue(specs[MODULE + ":jbc_work_mask"].valid)
        self.assertFalse(specs[MODULE + ":jbc_work_mask"].value)
        self.assertTrue(specs[MODULE + ":jbc_stand_mask"].valid)
        self.assertFalse(specs[MODULE + ":jbc_stand_mask"].value)

    def test_fae_stand_matches_web_handshake_and_work_derivation(self):
        for flags, address, work, stale_stand, expected in (
            (1, 0x12, 0, 0, True), (1, 0x12, 1, 1, False),
            (0, 0x12, 0, 1, False), (1, 0, 0, 1, False),
        ):
            with self.subTest(flags=flags, address=address, work=work):
                specs = self.module_specs({"type": 1, "jbc_link_flags": flags, "station_addr": address,
                                           "jbc_work_mask": work, "jbc_stand_mask": stale_stand})
                self.assertTrue(specs[MODULE + ":jbc_stand_mask"].valid)
                self.assertEqual(specs[MODULE + ":jbc_stand_mask"].value, expected)
        specs = self.module_specs({"type": 1, "online": False, "jbc_link_flags": 1,
                                   "station_addr": 0x12, "jbc_work_mask": 0})
        self.assertFalse(specs[MODULE + ":jbc_stand_mask"].valid)

    def test_usb_stand_uses_station_mask_not_fae_idle_derivation(self):
        for flags, mask, expected in ((1, 0, False), (1, 2, True), (0, 2, False)):
            specs = self.module_specs({"type": 9, "jbc_link_flags": flags, "jbc_work_mask": 0,
                                       "jbc_stand_mask": mask})
            self.assertTrue(specs[MODULE + ":jbc_stand_mask"].valid)
            self.assertEqual(specs[MODULE + ":jbc_stand_mask"].value, expected)

    def test_serial_numbers_are_hardware_ids_not_module_alias_or_address(self):
        for address, name in ((0x20, "Fan"), (0x21, "New alias")):
            specs = model.build_specs(MASTER, {"modules": [{"uid": MODULE, "online": True,
                                       "addr": address, "display_name": name}]}, {})
            for uid in (MASTER, MODULE):
                serial = specs[uid + ":serial_number"]
                self.assertEqual(serial.value, uid)
                self.assertTrue(serial.valid)
                self.assertTrue(serial.diagnostic)
                self.assertEqual(serial.icon, "mdi:barcode")
                self.assertIsNone(serial.state_class)
        specs = self.module_specs({"online": False})
        self.assertFalse(specs[MODULE + ":serial_number"].valid)

    def test_jbc_two_soldering_ports_temperature_and_countdown(self):
        port = {"valid": True, "tool": 2, "temperature": 3150, "power_permille": 550,
                "flags": 2, "future_mode": 72, "time_to_sleep_hibern": 125, "selected_temp": 3330}
        specs = self.module_specs({"type": 9, "jbc_link_flags": 1, "jbc_usb_model": "DDE",
                                   "jbc_usb_port_count": 2, "jbc_usb_ports": [port, port]})
        self.assertEqual(specs[MODULE + ":jbc_p1_temperature"].value, 350)
        self.assertEqual(specs[MODULE + ":jbc_p2_power"].value, 55)
        self.assertEqual(specs[MODULE + ":jbc_p1_state"].value, "SLEEP")
        self.assertEqual(specs[MODULE + ":jbc_p2_countdown"].value, "2 min 05 s")
        self.assertEqual(specs[MODULE + ":jbc_p1_future_mode"].value, "HIBERNATION")
        self.assertEqual(specs[MODULE + ":jbc_p1_selected_temperature"].value, 370)

    def test_temperature_levels_choose_enabled_setpoint(self):
        port = {"valid": True, "selected_temp": 3150, "levels_on": 1, "selected_level": 1,
                "level_temp": [2700, 3330, 3600], "level_on": [1, 1, 0]}
        module = {"type": 9, "jbc_link_flags": 1, "jbc_usb_model": "DDE", "jbc_usb_port_count": 1, "jbc_usb_ports": [port]}
        self.assertEqual(self.module_specs(module)[MODULE + ":jbc_p1_selected_temperature"].value, 370)
        port["selected_level"] = 2
        self.assertIsNone(self.module_specs(module)[MODULE + ":jbc_p1_selected_temperature"].value)

    def test_station_class_swap_removes_future_mode_and_hotair_flow(self):
        port = {"valid": True, "temperature": 2700, "time_to_sleep_hibern": 550, "time_to_stop": 1251}
        module = {"type": 9, "jbc_link_flags": 1, "jbc_usb_model": "JTSE", "jbc_usb_port_count": 1, "jbc_usb_ports": [port]}
        specs = self.module_specs(module)
        self.assertNotIn(MODULE + ":jbc_p1_future_mode", specs)
        self.assertEqual(specs[MODULE + ":jbc_p1_flow"].value, 55)
        self.assertEqual(specs[MODULE + ":jbc_p1_time_to_stop"].value, "2 min 06 s")
        module["jbc_usb_model"] = "CLMU"
        module["jbc_usb_ports"] = [{"valid": True, "cl_motors_on": True, "cl_door_open": False, "future_mode": 1}]
        specs = self.module_specs(module)
        self.assertNotIn(MODULE + ":jbc_p1_flow", specs)
        self.assertNotIn(MODULE + ":jbc_p1_temperature", specs)
        self.assertEqual(specs[MODULE + ":jbc_p1_cleaning_mode"].value, "DETECTION")
        self.assertTrue(specs[MODULE + ":jbc_p1_cl_motors_on"].value)

    def test_preheater_extractor_and_feeder_only_supported_values(self):
        module = {"type": 9, "jbc_link_flags": 1, "jbc_usb_port_count": 1, "jbc_usb_model": "PHBE",
                  "jbc_usb_ph_tc": [{"actual_temp": 900}, {}],
                  "jbc_usb_ports": [{"valid": True, "ph_selected_power": 650, "ph_heater_status": 1}]}
        specs = self.module_specs(module)
        self.assertEqual(specs[MODULE + ":jbc_tc1_actual_temp"].value, 100)
        self.assertNotIn(MODULE + ":jbc_tc2_actual_temp", specs)
        self.assertEqual(specs[MODULE + ":jbc_p1_ph_selected_power"].value, 65)
        module.update(jbc_usb_model="F2", jbc_usb_ports=[{"valid": True, "fe_intake_work": True}])
        self.assertTrue(self.module_specs(module)[MODULE + ":jbc_p1_fe_intake_work"].value)
        module.update(jbc_usb_model="SF", jbc_usb_ports=[{"valid": True, "sf_speed_tenth_mm_s": 125, "sf_tool_enabled": True}])
        self.assertEqual(self.module_specs(module)[MODULE + ":jbc_p1_sf_speed_tenth_mm_s"].value, 12.5)

    def test_weller_filter_values_and_writes(self):
        specs = self.module_specs({"type": 5, "weller_uart_age_sec": 0, "weller_filter_status": 1,
                                   "weller_filter_runtime_minutes": 2450, "weller_programmed_filter_minutes": 4320,
                                   "weller_speed_percent": 55, "io_output_mask": 3})
        self.assertEqual(specs[MODULE + ":filter_operating_time"].value, "1d 16h 50m")
        self.assertEqual(specs[MODULE + ":filter_remaining"].value, "1d 7h 10m")
        self.assertEqual(specs[MODULE + ":weller_filter"].value, "very good")
        self.assertEqual(specs[MODULE + ":weller_speed"].argument, "speed")
        self.assertEqual(specs[MODULE + ":weller_light"].params, {"bit": 1})
        self.assertEqual(specs[MODULE + ":reset_filter"].params, {"reset_filter": 1})

    def test_weller_disconnected_keeps_last_filter_reading(self):
        for status, text in ((1, "very good"), (10, "change soon"), (100, "change filter")):
            specs = self.module_specs({"type": 5, "weller_uart_age_sec": 65534, "weller_filter_status": status})
            self.assertTrue(specs[MODULE + ":weller_filter"].valid)
            self.assertEqual(specs[MODULE + ":weller_filter"].value, text)
        specs = self.module_specs({"type": 5, "weller_uart_age_sec": 65534, "weller_filter_status": 10})
        self.assertEqual(specs[MODULE + ":filter_interval"].platform, "select")
        self.assertIn("6d 16h", specs[MODULE + ":filter_interval"].options)
        self.assertFalse(specs[MODULE + ":weller_speed"].valid)
        self.assertFalse(specs[MODULE + ":weller_link"].value)

    def test_weller_filter_off_option_preserves_positive_values_and_status(self):
        for language, off in (("en", "Off"), ("de", "Aus")):
            for linked in (True, False):
                module = {"uid": MODULE, "type": 5, "online": True,
                          "weller_uart_age_sec": 0 if linked else 65534,
                          "weller_programmed_filter_minutes": 0,
                          "weller_filter_runtime_minutes": 150,
                          "weller_filter_status": 10}
                specs = model.build_specs(MASTER, {"ui_lang": language, "modules": [module]}, {})
                interval = specs[MODULE + ":filter_interval"]
                self.assertTrue(interval.valid)
                self.assertEqual(interval.value, off)
                self.assertEqual(interval.options[0], off)
                self.assertEqual(interval.option_values[0], "0")
                self.assertEqual(interval.argument, "filter_minutes")
                self.assertEqual(len(interval.options), 31)
                self.assertEqual(interval.options[1], "1h")
                self.assertEqual(interval.option_values[1], "60")
                self.assertEqual(interval.option_values[-1], "9600")
                self.assertEqual(specs[MODULE + ":filter_lifetime"].value, off)
                self.assertEqual(specs[MODULE + ":filter_remaining"].value, off)
                self.assertEqual(specs[MODULE + ":filter_operating_time"].value, "2h 30m")
                self.assertEqual(specs[MODULE + ":weller_filter"].value,
                                 "bald wechseln" if language == "de" else "change soon")

    def test_fanio_disabled_filter_has_no_derived_time_entities(self):
        for mode in (0, 2):
            specs = self.module_specs({"type": 3, "fanio_filter_mode": mode})
            self.assertNotIn(MODULE + ":filter_remaining", specs)
        specs = self.module_specs({"type": 3, "fanio_filter_mode": 1, "fanio_filter_runtime_valid": True,
                                   "fanio_filter_runtime_minutes": 200, "fanio_filter_lifetime_minutes": 100})
        self.assertEqual(specs[MODULE + ":filter_remaining"].value, "0m")

    def test_fanio_descriptor_never_generates_duplicate_legacy_channels(self):
        specs = self.module_specs({"type": 3, "caps": 4, "universal_descriptor_valid": True})
        self.assertNotIn(MODULE + ":out1", specs)
        self.assertEqual(specs[MODULE + ":main_power"].argument, "power")

    def test_fanio_main_controls_not_duplicated_when_in_descriptor(self):
        defs = [definition("switch", role="main_output_enable"),
                {**definition(role="main_output_power"), "id": 21}]
        specs = self.module_specs({"type": 3, "caps": 4 | 32}, defs)
        self.assertNotIn(MODULE + ":main_output", specs)
        self.assertNotIn(MODULE + ":main_power", specs)

    def test_fanio_disabled_pressure_profile_values_are_omitted(self):
        defs = [{**definition("sensor", "ro"), "key": "filter_pressure"}]
        for mode in (0, 1):
            specs = self.module_specs({"type": 3, "fanio_filter_mode": mode}, defs)
            self.assertNotIn(MODULE + ":entity_20_filter_pressure", specs)

    def test_jbc_connection_mode_all_station_classes(self):
        for name, station_flags, port_flags in (
            ("DDE", {"jbc_usb_sold_control_mode": False}, {}),
            ("JTSE", {}, {"ha_value_flags": 0x1000}),
            ("CLMU", {}, {"cl_flags": 0x10}),
            ("PHBE", {"jbc_usb_ph_station_flags": 0x100}, {}),
            ("F2", {"jbc_usb_fe_station_flags": 4}, {}),
            ("SF", {"jbc_usb_sf_station_flags": 0x80}, {}),
        ):
            with self.subTest(station=name):
                specs = self.module_specs({"type": 9, "jbc_link_flags": 1, "jbc_usb_model": name,
                                           "jbc_usb_port_count": 1, "jbc_usb_ports": [port_flags], **station_flags})
                self.assertEqual(specs[MODULE + ":jbc_connect_mode"].value, "MONITOR")

    def test_display_settings_map_correct_handler_arguments(self):
        specs = self.module_specs({"type": 6, "telemetry_valid": True, "display_language": 1,
                                   "display_theme": 0, "display_screensaver_min": 5})
        self.assertEqual(specs[MODULE + ":display_language"].value, "Deutsch")
        self.assertEqual(specs[MODULE + ":display_theme"].argument, "theme")
        self.assertEqual(specs[MODULE + ":display_screensaver_min"].value, "5 min")
        self.assertEqual(specs[MODULE + ":display_screensaver_min"].argument, "screensaver")

    def test_heap_units_match_1024_divisor(self):
        specs = model.build_specs(MASTER, {"developer_mode": True, "heap_free": 102400, "modules": [
            {"uid": MODULE, "module_heap_free": 204800, "online": True, "telemetry_valid": True}]}, {})
        self.assertEqual(specs[MASTER + ":heap_free"].value, 100)
        self.assertEqual(specs[MODULE + ":module_heap_free"].value, 200)
        self.assertEqual(specs[MASTER + ":heap_free"].unit, "KiB")

    def test_heap_precision_and_device_class_not_wasted_memory(self):
        specs = model.build_specs(MASTER, {"developer_mode": True, "heap_free": 179932}, {})
        heap = specs[MASTER + ":heap_free"]
        self.assertEqual(heap.value, 176)
        self.assertEqual(heap.precision, 0)
        self.assertEqual(heap.device_class, "data_size")
        self.assertEqual(heap.state_class, "measurement")

    def test_master_mqtt_icons_and_number_modes(self):
        specs = model.build_specs(MASTER, {}, {})
        for key, icon in (("continuous", "fan-auto"), ("stand_intakes", "soldering-iron"),
                          ("level", "fan-speed-3"), ("delay_work", "timer-cog-outline"),
                          ("afterrun_power", "fan-clock")):
            self.assertEqual(specs[MASTER + ":" + key].icon, "mdi:" + icon)
        self.assertEqual(specs[MASTER + ":delay_work"].number_mode, "box")
        self.assertEqual(specs[MASTER + ":custom_power"].number_mode, "slider")

    def test_jbc_tools_errors_and_port_groups_match_master(self):
        specs = self.module_specs({"type": 9, "jbc_link_flags": 1, "jbc_usb_model": "JTSE",
            "jbc_usb_station_error": 3, "jbc_usb_port_count": 1,
            "jbc_usb_ports": [{"valid": True, "tool": 1, "error": 1, "temperature": 2700}]})
        self.assertEqual(specs[MODULE + ":jbc_p1_tool"].value, "JT")
        self.assertEqual(specs[MODULE + ":jbc_p1_error"].value, "AIR_PUMP_ERROR")
        self.assertEqual(specs[MODULE + ":jbc_usb_station_error"].value, "MEMORY")
        self.assertEqual(specs[MODULE + ":jbc_family"].value, "HA")
        self.assertEqual(specs[MODULE + ":jbc_p1_tool"].icon, "mdi:soldering-iron")
        self.assertEqual(specs[MODULE + ":jbc_p1_tool"].group, "port_1")
        self.assertEqual(specs[MODULE + ":jbc_p1_temperature"].device_class, "temperature")
        self.assertEqual(specs[MODULE + ":jbc_stand_mask"].icon, "mdi:car-brake-parking")

    def test_jbc_class_states_and_connectivity(self):
        for family, port, expected in (("CLMU", {"cl_flags": 1, "cl_motors_on": True}, "CLEANING"),
                                        ("PHBE", {"flags": 32}, "WORK"),
                                        ("SF", {"sf_flags": 4, "sf_feeding_state": True}, "FEEDING")):
            specs = self.module_specs({"type": 9, "jbc_usb_model": family, "jbc_link_flags": 1,
                "jbc_usb_port_count": 1, "jbc_usb_ports": [{"valid": True, **port}]})
            self.assertEqual(specs[MODULE + ":jbc_p1_state"].value, expected)
            self.assertEqual(specs[MODULE + ":jbc_link"].device_class, "connectivity")

    def test_unknown_jbc_values_never_assume_a_known_tool_or_error(self):
        self.assertEqual(model.jbc_tool(99, "DDE", "SOLD"), "0x63")
        self.assertEqual(model.jbc_error(99), "ERROR_0x63")
        self.assertIsNone(model.jbc_error(65535, station=True))

    def test_fanio_filter_fallback_and_descriptor_do_not_duplicate(self):
        module = {"type": 3, "fanio_filter_mode": 2, "fanio_filter_pressure_raw": 42,
                  "fanio_filter_saturation_permille": 123}
        specs = self.module_specs(module)
        self.assertEqual(specs[MODULE + ":filter_saturation"].value, 12)
        self.assertEqual(specs[MODULE + ":filter_pressure"].icon, "mdi:gauge")
        self.assertNotIn(MODULE + ":filter_lifetime", specs)
        specs = self.module_specs(module, [{**definition("sensor", "ro"), "key": "filter_pressure"}])
        self.assertNotIn(MODULE + ":filter_pressure", specs)
        self.assertEqual(specs[MODULE + ":entity_20_filter_pressure"].icon, "mdi:gauge")

    def test_profile_role_and_explicit_icon(self):
        for meta, expected in (({"role": "main_output_power"}, "mdi:fan"),
                               ({"role": "main_output_power", "icon": "mdi:pump"}, "mdi:pump")):
            spec = model.profile_spec(MODULE, definition(**meta), {"ascii": "50", "len": 1}, True)
            model.presentation(spec)
            self.assertEqual(spec.icon, expected)


if __name__ == "__main__":
    unittest.main()
