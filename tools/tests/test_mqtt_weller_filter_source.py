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
TYPOGRAPHY = (ROOT / "OpenFumeExtractorMaster/src/WebTypography.h").read_text(
    encoding="utf-8"
)


class WellerMqttFilterTests(unittest.TestCase):
    def test_weller_off_reaches_module_and_preserves_display_wire_indices(self):
        module = (ROOT / "Module/WellerZeroSmogModule/WellerZeroSmogModule.ino").read_text(encoding="utf-8")
        command = module.split("static void weller_set_filter_runtime(uint16_t minutes) {", 1)[1].split("static void weller_send_light", 1)[0]
        self.assertRegex(command, r"(?s)if \(minutes == 0\).*?weller_send_checked4\('f', 0\);.*?weller_send_checked4\('g', 0\);")
        scheduler = (ROOT / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")
        setter = scheduler.split("bool MasterScheduler::setWellerFilterRuntime(", 1)[1].split("bool MasterScheduler::calibrateFanIoProFilter", 1)[0]
        self.assertIn("minutes != 0 && minutes < 60U", setter)
        event = scheduler.split("if (type == DISPLAY_EVENT_WELLER_FILTER_TIME_SET)", 1)[1].split("if (type == DISPLAY_EVENT_IO_OUT_TOGGLE)", 1)[0]
        self.assertIn("preset > 30", event)
        self.assertIn("preset == 29", event)
        self.assertNotIn("if (minutes)", event)
        for name in ("DisplayModule_320x480", "DisplayModule_800x480", "DisplayModule_ST7796_320x480"):
            display = (ROOT / f"Module/{name}/{name}.ino").read_text(encoding="utf-8")
            self.assertIn(r'"Off\n1 hour\n2 hours', display)
            self.assertIn(r'"Aus\n1 Stunde\n2 Stunden', display)
            self.assertIn("if (minutes == 0) return 0;", display)
            self.assertIn("selected == 0 ? 30 : (uint8_t)(selected - 1U)", display)
            self.assertIn('#include "src/OfeDisplayControlsUi.inc.h"', display)
            controls = (ROOT / f"Module/{name}/src/OfeDisplayControlsUi.inc.h").read_text(encoding="utf-8")
            self.assertIn("lv_dropdown_set_options(ui_detail_weller_filter_dropdown, weller_filter_options());", controls)

    def test_weller_off_is_exposed_by_web_and_mqtt(self):
        self.assertIn("weller_filter_off_v1", MQTT)
        self.assertIn('mqtt_txt("Aus", "Off")', MQTT)
        self.assertIn('value == "Off" || value == "Aus"', MQTT)
        self.assertIn("master_cmd_set_weller_filter_runtime(addr, 0)", MQTT)
        self.assertIn("if(total===null)return", WEB)
        self.assertNotIn("if(total<=0)return", WEB.split("async function wellerFilter(addr)", 1)[1].split("let stateLoadBusy", 1)[0])
        controls = (ROOT / "OpenFumeExtractorMaster/src/WebControls.inc.h").read_text(encoding="utf-8")
        setter = controls.split("if (web.hasArg(\"filter_minutes\"))", 1)[1].split("static void web_handle_display_set", 1)[0]
        self.assertIn("minutes != 0 && minutes < 60UL", setter)
        self.assertIn("invalid filter minutes", setter)

    def test_cached_weller_filter_is_not_erased_or_used_as_current_alarm(self):
        module = (ROOT / "Module/WellerZeroSmogModule/WellerZeroSmogModule.ino").read_text(encoding="utf-8")
        faults = module.split("static void update_weller_faults()", 1)[1].split("static void handle_status", 1)[0]
        self.assertNotIn("filter_status_code = 0", faults)
        self.assertIn("fault_mask &= (uint16_t)~0x0006U", faults)
        scheduler = (ROOT / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")
        alarms = scheduler.split("uint16_t MasterScheduler::systemJbcError()", 1)[1].split("uint16_t MasterScheduler::minSelectFlow", 1)[0]
        self.assertNotIn("out->weller_filter_status", alarms)
        self.assertIn("module_filter_fault_current(*out)", alarms)
        self.assertIn("Last received filter status (Weller disconnected)", WEB)
        availability = MQTT.split("static String mqtt_module_availability_condition(", 1)[1].split("static void mqtt_publish_module_entity(", 1)[0]
        self.assertNotIn('!strcmp(suffix, "filter")', availability)

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
        self.assertIn(".module-card .filter-actions button", TYPOGRAPHY)
        self.assertIn("font-size:13px;font-weight:700;line-height:1.25", TYPOGRAPHY)
        self.assertNotIn("button.dataset.fanioStyle", WEB)
        self.assertNotIn("button.style.fontFamily", WEB)
        self.assertIn("interval.style.order='2'", WEB)
        self.assertIn("interval.appendChild(select)", WEB)
        self.assertIn("groups[0].appendChild(swFact)", WEB)
        self.assertIn("['wfstat_','wfprog_','wfrun_','wfremain_']", WEB)
        self.assertIn("caption.style.marginBottom='10px'", WEB)


if __name__ == "__main__":
    unittest.main()
