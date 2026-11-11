"""Integration contracts for the shared system-signal feature."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
MASTER = ROOT / "OpenFumeExtractorMaster/src"
NAMES = ("DisplayModule_320x480", "DisplayModule_800x480",
         "DisplayModule_ST7796_320x480", "DisplayModule_P4_800x480")


class SystemAutomationSourceTests(unittest.TestCase):
    def test_backlight_implementation_is_shared_by_all_displays(self):
        headers = [(ROOT / "Module" / name / "src/OfeDisplayBacklight.h").read_bytes() for name in NAMES]
        self.assertTrue(all(value == headers[0] for value in headers))

    def test_capability_and_gui_ownership(self):
        for name in NAMES:
            with self.subTest(display=name):
                source = (ROOT / "Module" / name / (name + ".ino")).read_text(encoding="utf-8-sig")
                self.assertGreaterEqual(source.count("CAP_DISPLAY | CAP_DISPLAY_AUTOMATION |"), 3)
                self.assertEqual(source.count("display_backlight.applyAutomationFrame(req.payload, req.len, millis());"), 1)
                service = source.split("static void process_display_ui_once() {", 1)[1].split("\nvoid setup()", 1)[0]
                self.assertEqual(service.count("if (display_backlight.takeAutomationWake()) screensaver_wake();"), 1)

    def test_protocol_capability_is_consistent(self):
        paths = list((ROOT / "Module").glob("*/src/Rs485PeripheralBus.h"))
        paths.append(MASTER / "bus/Rs485PeripheralBus.h")
        for path in paths:
            self.assertIn("CAP_DISPLAY_AUTOMATION = 1UL << 29", path.read_text())

    def test_both_editors_load_the_same_catalogue(self):
        for name in ("WebStatus.inc.h", "WebLogic.inc.h"):
            source = (MASTER / name).read_text()
            self.assertIn('#include "WebAutomationSignals.h"', source)
            self.assertIn("OFE_WEB_AUTOMATION_SCRIPT", source)
            self.assertIn("ofeRoutingSignals(", source)
        logic = (MASTER / "WebLogic.inc.h").read_text()
        self.assertIn("ofeRoutingSignals(d,logicDe()?'de':'en'", logic)
        self.assertIn("ofeAutomationInputInfo(d,signal)", logic)

    def test_runtime_requests_are_not_saved_settings(self):
        source = (MASTER / "MasterScheduler.cpp").read_text()
        implementation = source.split("void MasterScheduler::setLogicSystemOutput(", 1)[1].split("uint32_t MasterScheduler::systemInputMask", 1)[0]
        self.assertNotIn("Preferences", implementation)
        self.assertNotIn("setPowerSaveConfig", implementation)
        self.assertNotIn("setDisplaySettings", implementation)
        self.assertIn("if (automationEcoInhibited() ||", source)
        self.assertIn("rule_system_requests_ = system_requests;", source)

    def test_state_checks_and_status_transport_are_bounded(self):
        source = (MASTER / "MasterScheduler.cpp").read_text()
        self.assertIn("uint32_t(now - system_alarm_sample_ms_) >= 50U", source)
        self.assertIn("uint32_t(now - last_system_rule_update_ms_) >= 50U", source)
        self.assertIn("const uint16_t live_tail_reserve = 14U", source)
        self.assertIn("if (payload_can_write(14U))", source)
        self.assertIn("payload_write_u8(0xA3);", source)
        self.assertIn("display->display_automation_wake_sequence", source)

    def test_bad_filter_samples_are_gated(self):
        source = (MASTER / "MasterScheduler.cpp").read_text()
        implementation = source.split("uint32_t MasterScheduler::systemInputMask(", 1)[1].split("bool MasterScheduler::systemInputActive", 1)[0]
        self.assertIn("!rec.online", implementation)
        self.assertIn("rec.telemetry_last_ms", implementation)
        self.assertIn("MODULE_WELLER_ZERO_SMOG && connected", implementation)
        self.assertIn("if (valid)", implementation)


if __name__ == "__main__":
    unittest.main()
