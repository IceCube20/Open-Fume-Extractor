"""Compare firmware discovery output with native specs for explicit audit cases.

No device/broker connection or production source changes. Descriptor fixtures
are already parsed; this audits discovery/presentation, not descriptor decoding.
The regression suite requires every audited inventory/metadata difference to be empty.
"""
import importlib.util
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys

HA = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("parity_fixtures", HA / "tests/test_mqtt_parity.py")
fixtures = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = fixtures
spec.loader.exec_module(fixtures)
model, MASTER, MODULE = fixtures.model, fixtures.MASTER, fixtures.MODULE
COUNTERS = re.findall(r'HA_JBC_COUNTER\((\w+), (\w+), (0x[0-9A-F]+)U, (\w+), "([^"]+)", "([^"]+)", ("min"|nullptr)\)',
                     (HA.parent / "OpenFumeExtractorMaster/src/JbcUsbHaCounters.h").read_text(encoding="utf-8"))


def definition(id, key, typ, mode="ro", label=None, **meta):
    return {"id": id, "key": key, "type": typ, "mode": mode, "label": label or key, "meta": meta}


def fan_definitions(mode):
    # Exact ids/keys/types from FanIoProModule's dynamic descriptor.
    defs = [definition(20, "in1", "binary_sensor", role="main_input"),
            definition(40, "out1", "switch", "rw", role="output_enable"),
            definition(60, "fan", "switch", "rw", "Extractor", role="main_output_enable"),
            definition(61, "power", "number", "rw", "Power", role="main_output_power", unit="%", min="10", max="100", step="1"),
            definition(62, "rpm", "sensor", label="RPM", unit="rpm")]
    if mode in (2, 3):
        defs += [definition(63, "pressure", "sensor", label="Pressure", unit="raw"),
                 definition(66, "filter_saturation", "sensor", label="Filter", unit="%", scale="10")]
    return defs + [definition(64, "filter_runtime", "sensor", label="Filter operating time", unit="min", tb="m", tf="dhm"),
                   definition(67, "filter_mode", "select", label="Filter mode", options="Off|Runtime|Pressure|Both", values="0|1|2|3")]


def cases():
    result = []

    def add(id, typ=1, family="DDE", definitions=None, core_updates=None, entity_values=None, controls=None, **updates):
        state = fixtures.fixture(typ, family)
        state["developer_mode"] = updates.pop("developer_mode", True)
        state.update(heap_min=100000, loop_max_ms=25, power_save_active=True, work_mask=0,
                     output_module={"valid": True, "rpm": 1200, "power": 600, "fault_text": "OK"},
                     cpu_load_pct=10, output_power=555, afterrun_ms=90000, alarm_count=0,
                     afterrun_power_min=100, afterrun_power_max=1000)
        mod = state["modules"][0]
        mod.update(eco_mode=True, output_status_valid=True, module_output_enabled=True, module_output_power=600,
                   module_output_rpm=1200, combined_fault_text="OK", module_cpu_load_pct=5, module_heap_free=200000,
                   module_heap_min=180000, module_uptime_s=3600, module_loop_max_ms=5,
                   io_main_alias="Extractor", io_in1_alias="Button", io_in2_alias="IN2",
                   io_out1_alias="Valve", io_out2_alias="OUT2", io_output_mask=1,
                   weller_speed_percent=60, display_brightness_pct=85, display_language=0,
                   display_theme=0, display_screensaver_min=5, fanio_filter_pressure_raw=100,
                   fanio_filter_saturation_permille=100)
        mod.update(updates)
        if isinstance(mod.get("ha_jbc"), dict):
            mod["ha_jbc"].update(jbc_p1_set_flow_permille=600,
                                 **{f"jbc_tc{n}_set_temp_c": 100 for n in range(1, 5)})
            mod["ha_jbc"].update(core_updates or {})
            for index, port in enumerate(mod.get("jbc_usb_ports", []), 1):
                for key, flags, mask, kind, *_ in COUNTERS:
                    if mod["ha_jbc"]["jbc_family"] == kind and int(port.get(flags, 0)) & int(mask, 16) == int(mask, 16):
                        port.setdefault(key, 0)
                        mod["ha_jbc"][f"jbc_p{index}_{key}"] = port[key]
        defs = definitions or []
        mod.update(universal_descriptor_valid=definitions is not None, universal_entities_valid=True,
                   universal_entities=[{"id": d["id"], "len": 3, "ascii": "100", "display_text": "100", "value_bool": True,
                                        **(entity_values or {}).get(d["id"], {})} for d in defs])
        for lang in ("de", "en"):
            translated = json.loads(json.dumps(state))
            translated["ui_lang"] = lang
            raw = translated["modules"][0]
            core = raw.get("ha_jbc") or {}
            if controls:
                core["controls"] = [{**control, "name": control[lang]} for control in controls]
                core.update({"jbc_cfg_" + str(control["id"]): control["value"] for control in controls})
            wire = {**raw, "definitions": defs, "core": {**core, "port_count": core.get("jbc_port_count", 0), "family": core.get("jbc_family", "UNKNOWN")}}
            result.append({"id": id + "/" + lang, "state": translated, "definitions": defs,
                           "wire": {"id": id + "/" + lang, "lang": lang, "developer_mode": state.get("developer_mode", False),
                                    "master": id.startswith("Master"), "module": wire}})

    add("Master")
    add("Master diagnostics off", developer_mode=False)
    add("JBC FAE", caps=1)
    for name in ("DDE", "JTSE", "CLMU", "PHBE", "F2", "SF", "UNKNOWN"):
        counters = {
            "DDE": {"counter_work_min": 120, "detail_value_flags": 8},
            "JTSE": {"ha_counter_work_min": 120, "ha_counter_work_cycles": 5, "ha_value_flags": 0x0800},
            "CLMU": {"cl_counter_work_cycles": 5, "cl_counter_cleaning_detection_min": 120, "cl_flags": 4},
            "PHBE": {"ph_flags": 0x7E0, "ph_counter_work_min_power": 125},
            "F2": {"fe_flags": 0x300, "fe_partial_work_cycles": 2},
            "SF": {"sf_flags": 0x60, "sf_counter_tin_length": 9223372036854775809},
        }.get(name, {})
        add("JBC USB " + name, 9, name, caps=1 << 24, jbc_usb_station_name="Test station",
            jbc_usb_ph_tc=[{"selected_temp": 900, "actual_temp": 810}] * 4,
            jbc_usb_ports=[{"valid": True, **counters}])
    add("JBC USB disconnected", 9, "DDE", caps=1 << 24, core_updates={"jbc_link": False})
    add("JBC USB core unavailable", 9, "DDE", caps=1 << 24, core_updates={"valid": False})
    add("JBC USB all SOLD counters", 9, "DDE", caps=1 << 24, jbc_usb_ports=[{
        "valid": True, "detail_value_flags": 0x28, "sold_extra_flags": 3, "sold_special_counter_flags": 10}])
    first_port = fixtures.fixture(9, "DDE")["modules"][0]["ha_jbc"]
    four_ports = {f"jbc_p{n}_" + key[7:]: value for n in range(1, 5)
                  for key, value in first_port.items() if key.startswith("jbc_p1_")}
    add("JBC USB four SOLD ports", 9, "DDE", caps=1 << 24, jbc_usb_port_count=4,
        core_updates={"jbc_port_count": 4, **four_ports}, jbc_usb_ports=[{
            "valid": True, "detail_value_flags": 0x28, "sold_extra_flags": 3,
            "sold_special_counter_flags": 10} for _ in range(4)])
    add("JBC USB unknown selected temperature", 9, "DDE", caps=1 << 24,
        core_updates={"jbc_p1_set_temp_c": -1})
    add("JBC USB invalid port", 9, "DDE", caps=1 << 24, core_updates={"jbc_p1_valid": False})
    temperature = {"id": 0, "platform": "number", "de": "Port 1 Solltemperatur", "en": "Port 1 Set temperature",
                   "unit": "\u00b0C", "icon": "mdi:thermometer", "min": 50, "max": 500, "step": 1, "value": 350}
    levels = [{"id": 2, "platform": "switch", "de": "Port 1 Temperaturlevel aktiv", "en": "Port 1 Temperature levels enabled",
               "icon": "mdi:format-list-numbered", "min": 0, "max": 1, "step": 1, "value": 0},
              {"id": 3, "platform": "select", "de": "Port 1 Temperaturlevel", "en": "Port 1 Temperature level",
               "icon": "mdi:format-list-numbered", "min": 0, "max": 2, "step": 1, "value": 2},
              {**temperature, "id": 5, "de": "Port 1 Level 1 Temperatur", "en": "Port 1 Level 1 Temperature"}]
    flow = {"id": 1, "platform": "number", "de": "Port 1 Soll-Luftstrom", "en": "Port 1 Set flow",
            "unit": "%", "icon": "mdi:fan", "min": 10, "max": 100, "step": 0.1, "value": 55.5}
    sold_controls = [temperature, *levels[:2]]
    for level in range(3):
        sold_controls += [{"id": 4 + level * 4, "platform": "switch", "de": f"Port 1 Level {level + 1} aktiv",
                           "en": f"Port 1 Level {level + 1} enabled", "icon": "mdi:toggle-switch",
                           "min": 0, "max": 1, "step": 1, "value": 1},
                          {**temperature, "id": 5 + level * 4, "de": f"Port 1 Level {level + 1} Temperatur",
                           "en": f"Port 1 Level {level + 1} Temperature"}]
    four_controls = [{**control, "id": control["id"] + (port - 1) * 16,
                      "de": control["de"].replace("Port 1", f"Port {port}"),
                      "en": control["en"].replace("Port 1", f"Port {port}")} for port in range(1, 5) for control in sold_controls]
    add("JBC USB four SOLD ports controls", 9, "DDE", caps=1 << 24, jbc_usb_port_count=4,
        controls=four_controls, core_updates={"jbc_port_count": 4, **four_ports}, jbc_usb_ports=[{
            "valid": True, "detail_value_flags": 0x39, "sold_extra_flags": 3,
            "sold_special_counter_flags": 10} for _ in range(4)])
    for name, selected in (("DDE", [temperature, *levels]), ("JTSE", [temperature, flow]),
                           ("PHBE", [{**temperature, "id": 64, "de": "TC1 Solltemperatur", "en": "TC1 Set temperature"}]),
                           ("F2", [{**flow, "id": 68, "de": "Soll-Luftstrom", "en": "Set flow", "min": 20, "step": 10}])):
        add("JBC USB controls " + name, 9, name, caps=1 << 24, controls=selected)
    add("JBC USB controls unavailable", 9, "DDE", caps=1 << 24, controls=[temperature, *levels],
        core_updates={"jbc_link": False})
    add("FanIO legacy", 2, caps=4 | 8 | 32)
    add("FanIO relay-only legacy", 2, caps=32)
    add("FanIO descriptor", 2, definitions=fan_definitions(0)[:5], caps=4 | 8 | 32 | (1 << 17))
    for name, typ in (("FanIO", 2), ("FanIO Pro", 3)):
        channels = [definition(19 + n, f"in{n}", "binary_sensor", label=f"Taster {n}", role="main_input")
                    for n in range(1, 9)]
        channels += [definition(39 + n, f"out{n}", "switch", "rw", label=f"LED {n}", role="output_enable")
                     for n in range(1, 9)]
        add(name + " eight channels", typ, definitions=channels, caps=1 << 17, fanio_filter_mode=0)
        custom = json.loads(json.dumps(channels))
        custom[7]["meta"]["icon"] = "mdi:gesture-tap-button"
        custom[15]["meta"]["icon"] = "mdi:led-on"
        add(name + " custom channel icons", typ, definitions=custom, caps=1 << 17, fanio_filter_mode=0)
    add("FanIO offline", 2, caps=4 | 8 | 32, online=False)
    add("FanIO diagnostics off", 2, caps=4 | 8 | 32, developer_mode=False)
    add("FanIO stale telemetry", 2, caps=4 | 8 | 32, telemetry_valid=False)
    add("FanIO stale output", 2, caps=4 | 8 | 32, output_status_valid=False)
    for mode in range(4):
        add("FanIO Pro mode " + str(mode), 3, definitions=fan_definitions(mode),
            caps=4 | 8 | 32 | 256 | (1 << 17), fanio_filter_mode=mode)
    add("Weller connected", 5, caps=4 | 8 | 32 | 256)
    add("Weller disconnected", 5, caps=4 | 8 | 32 | 256, weller_uart_age_sec=65534)
    for panel in ("320x480", "800x480", "ST7796 320x480"):
        add("Display " + panel, 6, caps=1 << 10, transport="wifi")
    add("Display stale telemetry", 6, caps=1 << 10, telemetry_valid=False)
    profile = [definition(20 + i, "channel" + str(i), "switch", "rw", role="output_enable", icon="mdi:valve") for i in range(32)]
    for name, typ in (("Universal RS232", 7), ("Modbus RTU", 8)):
        add(name + " 32 entities", typ, definitions=profile, caps=1 << 17)
        varied = [definition(20, "power", "number", "rw", unit="%", min="30", max="100", step="1"),
                  definition(21, "light", "switch", "rw", icon="mdi:lightbulb"),
                  definition(22, "temperature", "sensor", unit="\u00b0C"),
                  definition(23, "runtime", "text", unit="min", tb="m", tf="dhm"),
                  definition(24, "mapped", "text", map="1=Good|10=Warning"),
                  definition(25, "write_only", "number", "wo"),
                  definition(26, "filter", "select", "rw", options="8h|16h", values="48|96"),
                  definition(27, "name", "text", "rw")]
        add(name + " mixed profile", typ, definitions=varied, caps=1 << 17,
            entity_values={23: {"ascii": "2450", "display_text": "1d 16h 50m"},
                           24: {"ascii": "Good", "display_text": "Good"}})
    return result


MASTER_KEYS = {"output": "output_enabled", "power": "output_power", "modules": "modules_count", "jbc_error": "stat_error",
               "jbc_error_text": "stat_error_text", "ip": "master_ip", "mqtt_state": "mqtt_state_text", "heap": "heap_free",
               "cpu": "cpu_load_pct", "firmware": "master_fw", "afterrun_stand": "delay_stand", "afterrun_work": "delay_work",
               "selected_flow": "custom_power", "continuous_suction": "continuous", "suction_level": "level",
               "uptime": "uptime_ms", "loop_max": "loop_max_ms", "eco": "power_save_active"}
MODULE_KEYS = {"firmware": "fw", "cpu": "module_cpu_load_pct", "heap": "module_heap_free", "uptime": "module_uptime_s",
               "heap_min": "module_heap_min", "loop_max": "module_loop_max_ms", "work": "jbc_work_mask", "stand": "jbc_stand_mask",
               "station": "station_type", "device_id": "jbc_device_id", "jbc_model": "jbc_usb_model",
               "jbc_station_error": "jbc_usb_station_error", "jbc_station_name": "jbc_usb_station_name",
               "jbc_port_count": "jbc_usb_port_count", "relay_fan": "main_output", "power": "main_power",
               "filter_runtime": "filter_operating_time", "speed": "weller_speed", "fan": "weller_fan", "light": "weller_light",
               "filter": "weller_filter", "language": "display_language", "theme": "display_theme", "screensaver": "display_screensaver_min",
               "jbc_cleaner_mode": "jbc_p1_cleaning_mode", "jbc_motors": "jbc_p1_cl_motors_on", "jbc_door": "jbc_p1_cl_door_open",
               "jbc_heater": "jbc_p1_heater", "jbc_selected_power": "jbc_p1_ph_selected_power", "jbc_active_zones": "jbc_p1_ph_active_zones",
               "jbc_time_to_stop": "jbc_p1_time_to_stop", "jbc_suction_level": "jbc_p1_suction_level",
               "jbc_intake_work": "jbc_p1_fe_intake_work", "jbc_intake_stand": "jbc_p1_fe_intake_stand",
               "jbc_feeding": "jbc_p1_sf_feeding_state", "jbc_program": "jbc_p1_sf_feeding_selected_program",
               "jbc_speed": "jbc_p1_sf_speed_tenth_mm_s", "jbc_length": "jbc_p1_sf_length_tenth_mm", "jbc_tool_enabled": "jbc_p1_sf_tool_enabled"}


def wire_key(key, master=False, typ=None):
    if master:
        return MASTER_KEYS.get(key, key)
    if key == "rpm" and typ == 5:
        return "weller_rpm"
    if key.startswith("jbc_tc") and key.endswith("_temp_c"):
        return key.removesuffix("set_temp_c") + "selected_temp" if key.endswith("_set_temp_c") else key.removesuffix("temp_c") + "actual_temp"
    if key.startswith("jbc_p"):
        for old, new in (("set_temp_c", "selected_temperature"), ("set_flow_permille", "selected_flow"), ("temp_c", "temperature"), ("power_permille", "power"),
                         ("transition_countdown_s", "countdown"), ("flow_permille", "flow"), ("time_to_stop_ds", "time_to_stop")):
            if key.endswith("_" + old):
                return key[:-len(old)] + new
    return MODULE_KEYS.get(key, key)


def capture(inputs):
    node = shutil.which("node") or str(Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe")
    command = [node, str(HA / "tests/capture_mqtt_catalog.cjs")]
    output = subprocess.run(command, input=json.dumps([c["wire"] for c in inputs]),
                            capture_output=True, text=True, encoding="utf-8", check=True, timeout=60)
    return {r["id"]: r["discovery"] for r in json.loads(output.stdout)}


def run():
    inputs = cases()
    captures = capture(inputs)
    results = []
    for case in inputs:
        is_master = case["wire"]["master"]
        uid = MASTER if is_master else MODULE
        specs = model.build_specs(MASTER, case["state"], {MODULE: case["definitions"]})
        native = {}
        for s in specs.values():
            if s.uid != uid:
                continue
            key = s.key
            if key.startswith("entity_"):
                if key.endswith("_filter_runtime"):
                    key = "filter_operating_time"
                elif key.endswith("_filter_saturation"):
                    key = "filter_saturation"
                elif key.endswith("_filter_mode"):
                    key = "filter_mode"
                else:
                    key = "_".join(key.split("_")[:2])
            native[key] = s
        mqtt = {}
        for message in captures[case["id"]]:
            payload = message["payload"]
            suffix = payload["uniq_id"].removeprefix("test-master_")
            if not is_master:
                suffix = suffix[7:]  # mod_20_
            key = wire_key(suffix, is_master, case["state"]["modules"][0]["type"])
            mqtt[key] = {"platform": message["topic"].split("/")[1], **payload}
        differences = []
        for key in sorted(set(mqtt) & set(native)):
            meta, s = mqtt[key], native[key]
            caption = s.name
            for field, actual, expected in (("name", meta["name"], caption), ("platform", meta["platform"], s.platform),
                    ("diagnostic", meta.get("ent_cat") == "diagnostic", s.diagnostic), ("icon", meta.get("icon"), s.icon),
                    ("unit", meta.get("unit_of_meas"), s.unit), ("device_class", meta.get("dev_cla"), s.device_class),
                    ("state_class", meta.get("stat_cla"), s.state_class), ("control", bool(meta.get("cmd_t")), bool(s.action))):
                if actual != expected:
                    differences.append({"key": key, "field": field, "mqtt": actual, "native": expected})
            if s.platform == "number":
                for field in ("min", "max", "step", "mode"):
                    value = {"min": s.minimum, "max": s.maximum, "step": s.step, "mode": s.number_mode}[field]
                    if meta.get(field) != value:
                        differences.append({"key": key, "field": field, "mqtt": meta.get(field), "native": value})
            if s.platform == "select" and list(s.options) != meta.get("options"):
                differences.append({"key": key, "field": "options", "mqtt": meta.get("options"), "native": s.options})
            if s.platform == "text":
                for field, expected in (("min", 0), ("max", 188), ("mode", "text")):
                    if meta.get(field) != expected:
                        differences.append({"key": key, "field": field, "mqtt": meta.get(field), "native": expected})
        results.append({"case": case["id"], "mqtt_count": len(mqtt), "native_count": len(native),
                        "native_only": sorted(set(native) - set(mqtt)), "mqtt_only": sorted(set(mqtt) - set(native)),
                        "metadata_differences": differences})
    return results


if __name__ == "__main__":
    print(json.dumps(run(), indent=2, ensure_ascii=True))
