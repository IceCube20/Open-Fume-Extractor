"""Convert authoritative master values, without reapplying builder scaling."""
from __future__ import annotations
from dataclasses import dataclass, field
import math
from typing import Any


@dataclass
class Spec:
    uid: str
    key: str
    platform: str
    name: str
    value: Any = None
    valid: bool = True
    unit: str | None = None
    action: str | None = None
    argument: str = "value"
    params: dict = field(default_factory=dict)
    options: tuple[str, ...] = ()
    option_values: tuple[str, ...] = ()
    minimum: float = 0
    maximum: float = 100
    step: float = 1
    diagnostic: bool = False
    icon: str | None = None
    device_class: str | None = None
    state_class: str | None = None
    precision: int | None = None
    number_mode: str = "slider"
    group: str = "status"
    attributes: dict = field(default_factory=dict)

    @property
    def id(self) -> str:
        return self.uid + ":" + self.key


def numeric(value: Any) -> float | None:
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (ValueError, TypeError):
        return None


def divided(value: Any, divisor: float) -> float | None:
    value = numeric(value)
    return None if value is None else value / divisor


def jbc_temperature(value: Any) -> int | None:
    value = numeric(value)
    return None if value is None else int(value / 9)


def duration(value: Any, divisor: float = 1) -> str | None:
    seconds = divided(value, divisor)
    if seconds is None or seconds < 0:
        return None
    seconds = math.ceil(seconds)
    return f"{seconds // 60} min {seconds % 60:02d} s"


def filter_time(value: Any) -> str | None:
    try:
        minutes = int(value)
    except (TypeError, ValueError, OverflowError):
        return None
    if minutes < 0:
        return None
    parts = []
    if minutes // 1440:
        parts.append(f"{minutes // 1440}d")
    if (minutes % 1440) // 60:
        parts.append(f"{(minutes % 1440) // 60}h")
    if minutes % 60 or not parts:
        parts.append(f"{minutes % 60}m")
    return " ".join(parts)


def uptime_text(value: Any, divisor: float = 1) -> str | None:
    """Match the master's duration_text_seconds, including minute rounding."""
    seconds = divided(value, divisor)
    if seconds is None or seconds < 0:
        return None
    seconds = int(seconds)
    if seconds < 60:
        return f"{seconds}s"
    minutes = (seconds + 30) // 60
    days, remainder = divmod(minutes, 1440)
    hours, minutes = divmod(remainder, 60)
    parts = []
    if days:
        parts.append(f"{days}d")
    if hours:
        parts.append(f"{hours}h")
    if minutes or not parts:
        parts.append(f"{minutes}m")
    return " ".join(parts)


def jbc_family(model: str) -> str:
    model = str(model).upper()
    groups = {
        "SOLD": "CA CDCF CDN CP CSCV CDE CFE CAE CPE CSVE DD DDE DDR DI DM DME HD HDE HDR LC NA NAE PSE SM WS ALE",
        "HA": "JT JTSE", "CL": "CLM CLMU", "PH": "PH PHBE PHNE PHSE PHXL",
        "FE": "F1 F2 F2W F4W", "SF": "SF",
    }
    return next((family for family, models in groups.items() if model in models.split()), "UNKNOWN")


def jbc_error(value: Any, family: str = "", station: bool = False) -> str | None:
    raw = numeric(value)
    if raw is None or raw == 65535:
        return None
    raw = int(raw)
    if not raw:
        return "OK"
    if station:
        names = {1: "STOPOVERLOAD_TRAFO", 2: "WRONGSENSOR_TRAFO", 3: "MEMORY",
                 4: "MAINSFREQUENCY", 5: "STATION_MODEL", 6: "NOT_MCU_TOOLS"}
        return names.get(raw, f"ERROR_0x{raw:04X}")
    code = raw + {"HA": 20, "PH": 40}.get(family, 0)
    names = {
        1: "SHORTCIRCUIT", 2: "SHORTCIRCUIT_NR", 3: "OPENCIRCUIT", 4: "NO_TOOL",
        5: "WRONGTOOL", 6: "DETECTIONTOOL", 7: "MAXPOWER", 8: "STOPOVERLOAD_MOS",
        9: "TIN_FEEDER_CLOGGING", 21: "AIR_PUMP_ERROR", 22: "PROTECION_TC_HIGH",
        23: "REGULATION_TC_HIGH", 24: "EXTERNAL_TC_MISSING", 25: "SELECTED_TEMP_NOT_REACHED",
        26: "HIGH_HEATER_INTENSITY", 27: "LOW_HEATER_RESISTANCE", 28: "WRONG_HEATER",
        29: "NOTOOL_HA", 30: "DETECTIONTOOL_HA", 41: "SELECTED_TEMP_NOT_REACHED_PH",
        42: "LOW_HEATER_INTENSITY",
    }
    names.update({43 + i: f"TC{i + 1}_NOT_CONNECTED" for i in range(4)})
    names.update({47 + i: f"TC{i + 1}_LIMIT_REACHED" for i in range(4)})
    return names.get(code, f"ERROR_0x{code:02X}")


def jbc_tool(value: Any, model: str, family: str) -> str | None:
    raw = numeric(value)
    if raw is None:
        return None
    raw = int(raw)
    if model in ("HD", "HDE"):
        raw = 9
    elif model == "NA":
        raw = {1: 7, 3: 8}.get(raw, raw)
    elif model == "ALE":
        raw = 10
    elif family == "HA" and raw:
        raw += 30
    return {0: "No tool", 1: "T210", 2: "T245", 3: "PA", 4: "HT", 5: "DS", 6: "DR",
            7: "NT105", 8: "NP105", 9: "T470", 10: "ALE250", 31: "JT", 32: "TE",
            33: "PHS", 34: "PHB"}.get(raw, f"0x{raw:02X}")


JBC_COUNTER_LABELS = {
    "plug_min": ("Eingesteckt", "Plugged"),
    "work_min": ("Work", "Work"),
    "sleep_min": ("Sleep", "Sleep"),
    "hiber_min": ("Hibernation", "Hibernation"),
    "idle_min": ("Idle", "Idle"),
    "sleep_cycles": ("Sleep-Zyklen", "Sleep cycles"),
    "desold_cycles": ("Entlötzyklen", "Desolder cycles"),
    "work_cycles": ("Arbeitszyklen", "Work cycles"),
    "suction_cycles": ("Absaugzyklen", "Suction cycles"),
    "cleaning_continuous_min": ("Reinigung kontinuierlich", "Continuous cleaning"),
    "cleaning_detection_min": ("Reinigung Erkennung", "Detection cleaning"),
    "door_open_cycles": ("Türöffnungen", "Door openings"),
    "work_min_power": ("Work Leistung", "Work power"),
    "work_min_temp": ("Work Temperatur", "Work temperature"),
    "work_min_profile": ("Work Profil", "Work profile"),
    "work_cycles_power": ("Zyklen Leistung", "Power cycles"),
    "work_cycles_temp": ("Zyklen Temperatur", "Temperature cycles"),
    "work_cycles_profile": ("Zyklen Profil", "Profile cycles"),
    "work_intake_min": ("WORK Ansaugung", "WORK intake"),
    "stand_intake_min": ("STAND Ansaugung", "STAND intake"),
    "tin_deliver_cycles": ("Zinnförderzyklen", "Tin delivery cycles"),
    "tin_length": ("Zinnlänge", "Tin length"),
    "sold_number": ("Lötungen", "Soldering count"),
    "energy_delivered": ("Abgegebene Energie", "Delivered energy"),
    "sold_total": ("Lötungen gesamt", "Total soldering count"),
    "sold_per_min": ("Lötungen pro Minute", "Soldering per minute"),
    "sold_ok": ("Gültige Lötungen", "Successful soldering count"),
}


def jbc_counter_metadata(key: str):
    partial = "partial_" in key
    marker = "partial_" if partial else "counter_"
    leaf = key.partition(marker)[2]
    labels = JBC_COUNTER_LABELS.get(leaf)
    if not labels or key.endswith("_flags"):
        return None
    if partial:
        labels = ("Teil " + labels[0], "Partial " + labels[1])
    unit = "min" if "_min" in leaf and "per_min" not in leaf else None
    return labels, unit


def jbc_counter_matches_family(key: str, family: str) -> bool:
    prefixes = {"SOLD": ("counter_", "sold_"), "HA": ("ha_",), "CL": ("cl_",),
                "PH": ("ph_",), "FE": ("fe_",), "SF": ("sf_",)}
    return key.startswith(prefixes.get(family, ()))


def presentation(spec: Spec) -> None:
    """Mirror MQTT presentation without changing stable identities or raw scaling."""
    key = spec.key
    leaf = key.split("_", 2)[2] if key.startswith("entity_") else key
    if key.startswith("jbc_cfg_"):
        pass  # The cached control ID already assigns a port/station group.
    elif key.startswith("jbc_p"):
        spec.group = key.split("_", 2)[1].replace("p", "port_")
        leaf = key.split("_", 2)[2]
    elif key.startswith("jbc_tc"):
        spec.group = "thermocouples"
    elif "filter" in key:
        spec.group = "filter"
    elif spec.diagnostic:
        spec.group = "diagnostics"
    elif spec.action:
        spec.group = "controls"
    icons = {
        "output_enabled": "fan", "output_power": "fan", "feedback_power": "fan", "work": "soldering-iron",
        "feedback_rpm": "fan", "afterrun": "timer-outline", "modules_count": "devices",
        "stat_error": "alert-circle-outline", "stat_error_text": "alert-circle-outline",
        "alarm_count": "bell-alert-outline", "alarm_text": "bell-alert-outline",
        "wifi_rssi": "wifi", "master_ip": "ip-network", "datetime": "calendar-clock",
        "mqtt_state_text": "lan-connect", "mqtt_state_code": "lan-connect",
        "cpu_load_pct": "chip", "module_cpu_load_pct": "chip", "fw": "chip", "master_fw": "chip",
        "heap_free": "memory", "heap_min": "memory", "module_heap_free": "memory", "module_heap_min": "memory",
        "uptime_ms": "clock-outline", "module_uptime_s": "clock-outline",
        "loop_max_ms": "timer-outline", "module_loop_max_ms": "timer-outline", "transport": "lan",
        "online": "lan-connect", "jbc_link": "lan-connect", "weller_link": "lan-connect",
        "station_type": "devices", "jbc_device_id": "identifier", "jbc_usb_model": "devices",
        "jbc_family": "devices", "jbc_usb_station_name": "tag-text-outline", "jbc_usb_port_count": "counter",
        "jbc_connect_mode": "lan", "state": "list-status", "cleaning_mode": "tune-variant",
        "cl_door_open": "door", "ph_active_zones": "view-grid-outline", "ph_selected_power": "fan",
        "fe_intake_work": "soldering-iron", "fe_intake_stand": "car-brake-parking", "suction_level": "fan-speed-3",
        "sf_feeding_selected_program": "format-list-numbered", "sf_speed_tenth_mm_s": "speedometer",
        "sf_length_tenth_mm": "ruler", "sf_tool_enabled": "soldering-iron",
        "flow": "fan", "selected_flow": "fan", "power": "fan", "temperature": "thermometer",
        "selected_temperature": "thermometer", "in1": "import", "in2": "import", "out1": "export", "out2": "export",
        "in3": "import", "in4": "import", "in5": "import", "in6": "import", "in7": "import", "in8": "import",
        "out3": "export", "out4": "export", "out5": "export", "out6": "export", "out7": "export", "out8": "export",
        "rpm": "fan", "weller_rpm": "fan", "fault": "alert-circle-outline", "output_fault": "alert-circle-outline",
        "weller_fan": "fan", "weller_light": "lightbulb", "fan": "fan", "light": "lightbulb",
        "continuous": "fan-auto", "stand_intakes": "soldering-iron", "level": "fan-speed-3",
        "custom_power": "fan", "delay_work": "timer-cog-outline", "delay_stand": "timer-outline",
        "afterrun_power": "fan-clock", "afterrun_power_enabled": "fan-clock",
        "status_led_enabled": "led-on", "status_led_brightness": "brightness-6",
        "main_input": "source-branch", "main_output": "fan", "main_power": "fan",
        "weller_speed": "fan", "brightness": "brightness-6", "display_language": "translate",
        "display_theme": "theme-light-dark", "display_screensaver_min": "monitor-screenshot",
        "jbc_usb_station_error": "alert-circle-outline", "error": "alert-circle-outline",
        "tool": "soldering-iron", "jbc_work_mask": "soldering-iron", "jbc_stand_mask": "car-brake-parking",
        "future_mode": "progress-clock", "countdown": "timer-sand", "time_to_stop": "timer-sand",
        "cl_motors_on": "fan", "heater": "radiator", "jbc_continuous": "fan",
        "sf_feeding_state": "ray-start-arrow", "weller_filter": "air-filter",
        "filter_saturation": "air-filter", "filter_pressure": "gauge", "filter_mode": "tune-variant",
        "filter_lifetime": "calendar-sync", "filter_interval": "calendar-sync",
        "filter_operating_time": "timer-outline", "filter_runtime": "timer-outline",
        "filter_remaining": "timer-sand", "reset_filter": "air-filter",
        "restart": "restart", "reboot": "restart", "eco": "sprout", "power_save_active": "sprout",
        "serial_number": "barcode",
    }
    if not spec.icon and leaf in icons:
        spec.icon = "mdi:" + icons[leaf]
    if not spec.icon:
        unit_icons = {"\u00b0C": "thermometer", "KiB": "memory", "dBm": "wifi", "rpm": "fan", "%": "percent",
                      "s": "timer-outline", "ms": "timer-outline", "min": "timer-outline", "mm": "ruler", "mm/s": "speedometer"}
        platform_icons = {"switch": "toggle-switch", "binary_sensor": "checkbox-marked-circle-outline",
                          "select": "format-list-bulleted", "number": "tune", "button": "gesture-tap-button", "text": "form-textbox"}
        spec.icon = "mdi:" + ("thermometer" if key.startswith("jbc_tc") else
                              unit_icons.get(spec.unit, platform_icons.get(spec.platform, "gauge")))
    if spec.platform == "binary_sensor":
        if key in ("online", "jbc_link", "weller_link"):
            spec.device_class = "connectivity"
        elif leaf == "cl_door_open":
            spec.device_class = "door"
    if spec.platform == "sensor":
        classes = {"KiB": "data_size", "dBm": "signal_strength", "\u00b0C": "temperature"}
        spec.device_class = classes.get(spec.unit)
        if spec.unit and (spec.value is None or isinstance(spec.value, (int, float)) and not isinstance(spec.value, bool)):
            spec.state_class = "measurement"
        if "selected_temp" in leaf or leaf == "ph_selected_power":
            spec.state_class = None
        spec.precision = 0 if spec.unit in ("KiB", "dBm", "rpm", "\u00b0C", "s", "ms") else 1 if spec.unit == "%" else None
        if key in ("output_power", "feedback_power"):
            spec.precision = 0
    if key in ("delay_work", "delay_stand", "filter_interval"):
        spec.number_mode = "box"


def mqtt_presentation(spec: Spec, state: dict, master_uid: str) -> None:
    """MQTT's built-in captions; never translate user aliases/profile labels."""
    german = state.get("ui_lang") == "de"
    names = {
        "output_enabled": ("Ausgang", "Output"), "output_power": ("Angeforderte Leistung", "Requested Power"),
        "afterrun": ("Nachlauf", "Afterrun"), "modules_count": ("Module", "Modules"),
        "alarm_count": ("Alarm Anzahl", "Alarm Count"), "alarm_text": ("Alarm Text", "Alarm Text"),
        "wifi_rssi": ("WLAN Signal", "WiFi RSSI"), "master_ip": ("IP Adresse", "IP Address"),
        "datetime": ("Datum/Zeit", "Date Time"), "mqtt_state_text": ("MQTT Status", "MQTT State"),
        "mqtt_state_code": ("MQTT Statuscode", "MQTT State Code"),
        "cpu_load_pct": ("CPU Last", "CPU Load"), "heap_free": ("Freier Heap", "Free Heap"),
        "master_fw": ("Firmware", "Firmware"), "fw": ("Firmware", "Firmware"),
        "serial_number": ("Seriennummer", "Serial number"), "uptime_ms": ("Laufzeit", "Uptime"),
        "module_cpu_load_pct": ("CPU", "CPU"), "module_heap_free": ("Heap", "Heap"),
        "module_uptime_s": ("Laufzeit", "Uptime"), "online": ("Verbindung", "Connection"),
        "heap_min": ("Minimaler freier Heap", "Minimum free heap"),
        "module_heap_min": ("Minimaler freier Heap", "Minimum free heap"),
        "loop_max_ms": ("Maximale Loop-Zeit", "Maximum loop time"),
        "module_loop_max_ms": ("Maximale Loop-Zeit", "Maximum loop time"), "transport": ("Transport", "Transport"),
        "stat_error": ("JBC Fehler", "JBC Error"), "stat_error_text": ("JBC Fehlertext", "JBC Error Text"),
        "continuous": ("Dauerlauf", "Continuous suction"), "level": ("Absaugstufe", "Suction level set"),
        "stand_intakes": ("Stand Intakes", "Stand intakes"),
        "custom_power": ("Benutzerleistung", "Selected flow"), "delay_work": ("Nachlauf Work", "Afterrun work"),
        "delay_stand": ("Nachlauf Stand", "Afterrun stand"), "afterrun_power": ("Nachlaufleistung", "Afterrun power"),
        "afterrun_power_enabled": ("Nachlaufleistung aktiv", "Afterrun power enabled"),
        "status_led_enabled": ("Status-LEDs", "Status LEDs"), "status_led_brightness": ("Status-LED Helligkeit", "Status LED brightness"),
        "main_input": ("Haupteingang", "Main input"), "main_output": ("Hauptausgang Absaugung", "Main extractor output"),
        "jbc_link": ("JBC Verbindung", "JBC Link"), "station_type": ("Station", "Station"),
        "jbc_device_id": ("Device ID", "Device ID"), "jbc_usb_model": ("Stationsmodell", "Station model"),
        "jbc_family": ("Gerätefamilie", "Device family"), "jbc_usb_station_error": ("Stationsfehler", "Station error"),
        "jbc_connect_mode": ("Verbindungsmodus", "Connection mode"), "jbc_usb_station_name": ("Stationsname", "Station name"),
        "jbc_usb_port_count": ("Ports", "Ports"), "jbc_continuous": ("Dauerabsaugung", "Continuous suction"),
        "weller_link": ("Weller Verbindung", "Weller Link"), "weller_speed": ("Drehzahl", "Speed"),
        "weller_fan": ("Lüfter", "Fan"), "weller_light": ("Licht", "Light"),
        "weller_rpm": ("RPM", "RPM"), "rpm": ("RPM", "RPM"), "weller_filter": ("Filterzustand", "Filter status"),
        "filter_mode": ("Filtermodus", "Filter mode"), "filter_saturation": ("Filter", "Filter"),
        "filter_pressure": ("Druck", "Pressure"), "filter_lifetime": ("Filter-Wechselintervall", "Filter replacement interval"),
        "filter_operating_time": ("Filterbetriebszeit", "Filter operating time"), "filter_runtime": ("Filterbetriebszeit", "Filter operating time"),
        "filter_remaining": ("Filter-Restlaufzeit", "Filter remaining time"), "fault": ("Fehler", "Fault"),
        "main_power": ("Leistung", "Power"), "brightness": ("Helligkeit", "Brightness"),
        "display_language": ("Sprache", "Language"), "display_theme": ("Farbschema", "Theme"),
        "display_screensaver_min": ("Ruhemodus", "Screensaver"), "restart": ("Neustart", "Restart"),
        "reboot": ("Neustart", "Restart"), "eco": ("Energiesparen", "Power saving"),
        "power_save_active": ("Energiesparen", "Power saving"),
        "work": ("Work", "Work"), "feedback_rpm": ("Lüfter RPM", "Fan RPM"),
        "feedback_power": ("Ausgang Rückmeldung", "Output power feedback"),
        "output_fault": ("Ausgang Fehler", "Output fault"),
        "reset_filter": ("Filterbetriebszeit zurücksetzen", "Reset filter operating time"),
        "filter_interval": ("Filter-Wechselintervall", "Filter replacement interval"),
    }
    key = spec.key
    module = next((m for m in state.get("modules", []) if str(m.get("uid", "")).upper() == spec.uid), {})
    # A descriptor's translated label/alias is already authoritative. Fan/IO
    # filter telemetry is a dedicated MQTT entity, not a second profile copy.
    if key.startswith("entity_"):
        leaf = key.split("_", 2)[2]
        if module.get("type") != 3 or leaf not in ("filter_pressure", "filter_saturation", "filter_runtime", "filter_mode"):
            return
        key = leaf
    if key in names and not (key == "main_output" and spec.uid != master_uid):
        spec.name = names[key][0 if german else 1]
    elif key.startswith("jbc_p"):
        leaf = key.split("_", 2)[2]
        port_names = {
            "state": ("Status", "State"), "tool": ("Werkzeug", "Tool"), "error": ("Fehler", "Error"),
            "temperature": ("Temperatur", "Temperature"), "selected_temperature": ("Solltemperatur", "Set temperature"),
            "power": ("Leistung", "Power"), "future_mode": ("Nächster Modus", "Next mode"),
            "countdown": ("Zeit bis Moduswechsel", "Time to mode change"), "flow": ("Luftstrom", "Flow"),
            "selected_flow": ("Soll-Luftstrom", "Set flow"), "time_to_stop": ("Time to stop", "Time to stop"),
            "cleaning_mode": ("Reinigungsmodus", "Cleaner mode"), "cl_motors_on": ("Motoren", "Motors"),
            "cl_door_open": ("Tür offen", "Door open"), "heater": ("Heizung", "Heater"),
            "ph_selected_power": ("Sollleistung", "Selected power"), "ph_active_zones": ("Aktive Zonen", "Active zones"),
            "suction_level": ("Absaugstufe", "Suction level"), "fe_intake_work": ("WORK Ansaugung", "WORK intake"),
            "fe_intake_stand": ("STAND Ansaugung", "STAND intake"), "sf_feeding_state": ("Zinnzufuhr", "Feeding"),
            "sf_feeding_selected_program": ("Programm", "Program"), "sf_speed_tenth_mm_s": ("Geschwindigkeit", "Speed"),
            "sf_length_tenth_mm": ("Länge", "Length"), "sf_tool_enabled": ("Werkzeug aktiviert", "Tool enabled"),
        }
        counter = jbc_counter_metadata(leaf)
        if counter:
            spec.name = "Port " + key.split("_")[1][1:] + " " + counter[0][0 if german else 1]
        elif leaf in port_names:
            prefix = spec.name.split(" ", 2)[:2] if spec.name.startswith("Port ") else []
            spec.name = (" ".join(prefix) + " " if prefix else "") + port_names[leaf][0 if german else 1]
    elif key.startswith("jbc_tc"):
        spec.name = key.split("_")[1].upper() + (" Solltemperatur" if german else " Set temperature") if key.endswith("selected_temp") else key.split("_")[1].upper() + (" Temperatur" if german else " Temperature")
    if key == "level" and german:
        selected = spec.option_values[spec.options.index(spec.value)] if spec.value in spec.options else None
        spec.options = ("Hoch", "Mittel", "Niedrig", "Benutzer")
        spec.value = spec.options[int(selected)] if selected is not None else None
    if key == "weller_filter":
        code = module.get("weller_filter_status")
        spec.value = ({1: "sehr gut", 10: "bald wechseln", 100: "Filter wechseln"} if german else
                      {1: "very good", 10: "change soon", 100: "change filter"}).get(code, "unbekannt" if german else "unknown")
    if module.get("type") == 5 and numeric(module.get("weller_programmed_filter_minutes")) == 0:
        if key in ("filter_interval", "filter_lifetime", "filter_remaining"):
            spec.value = "Aus" if german else "Off"
    if module.get("type") == 5 and key == "filter_interval" and german:
        spec.options = ("Aus",) + spec.options[1:]
    if key == "filter_mode":
        mode = module.get("fanio_filter_mode")
        spec.value = (("Aus", "Nur Laufzeit", "Nur Drucksensor", "Laufzeit + Drucksensor") if german else
                      ("Off", "Runtime only", "Pressure only", "Runtime + pressure"))[int(mode or 0)]
        spec.platform, spec.action, spec.unit = "sensor", None, None
    if spec.uid == master_uid and key == "continuous":
        spec.value = bool(state.get("jbc", {}).get("continuous", state.get("continuous")))
    if key in ("heap_free", "heap_min", "module_heap_free", "module_heap_min") and spec.value is not None:
        spec.value = round(spec.value) if spec.uid == master_uid else math.floor(spec.value + 0.5)
    if key in ("filter_saturation", "main_power", "afterrun_power") and isinstance(spec.value, (int, float)):
        spec.value = math.floor(spec.value + 0.5)
        spec.precision = 0
    if key in ("alarm_count", "modules_count", "stat_error", "mqtt_state_code"):
        spec.state_class = "measurement"
    if key == "modules_count":
        spec.diagnostic, spec.group = False, "status"
    if key in ("cpu_load_pct", "module_cpu_load_pct", "modules_count", "alarm_count", "stat_error", "mqtt_state_code"):
        spec.precision = 0
    if key == "output_power" and spec.value is not None:
        spec.value = round(spec.value)
    if key == "feedback_power" and spec.value is not None:
        spec.value = math.floor(spec.value + 0.5)
    if key.endswith("selected_temperature") or key.endswith("sf_length_tenth_mm"):
        spec.state_class = None
    if key == "custom_power":
        raw = int(state.get("jbc", {}).get("select_flow", 0))
        spec.value = max(10, min(100, (raw + 5) // 10 if raw >= 100 else raw))


def legacy_jbc_specs(uid: str, module: dict, online: bool):
    linked = bool(int(module.get("jbc_link_flags", 0)) & 1)
    usb = module.get("type") == 9 or bool(int(module.get("caps", 0)) & (1 << 24))
    if not usb:
        linked = linked and bool(module.get("station_addr"))
    yield Spec(uid, "jbc_link", "binary_sensor", "Station connected", linked, online)
    valid = online and linked
    for key, name in (("jbc_work_mask", "Work"), ("jbc_stand_mask", "Stand")):
        active = linked and bool(module.get(key))
        if key == "jbc_stand_mask" and not usb:
            active = linked and not bool(module.get("jbc_work_mask"))
        yield Spec(uid, key, "binary_sensor", name, active, online)
    if not usb:
        for key, name in (("station_type", "Station"), ("jbc_device_id", "Station device ID")):
            value = module.get(key)
            if key == "station_type":
                value = f"{value} 0x{int(module.get('station_addr', 0)):02X}" if linked else "-"
            elif key == "jbc_device_id":
                try:
                    value = "".join(chr(b) if 32 <= b < 127 else "." if b else "" for b in bytes.fromhex(value or "")).strip() or "-"
                except (TypeError, ValueError):
                    value = None
            yield Spec(uid, key, "sensor", name, value, online)
        return
    for key, name in (("jbc_usb_model", "Station model"), ("jbc_usb_station_name", "Station name"),
                      ("jbc_usb_station_error", "Station error"), ("jbc_usb_port_count", "Ports")):
        value = module.get(key)
        if key == "jbc_usb_station_error":
            value = jbc_error(value, station=True)
        yield Spec(uid, key, "sensor", name, value, valid and value is not None and value != 65535)
    family = jbc_family(module.get("jbc_usb_model", ""))
    yield Spec(uid, "jbc_family", "sensor", "Station class", family, valid)
    ports = module.get("jbc_usb_ports", [])[:min(int(module.get("jbc_usb_port_count", 0)), 4)]
    first = ports[0] if ports else {}
    flags, known, control = 0, False, False
    if family == "SOLD":
        known = "jbc_usb_sold_control_mode" in module
        control = module.get("jbc_usb_sold_control_mode")
    else:
        field, known_mask, control_mask = {
            "HA": ("ha_value_flags", 0x1000, 0x2000), "CL": ("cl_flags", 0x10, 0x20),
            "PH": ("jbc_usb_ph_station_flags", 0x100, 0x200),
            "FE": ("jbc_usb_fe_station_flags", 4, 8), "SF": ("jbc_usb_sf_station_flags", 0x80, 0x100),
        }.get(family, ("", 0, 0))
        flags = int((first if family in ("HA", "CL") else module).get(field, 0))
        known, control = bool(flags & known_mask), bool(flags & control_mask)
    if known:
        yield Spec(uid, "jbc_connect_mode", "sensor", "Connection mode", "CONTROL" if control else "MONITOR", valid)
    for index, port in enumerate(ports, 1):
        prefix, title = f"jbc_p{index}_", f"Port {index} "
        ready = valid and bool(port.get("valid"))

        def item(key, name, value, unit=None, platform="sensor"):
            return Spec(uid, prefix + key, platform, title + name, value, ready, unit)

        yield item("error", "Error", jbc_error(port.get("error"), family))
        if family not in ("SOLD", "HA"):
            state = "IDLE"
            if family == "CL" and int(port.get("cl_flags", 0)) & 1 and port.get("cl_motors_on"):
                state = "CLEANING"
            elif family == "PH" and int(port.get("flags", 0)) & 32:
                state = "WORK"
            elif family == "SF" and int(port.get("sf_flags", 0)) & 4 and port.get("sf_feeding_state"):
                state = "FEEDING"
            yield item("state", "State", state)
        if family in ("SOLD", "HA"):
            yield item("tool", "Tool", jbc_tool(port.get("tool"), str(module.get("jbc_usb_model", "")).upper(), family))
            yield item("temperature", "Temperature", jbc_temperature(port.get("temperature")), "\u00b0C")
            yield item("power", "Power", divided(port.get("power_permille"), 10), "%")
            setpoint = port.get("selected_temp")
            if family == "SOLD" and port.get("levels_on"):
                level = port.get("selected_level")
                temps, enabled = port.get("level_temp", []), port.get("level_on", [])
                setpoint = (temps[level] if isinstance(level, int) and 0 <= level < min(len(temps), len(enabled))
                            and enabled[level] else None)
            yield item("selected_temperature", "Selected temperature", jbc_temperature(setpoint), "\u00b0C")
            flags = int(port.get("flags", 0))
            state = "NO_TOOL" if not port.get("tool") else "WORK"
            states = ((4, "HIBERNATION"), (2, "SLEEP"), (1, "STAND"), (8, "EXTRACTOR")) if family == "SOLD" else (
                (32, "WORK"), (1, "STAND"), (64, "COOLING"), (128, "SUCTION"))
            if port.get("tool"):
                state = next((name for mask, name in states if flags & mask), "WORK" if family == "SOLD" else "IDLE")
            yield item("state", "State", state)
            if family == "SOLD":
                mode = port.get("future_mode")
                yield item("future_mode", "Future mode", {0: "NONE", 78: "NONE", 83: "SLEEP", 72: "HIBERNATION"}.get(mode))
                yield item("countdown", "Transition countdown", duration(port.get("time_to_sleep_hibern")))
            else:
                yield item("flow", "Air flow", divided(port.get("time_to_sleep_hibern"), 10), "%")
                yield item("time_to_stop", "Time to stop", duration(port.get("time_to_stop"), 10))
                if "selected_flow_permille" in port:
                    yield item("selected_flow", "Selected air flow", divided(port["selected_flow_permille"], 10), "%")
        elif family == "CL":
            yield item("cleaning_mode", "Cleaning mode", {1: "DETECTION", 2: "CONTINUOUS", 4: "CALIBRATING"}.get(port.get("future_mode")))
            for key, name in (("cl_motors_on", "Motors"), ("cl_door_open", "Door open")):
                if key in port:
                    yield item(key, name, port[key], platform="binary_sensor")
        elif family == "PH":
            yield item("time_to_stop", "Time to stop", duration(port.get("time_to_stop"), 10))
            for key, name, divisor, unit in (("ph_selected_power", "Selected power", 10, "%"),
                                            ("ph_active_zones", "Active zones", 1, None),
                                            ("ph_work_mode", "Work mode", 1, None)):
                if key in port:
                    yield item(key, name, divided(port[key], divisor), unit)
            if "ph_heater_status" in port:
                yield item("heater", "Heater", bool(port["ph_heater_status"]), platform="binary_sensor")
        elif family == "FE":
            yield item("suction_level", "Suction level", {0: "HIGH", 1: "MEDIUM", 2: "LOW", 3: "CUSTOM"}.get(port.get("future_mode")))
            for key, name in (("fe_intake_work", "Intake work"), ("fe_intake_stand", "Intake stand")):
                if key in port:
                    yield item(key, name, port[key], platform="binary_sensor")
        elif family == "SF":
            for key, name, divisor, unit in (("sf_speed_tenth_mm_s", "Feeding speed", 10, "mm/s"),
                                            ("sf_length_tenth_mm", "Feeding length", 10, "mm"),
                                            ("sf_feeding_selected_program", "Program", 1, None)):
                if key in port:
                    yield item(key, name, divided(port[key], divisor), unit)
            for key, name in (("sf_feeding_state", "Feeding"), ("sf_tool_enabled", "Tool enabled")):
                if key in port:
                    yield item(key, name, bool(port[key]), platform="binary_sensor")
        # Optional counters exist in /state only after the station supplied them.
        for key, value in port.items():
            metadata = jbc_counter_metadata(key)
            if metadata and jbc_counter_matches_family(key, family):
                labels, unit = metadata
                try:
                    raw = int(value)
                except (TypeError, ValueError, OverflowError):
                    raw = None
                spec = item(key, labels[1], filter_time(raw) if unit else raw)
                spec.diagnostic = True
                spec.icon = "mdi:timer-outline" if unit else "mdi:counter"
                if unit and spec.value is not None:
                    spec.attributes["counter_minutes"] = raw
                yield spec
    if family == "PH":
        for index, tc in enumerate(module.get("jbc_usb_ph_tc", [])[:4], 1):
            for key, name in (("actual_temp", "Temperature"), ("selected_temp", "Selected temperature")):
                if key in tc:
                    yield Spec(uid, f"jbc_tc{index}_{key}", "sensor", f"TC {index} {name}", jbc_temperature(tc[key]), valid, "\u00b0C")
    if family == "FE" and int(module.get("jbc_usb_fe_station_flags", 0)) & 1:
        yield Spec(uid, "jbc_continuous", "binary_sensor", "Station continuous", bool(int(module["jbc_usb_fe_station_flags"]) & 2), valid)


def jbc_specs(uid: str, module: dict, online: bool):
    core = module.get("ha_jbc")
    if not isinstance(core, dict):
        yield from legacy_jbc_specs(uid, module, online)
        return
    linked = bool(core.get("jbc_link"))
    valid = online and bool(core.get("valid")) and linked
    family = core.get("jbc_family", "UNKNOWN")
    if core.get("valid") is True and core.get("jbc_link") is False:
        # An explicit module report of station absence supersedes cached ports.
        for key, name in (("jbc_link", "Station connected"), ("jbc_work_mask", "Work"),
                          ("jbc_stand_mask", "Stand")):
            yield Spec(uid, key, "binary_sensor", name, False, online)
        return
    for control in core.get("controls", []):
        ident = control.get("id")
        platform = control.get("platform")
        if not isinstance(ident, int) or not 0 <= ident < 69 or platform not in ("number", "switch", "select"):
            continue
        key = f"jbc_cfg_{ident}"
        value = numeric(core.get(key))
        ready = valid and value is not None and value >= 0
        if platform == "select":
            ready = ready and value in (0, 1, 2)
        options = ("Level 1", "Level 2", "Level 3") if platform == "select" else ()
        if platform == "select":
            value = options[int(value)] if value is not None and value in (0, 1, 2) else None
        elif platform == "switch":
            value = bool(value) if ready else None
        yield Spec(uid, key, platform, control["name"], value, ready, control.get("unit"),
                   "jbc_usb", "value", params={"control": ident}, icon=control.get("icon"),
                   minimum=control.get("min", 0), maximum=control.get("max", 100), step=control.get("step", 1),
                   number_mode="box", options=options, option_values=("0", "1", "2") if options else (),
                   group=f"port_{ident // 16 + 1}" if ident < 64 else "station")
    for key, field, platform, name in (
        ("jbc_link", "jbc_link", "binary_sensor", "Station connected"),
        ("jbc_work_mask", "work_active", "binary_sensor", "Work"),
        ("jbc_stand_mask", "stand_active", "binary_sensor", "Stand"),
        ("jbc_usb_model", "jbc_model", "sensor", "Station model"),
        ("jbc_family", "jbc_family", "sensor", "Station class"),
        ("jbc_usb_port_count", "jbc_port_count", "sensor", "Ports"),
        ("jbc_usb_station_error", "jbc_station_error_text", "sensor", "Station error"),
        ("jbc_connect_mode", "jbc_connect_mode", "sensor", "Connection mode"),
    ):
        value = core.get(field)
        if platform == "binary_sensor":
            value = linked and bool(value) if key != "jbc_link" else linked
        yield Spec(uid, key, platform, name, value,
                   online if platform == "binary_sensor" else valid and value not in (None, "-"))
    station_name = module.get("jbc_usb_station_name")
    yield Spec(uid, "jbc_usb_station_name", "sensor", "Station name", station_name,
               valid and station_name not in (None, "", "-"), diagnostic=True)
    count = max(0, min(int(core.get("jbc_port_count", 0)), 4))
    for index in range(1, count + 1):
        prefix = f"jbc_p{index}_"
        ready = valid and bool(core.get(prefix + "valid"))
        fields = [("state", "state", "State", None, 1), ("error", "error_text", "Error", None, 1)]
        if family in ("SOLD", "HA"):
            fields += [("tool", "tool_name", "Tool", None, 1),
                       ("temperature", "temp_c", "Temperature", "\u00b0C", 1),
                       ("selected_temperature", "set_temp_c", "Selected temperature", "\u00b0C", 1),
                       ("power", "power_permille", "Power", "%", 10)]
            if family == "SOLD":
                fields += [("future_mode", "future_mode", "Future mode", None, 1),
                           ("countdown", "transition_countdown_s", "Transition countdown", None, 1)]
            else:
                fields += [("flow", "flow_permille", "Air flow", "%", 10),
                           ("selected_flow", "set_flow_permille", "Selected air flow", "%", 10),
                           ("time_to_stop", "time_to_stop_ds", "Time to stop", None, 10)]
        for key, field, name, unit, divisor in fields:
            raw = core.get(prefix + field)
            value = divided(raw, divisor) if unit else raw
            if key in ("countdown", "time_to_stop"):
                value = duration(raw, divisor)
            known = value is not None and value != "-" and not (isinstance(raw, (int, float)) and raw < 0)
            yield Spec(uid, prefix + key, "sensor", f"Port {index} {name}", value, ready and known, unit)
    # Station-wide values share MQTT's core projection, retaining prior HA IDs.
    station_fields = {
        "CL": [("cleaning_mode", "jbc_cleaner_mode", "Cleaner mode", "sensor", None, 1),
               ("cl_motors_on", "jbc_motors", "Motors", "binary_sensor", None, 1),
               ("cl_door_open", "jbc_door_open", "Door open", "binary_sensor", None, 1)],
        "PH": [("heater", "jbc_heater", "Heater", "binary_sensor", None, 1),
               ("ph_selected_power", "jbc_selected_power_permille", "Selected power", "sensor", "%", 10),
               ("ph_active_zones", "jbc_active_zones", "Active zones", "sensor", None, 1),
               ("time_to_stop", "jbc_time_to_stop_ds", "Time to stop", "sensor", None, 10)],
        "FE": [("suction_level", "jbc_suction_level", "Suction level", "sensor", None, 1),
               ("fe_intake_work", "jbc_intake_work", "WORK intake", "binary_sensor", None, 1),
               ("fe_intake_stand", "jbc_intake_stand", "STAND intake", "binary_sensor", None, 1)],
        "SF": [("sf_feeding_state", "jbc_feeding", "Feeding", "binary_sensor", None, 1),
               ("sf_feeding_selected_program", "jbc_program", "Program", "sensor", None, 1),
               ("sf_speed_tenth_mm_s", "jbc_speed_tenth_mm_s", "Speed", "sensor", "mm/s", 10),
               ("sf_length_tenth_mm", "jbc_length_tenth_mm", "Length", "sensor", "mm", 10),
               ("sf_tool_enabled", "jbc_tool_enabled", "Tool enabled", "binary_sensor", None, 1)],
    }
    for key, field, name, platform, unit, divisor in station_fields.get(family, []):
        raw = core.get(field)
        value = divided(raw, divisor) if unit else raw
        if key == "time_to_stop":
            value = duration(raw, divisor)
        known = raw is not None and not (isinstance(raw, (int, float)) and raw < 0)
        yield Spec(uid, "jbc_p1_" + key, platform, name, value, valid and known, unit)
    if family == "FE":
        yield Spec(uid, "jbc_continuous", "binary_sensor", "Continuous suction", core.get("jbc_continuous"), valid)
    if family == "PH":
        for index in range(1, 5):
            for key, field, name in (("actual_temp", "temp_c", "Temperature"), ("selected_temp", "set_temp_c", "Selected temperature")):
                value = core.get(f"jbc_tc{index}_{field}")
                yield Spec(uid, f"jbc_tc{index}_{key}", "sensor", f"TC{index} {name}", value,
                           valid and value is not None and value >= 0, "\u00b0C", diagnostic=key == "selected_temp")
    projected = {key: value for key, value in core.items()
                 if key.startswith("jbc_p") and jbc_counter_metadata(key.split("_", 2)[-1])}
    if not projected and "controls" not in core:
        # Compatibility with masters predating the shared counter projection.
        for index, port in enumerate(module.get("jbc_usb_ports", [])[:count], 1):
            for key, value in port.items():
                if jbc_counter_metadata(key) and jbc_counter_matches_family(key, family):
                    projected[f"jbc_p{index}_{key}"] = value
    for key, raw in projected.items():
        index = int(key.split("_")[1][1:])
        if not 1 <= index <= count or not jbc_counter_matches_family(key.split("_", 2)[2], family):
            continue
        labels, unit = jbc_counter_metadata(key.split("_", 2)[2])
        try:
            value = int(raw)
        except (TypeError, ValueError, OverflowError):
            value = None
        yield Spec(uid, key, "sensor", f"Port {index} {labels[1]}", filter_time(value) if unit else value,
                   valid and bool(core.get(f"jbc_p{index}_valid")) and value is not None,
                   diagnostic=True, icon="mdi:timer-outline" if unit else "mdi:counter",
                   attributes={"counter_minutes": value} if unit and value is not None else {})


def module_specs(uid: str, module: dict, online: bool, definitions: list):
    typ, caps = module.get("type"), int(module.get("caps", 0))
    if typ in (1, 9) or caps & ((1 << 0) | (1 << 24)):
        yield from jbc_specs(uid, module, online)
    if typ == 6:
        valid = online and bool(module.get("telemetry_valid"))
        for key, argument, name, options, values in (
            ("display_language", "language", "Language", ("English", "Deutsch"), ("0", "1")),
            ("display_theme", "theme", "Theme", ("Dark", "Light"), ("0", "1")),
            ("display_screensaver_min", "screensaver", "Screensaver", ("Off", "1 min", "2 min", "5 min", "10 min"), ("0", "1", "2", "5", "10")),
        ):
            raw = str(module.get(key))
            value = options[values.index(raw)] if raw in values else None
            yield Spec(uid, key, "select", name, value, valid, action="display", argument=argument,
                       options=options, option_values=values)
    if typ == 5:
        age = numeric(module.get("weller_uart_age_sec"))
        linked = age is not None and 0 <= age <= 10
        yield Spec(uid, "weller_link", "binary_sensor", "Weller connected", linked, online)
        valid = online and linked
        yield Spec(uid, "weller_speed", "number", "Fan power", module.get("weller_speed_percent"), valid,
                   "%", "weller", "speed", minimum=30)
        yield Spec(uid, "weller_fan", "switch", "Fan", bool(int(module.get("io_output_mask", 0)) & 1), valid,
                   action="io", params={"bit": 0})
        yield Spec(uid, "weller_light", "switch", "Work light", bool(int(module.get("io_output_mask", 0)) & 2 or module.get("weller_work_light")), valid,
                   action="io", params={"bit": 1})
        yield Spec(uid, "weller_rpm", "sensor", "Fan RPM", module.get("weller_fan_rpm"), online, "rpm")
        yield Spec(uid, "weller_filter", "sensor", "Filter", {1: "Very good", 10: "Change soon", 100: "Change filter"}.get(module.get("weller_filter_status")), online)
        yield Spec(uid, "reset_filter", "button", "Reset filter operating time", valid=valid,
                   action="weller", params={"reset_filter": 1})
        minutes = tuple([0] + [h * 60 for h in range(1, 24)] + [d * 1440 for d in range(1, 7)] + [9600])
        options = ("Off",) + tuple(filter_time(value) for value in minutes[1:])
        interval = numeric(module.get("weller_programmed_filter_minutes"))
        yield Spec(uid, "filter_interval", "select", "Filter replacement interval", "Off" if interval == 0 else filter_time(interval), online,
                   action="weller", argument="filter_minutes", options=options, option_values=tuple(map(str, minutes)))
        prefix, valid_filter = "weller", online
    elif typ == 3 and module.get("fanio_filter_mode") in (1, 3):
        prefix, valid_filter = "fanio", online and bool(module.get("fanio_filter_runtime_valid"))
        yield Spec(uid, "reset_filter", "button", "Reset filter operating time", valid=online,
                   action="fanio_reset_filter")
    else:
        prefix, valid_filter = None, False
    if prefix:
        runtime = numeric(module.get(prefix + "_filter_runtime_minutes"))
        lifetime = numeric(module.get(prefix + ("_programmed_filter_minutes" if prefix == "weller" else "_filter_lifetime_minutes")))
        remaining = max(0, lifetime - runtime) if runtime is not None and lifetime is not None else None
        for key, name, value in (("filter_operating_time", "Filter operating time", runtime),
                                  ("filter_lifetime", "Filter replacement interval", lifetime),
                                  ("filter_remaining", "Filter remaining time", remaining)):
            if key == "filter_operating_time" and any(d.get("key") == "filter_runtime" for d in definitions):
                continue
            yield Spec(uid, key, "sensor", name, filter_time(value), valid_filter)
    if typ == 3 and module.get("fanio_filter_mode") in (1, 2, 3):
        yield Spec(uid, "filter_mode", "sensor", "Filter mode",
                   {1: "Runtime only", 2: "Pressure only", 3: "Runtime + pressure"}[module["fanio_filter_mode"]], online)
        if module.get("fanio_filter_mode") in (2, 3):
            for key, field, name, divisor, unit in (
                ("filter_pressure", "fanio_filter_pressure_raw", "Filter pressure", 1, None),
                ("filter_saturation", "fanio_filter_saturation_permille", "Filter saturation", 10, "%"),
            ):
                if not any(d.get("key") == key for d in definitions):
                    yield Spec(uid, key, "sensor", name, divided(module.get(field), divisor), online, unit)
    if typ in (2, 3) and not definitions and not module.get("universal_descriptor_valid"):
        for bit in range(2):
            yield Spec(uid, f"in{bit + 1}", "binary_sensor", module.get(f"io_in{bit + 1}_alias") or f"IN{bit + 1}",
                       bool(int(module.get("io_input_mask", 0)) & (1 << bit)), online)
            yield Spec(uid, f"out{bit + 1}", "switch", module.get(f"io_out{bit + 1}_alias") or f"OUT{bit + 1}",
                       bool(int(module.get("io_output_mask", 0)) & (1 << bit)), online, action="io", params={"bit": bit})
    if typ in (2, 3) and caps & ((1 << 2) | (1 << 5)):
        valid = online and bool(module.get("output_status_valid"))
        roles = {d.get("meta", {}).get("role") for d in definitions}
        if "main_output_enable" not in roles:
            yield Spec(uid, "main_output", "switch", module.get("io_main_alias") or "Main output", module.get("module_output_enabled"), valid,
                       action="output", argument="enable")
        if caps & (1 << 2) and "main_output_power" not in roles:
            yield Spec(uid, "main_power", "number", "Main output power", divided(module.get("module_output_power"), 10), valid,
                       "%", "output", "power", minimum=10)
        if caps & (1 << 3) and not any(d.get("key") == "rpm" for d in definitions):
            yield Spec(uid, "rpm", "sensor", "Fan RPM", module.get("module_output_rpm"), valid, "rpm")
        yield Spec(uid, "fault", "sensor", "Output fault", module.get("combined_fault_text"), valid)


def profile_spec(uid: str, definition: dict, entity: dict | None, valid: bool) -> Spec | None:
    typ = definition.get("type")
    if typ not in ("sensor", "text", "binary_sensor", "switch", "number", "select", "button"):
        return None
    meta = definition.get("meta", {})
    mode = str(meta.get("access") or definition.get("mode", "ro")).lower()
    entity = entity or {}
    raw = entity.get("ascii")
    shown = entity.get("display_text", raw)
    writable = "w" in mode
    readable = "r" in mode
    platform = ("text" if writable else "sensor") if typ == "text" else typ
    if typ == "number" and not writable:
        platform = "sensor"
    if typ == "switch" and not writable:
        platform = "binary_sensor"
    if typ == "select" and not writable:
        platform = "sensor"
    if typ == "button" and not writable:
        return None
    value = shown
    unit = meta.get("unit") or None
    time_display = str(meta.get("time_display") or meta.get("tf") or "").lower()
    time_base = str(meta.get("time_base") or meta.get("tb") or "").lower()
    duration_text = time_display == "dhm" and time_base not in ("", "none")
    if typ in ("switch", "binary_sensor"):
        value = entity.get("value_bool")
    elif platform == "number" or (typ in ("sensor", "number") and not duration_text):
        value = numeric(raw)
    elif duration_text:
        unit = None
    options = tuple(str(meta.get("options", "")).split("|")) if meta.get("options") else ()
    values = tuple(str(meta.get("values", "")).split("|")) if meta.get("values") else options
    if len(options) != len(values) or len(set(options)) != len(options):
        writable = False
        if platform == "select":
            platform = "sensor"
    if typ == "select" and "selected_value" in entity:
        try:
            value = options[values.index(str(entity["selected_value"]))]
        except (ValueError, IndexError):
            value = None
    elif typ == "select" and shown not in options:
        value = None if platform == "select" else shown
    lo, hi, step = (numeric(meta.get(k, default)) for k, default in (("min", 0), ("max", 100), ("step", 1)))
    if typ == "number" and (lo is None or hi is None or step is None or lo > hi or step <= 0):
        writable = False
        platform = "sensor"
    return Spec(
        uid, "entity_" + str(definition["id"]) + "_" + str(definition.get("key", "")), platform,
        str(definition.get("label") or definition.get("key") or definition["id"]),
        value, valid and (not readable or bool(entity.get("len", 0))), unit,
        "entity" if writable else None, params={"id": definition["id"]},
        options=options, option_values=values, minimum=lo or 0, maximum=hi or 0, step=step or 1,
        icon=(str(meta["icon"]) if str(meta.get("icon", "")).startswith("mdi:") else
              "mdi:fan" if meta.get("role") in ("main_output_enable", "main_output_power") else
              "mdi:timer-outline" if duration_text and platform != "number" else None),
    )


def led_specs(uid: str, data: dict, master: bool, enabled: bool, german: bool):
    """Mirror WebStatus's LED event styles, using reported telemetry only."""
    styles = (
        ("Aus", "Off", "#59616b", "off", 0),
        ("Bus online", "Bus online", "#00ff00", "greenwhite", 3200),
        ("Busaktivität", "Bus activity", "#00ff00", "solid", 0),
        ("Bus offline", "Bus offline", "#ff0000", "breath", 1600),
        ("Nicht gekoppelt", "Not paired", "#ff8c00", "blink", 1000),
        ("Firmware-Update", "Firmware update", "#0024ff", "bluewhite", 3200),
        ("Gerät verbunden", "Device online", "#ffffff", "whitebreath", 3200),
        ("Gerät getrennt", "Device offline", "#ff0000", "breath", 1600),
        ("Work aktiv", "Work active", "#00ff00", "solid", 0),
        ("Absaugung aktiv", "Extractor on", "#0046ff", "breath", 3200),
        ("Nachlauf", "Afterrun", "#aa00ff", "blink", 1300),
        ("Dauerbetrieb", "Continuous", "#0046ff", "blink", 1000),
        ("Warnung", "Warning", "#ffaa00", "blink", 1000),
        ("Kritischer Fehler", "Critical", "#ff0000", "double", 900),
    )
    prefix = "master_led_" if master else "led_"
    valid = master or bool(data.get("online") and data.get("led_status_valid"))
    eco = bool(data.get("power_save_active" if master else "eco_mode"))
    for channel in ("ofe", "evt"):
        field = prefix + channel + "_event"
        if field not in data:
            continue
        event = data[field]
        known = isinstance(event, int) and not isinstance(event, bool) and 0 <= event < len(styles)
        name_de, name_en, color, effect, period = styles[event if known else 0]
        eco_led = known and channel == "ofe" and eco and event in (1, 2)
        if eco_led:
            name_de, name_en, color, effect, period = "Energiesparen", "Power saving", "#9600ff", "purplewhite", 3200
        if not enabled:
            name_de, name_en, color, effect, period = styles[0]
        yield Spec(uid, "led_" + channel, "sensor", channel.upper() + " LED",
                   name_de if german else name_en, valid and known, diagnostic=True, icon="mdi:led-on",
                   attributes={"ofe_led_event": event if known else None, "ofe_led_color": color,
                               "ofe_led_effect": effect, "ofe_led_period_ms": period,
                               "ofe_led_enabled": enabled, "ofe_led_eco": eco_led})


def build_specs(master_uid: str, state: dict, definitions: dict[str, list]) -> dict[str, Spec]:
    specs: dict[str, Spec] = {}

    def add(spec: Spec) -> None:
        if not state.get("developer_mode", False) and spec.key in (
                "heap_free", "heap_min", "loop_max_ms", "module_heap_free", "module_heap_min", "module_loop_max_ms"):
            return
        presentation(spec)
        mqtt_presentation(spec, state, master_uid)
        if spec.uid == master_uid and spec.key in ("alarm_count", "alarm_text"):
            alarms = state.get("alarms", [])
            spec.attributes.update({
                "alarms": [alarm for alarm in alarms if isinstance(alarm, str) and alarm] if isinstance(alarms, list) else [],
                "alarm_count": int(numeric(state.get("alarm_count")) or 0),
            })
        specs[spec.id] = spec

    led_enabled = bool(state.get("status_led_enabled", True))
    for spec in led_specs(master_uid, state, True, led_enabled, state.get("ui_lang") == "de"):
        add(spec)
    for key, name, unit in (("output_power", "Requested power", "%"), ("alarm_count", "Active alarms", None),
                            ("cpu_load_pct", "CPU", "%"), ("wifi_rssi", "Wi-Fi signal", "dBm")):
        value = numeric(state.get(key))
        if key == "output_power" and value is not None:
            value /= 10
        add(Spec(master_uid, key, "sensor", name, value, unit=unit,
                 diagnostic=key in ("cpu_load_pct", "wifi_rssi")))
    add(Spec(master_uid, "afterrun", "sensor", "Afterrun", uptime_text(state.get("afterrun_ms", 0), 1000)))
    alarm_messages = state.get("alarms")
    alarm_text = ("; ".join(item for item in alarm_messages if isinstance(item, str) and item) or "OK"
                  if isinstance(alarm_messages, list) else state.get("alarm_text", "OK"))
    add(Spec(master_uid, "alarm_text", "sensor", "Alarm text", alarm_text[:255] if isinstance(alarm_text, str) else None))
    for key, name in (("status_led_enabled", "Status LEDs"), ("status_led_brightness", "Status LED brightness")):
        if key in state:
            add(Spec(master_uid, key, "switch" if key.endswith("enabled") else "number", name, state[key],
                     unit=None if key.endswith("enabled") else "%", action="status_led",
                     params={"setting": "enabled" if key.endswith("enabled") else "brightness"}, minimum=10))
    for key, route in state.get("ha_routes", {}).items():
        if key in ("main_input", "main_output") and isinstance(route, dict):
            options = tuple(dict.fromkeys(route.get("options", [])))
            add(Spec(master_uid, key, "select", "Main input" if key == "main_input" else "Main extractor output",
                     route.get("value"), action=key, options=options, option_values=options))
    if "mqtt_state" in state:
        add(Spec(master_uid, "mqtt_state_code", "sensor", "MQTT State Code", state["mqtt_state"], diagnostic=True))
    for key, name in (("output_enabled", "Extraction active"), ("power_save_active", "Power saving")):
        add(Spec(master_uid, key, "binary_sensor", name, state.get(key)))
    for key, name in (("continuous", "Continuous"), ("stand_intakes", "Stand intakes")):
        value = state.get(key) if key == "continuous" else bool(state.get("jbc", {}).get(key))
        add(Spec(master_uid, key, "switch", name, value, action="extractor", argument=key))
    jbc = state.get("jbc", {})
    levels = ("High", "Medium", "Low", "Custom")
    level = jbc.get("suction_level")
    add(Spec(master_uid, "level", "select", "Suction level", levels[level] if isinstance(level, int) and 0 <= level < 4 else None,
             action="extractor", argument="suction", options=levels, option_values=("0", "1", "2", "3")))
    for key, name, argument, value, minimum, maximum, unit in (
        ("custom_power", "Custom power", "select", jbc.get("select_flow", 0) / 10,
         state.get("afterrun_power_min", 0) / 10, state.get("afterrun_power_max", 1000) / 10, "%"),
        ("delay_work", "Work afterrun", "delay_work", jbc.get("delay_work_sec"), 0, 3600, "s"),
        ("delay_stand", "Stand afterrun", "delay_stand", jbc.get("delay_stand_sec"), 0, 3600, "s"),
    ):
        add(Spec(master_uid, key, "number", name, value, unit=unit, action="extractor", argument=argument, minimum=minimum, maximum=maximum))
    add(Spec(master_uid, "restart", "button", "Restart", action="restart", diagnostic=True))
    add(Spec(master_uid, "serial_number", "sensor", "Serial number", master_uid, diagnostic=True))
    for key, name, unit, divisor in (("uptime_ms", "Uptime", None, 1000),
                                     ("heap_free", "Free heap", "KiB", 1024),
                                     ("heap_min", "Minimum free heap", "KiB", 1024),
                                     ("loop_max_ms", "Maximum loop time", "ms", 1),
                                     ("modules_count", "Modules", None, 1)):
        if key in state:
            value = uptime_text(state[key], divisor) if key == "uptime_ms" else divided(state[key], divisor)
            add(Spec(master_uid, key, "sensor", name, value, unit=unit, diagnostic=True))
    for key, name in (("master_fw", "Firmware"), ("master_ip", "IP address"),
                      ("datetime", "Date and time"), ("mqtt_state_text", "MQTT status")):
        if key in state:
            add(Spec(master_uid, key, "sensor", name, state[key], diagnostic=True))
    if "work_mask" in state:
        add(Spec(master_uid, "work", "binary_sensor", "Work", bool(state["work_mask"])))
    for key, name in (("stat_error", "JBC error"), ("stat_error_text", "JBC error text")):
        if key in jbc:
            add(Spec(master_uid, key, "sensor", name, jbc[key]))
    add(Spec(master_uid, "afterrun_power_enabled", "switch", "Afterrun power profile", state.get("afterrun_power_enabled"),
             action="extractor", argument="afterrun_power_enabled"))
    add(Spec(master_uid, "afterrun_power", "number", "Afterrun power", divided(state.get("afterrun_power"), 10), unit="%",
             action="extractor", argument="afterrun_power", minimum=state.get("afterrun_power_min", 0) / 10,
             maximum=state.get("afterrun_power_max", 1000) / 10))
    feedback = state.get("output_module", {})
    for key, name, unit, divisor in (("rpm", "Fan RPM", "rpm", 1), ("power", "Output power feedback", "%", 10)):
        if key in feedback:
            add(Spec(master_uid, "feedback_" + key, "sensor", name, divided(feedback[key], divisor), bool(feedback.get("valid")), unit))
    if "fault_text" in feedback:
        add(Spec(master_uid, "output_fault", "sensor", "Output fault", feedback["fault_text"], bool(feedback.get("valid"))))
    for module in state.get("modules", []):
        uid = str(module.get("uid", "")).upper()
        if not uid or uid == "0000000000000000":
            continue
        online = bool(module.get("online"))
        telemetry = online and bool(module.get("telemetry_valid"))
        for spec in led_specs(uid, module, False, led_enabled, state.get("ui_lang") == "de"):
            add(spec)
        add(Spec(uid, "online", "binary_sensor", "Connected", online, diagnostic=True))
        add(Spec(uid, "serial_number", "sensor", "Serial number", uid, online, diagnostic=True))
        add(Spec(uid, "eco", "binary_sensor", "Power saving", module.get("eco_mode"), valid=online))
        for key, name, unit, divisor in (("module_cpu_load_pct", "CPU", "%", 1), ("module_uptime_s", "Uptime", None, 1),
                                       ("module_heap_free", "Free heap", "KiB", 1024), ("module_heap_min", "Minimum free heap", "KiB", 1024),
                                       ("module_loop_max_ms", "Maximum loop time", "ms", 1)):
            value = uptime_text(module.get(key), divisor) if key == "module_uptime_s" else divided(module.get(key), divisor)
            add(Spec(uid, key, "sensor", name, value, telemetry, unit, diagnostic=True))
        for key, name in (("fw", "Firmware"), ("transport", "Transport")):
            if key in module:
                add(Spec(uid, key, "sensor", name, module[key], online, diagnostic=True))
        add(Spec(uid, "reboot", "button", "Restart", valid=online, action="module_reboot", diagnostic=True))
        if module.get("type") == 6:
            add(Spec(uid, "brightness", "number", "Brightness", module.get("display_brightness_pct"), telemetry,
                     "%", "display", "brightness", minimum=10, maximum=100))
        for spec in module_specs(uid, module, online, definitions.get(uid, [])):
            add(spec)
        values = {e["id"]: e for e in module.get("universal_entities", []) if "id" in e}
        for definition in definitions.get(uid, []):
            if (module.get("type") == 3 and definition.get("key") == "pressure"
                    and definition.get("id") == 63):
                # This hardware reading already has the stable filter_pressure entity.
                continue
            if (module.get("type") == 3 and definition.get("key") == "filter_mode"
                    and module.get("fanio_filter_mode") == 0):
                continue
            if (module.get("type") == 3 and definition.get("key") == "filter_runtime"
                    and module.get("fanio_filter_mode") in (0, 2)):
                continue
            if (module.get("type") == 3 and definition.get("key") in ("filter_pressure", "filter_saturation")
                    and module.get("fanio_filter_mode") in (0, 1)):
                continue
            spec = profile_spec(uid, definition, values.get(definition["id"]), online and bool(module.get("universal_entities_valid")))
            if spec:
                if module.get("type") == 3:
                    leaf = definition.get("key")
                    if leaf == "filter_runtime":
                        spec.value, spec.unit = filter_time(module.get("fanio_filter_runtime_minutes")), None
                        spec.valid = online and bool(module.get("fanio_filter_runtime_valid"))
                    elif leaf == "filter_pressure":
                        spec.value = numeric(module.get("fanio_filter_pressure_raw"))
                    elif leaf == "filter_saturation":
                        spec.value = divided(module.get("fanio_filter_saturation_permille"), 10)
                    elif leaf == "filter_mode":
                        specs.pop(uid + ":filter_mode", None)
                add(spec)
    return specs
