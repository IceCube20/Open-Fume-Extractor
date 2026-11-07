"""Protect an opt-in, GUI-owned RGB/OTA diagnostic, not a new production clock."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
S3 = ROOT / "Module/DisplayModule_800x480"


class RgbOtaProbeSourceTests(unittest.TestCase):
    def test_probe_changes_only_panel_clock_and_only_from_gui_service(self):
        source = (S3 / (S3.name + ".ino")).read_text(encoding="utf-8-sig")
        self.assertIn("#define DISPLAY_RGB_PCLK_HZ 16000000", source)
        self.assertIn("static OfeRgbOtaProbe rgb_ota_probe(DISPLAY_RGB_PCLK_HZ);", source)
        self.assertIn("esp_lcd_rgb_panel_set_pclk(panel_, hz)", source)
        gui = source.split("static void process_display_ui_once() {", 1)[1].split("void setup()", 1)[0]
        self.assertIn("rgb_ota_probe.service(millis(), fw_update_active.load()", gui)
        self.assertIn("gfx->requestDiagnosticClock(hz)", gui)
        cli = source.split("static void serial_cli_execute(char* command) {", 1)[1].split("static void serial_cli_feed", 1)[0]
        self.assertIn('!strncmp(command, "rgbprobe ", 9)', cli)
        self.assertIn('!rgb_ota_probe.select(argument)', cli)
        self.assertIn('Usage: rgbprobe [15|14|13|normal]', cli)
        self.assertNotIn("requestDiagnosticClock", cli)
        self.assertNotIn("esp_lcd_rgb_panel_set_pclk", cli)

    def test_actual_wifi_state_is_read_only_and_probe_has_no_persistence(self):
        source = (S3 / (S3.name + ".ino")).read_text(encoding="utf-8-sig")
        driver = source.split("static void serial_cli_print_wifi_driver() {", 1)[1].split("#endif", 1)[0]
        self.assertIn("esp_wifi_get_ps(&actual)", driver)
        self.assertNotIn("esp_wifi_set_ps", driver)
        self.assertNotIn("setSleep", driver)
        helper = (S3 / "src/OfeRgbOtaProbe.h").read_text()
        self.assertIn("selected_hz_{0}", helper)
        self.assertNotIn("6000000", helper)
        self.assertNotIn("Preferences", helper)
        self.assertNotIn("malloc", helper)
        self.assertNotIn("new ", helper)

    def test_runtime_scan_reports_follow_accepted_clock_requests(self):
        source = (S3 / (S3.name + ".ino")).read_text(encoding="utf-8-sig")
        self.assertEqual(source.count("const float scan_hz = (float)display_requested_pclk_hz() /"), 2)
        self.assertIn("(double)display_requested_pclk_hz() /", source)
        body = source.split("static uint32_t display_requested_pclk_hz() {", 1)[1].split("\n}", 1)[0]
        self.assertIn("#if defined(CONFIG_IDF_TARGET_ESP32S3)", body)
        self.assertIn("return rgb_ota_probe.requestedHz();", body)
        self.assertIn("#else\n  return DISPLAY_RGB_PCLK_HZ;", body)

    def test_generator_keeps_the_p4_clock_report_helper(self):
        folder = ROOT / "Module/DisplayModule_P4_800x480"
        source = (folder / (folder.name + ".ino")).read_text(encoding="utf-8-sig")
        definition = source.index("static uint32_t display_requested_pclk_hz() {")
        first_report = source.index("(double)display_requested_pclk_hz() /")
        self.assertLess(definition, first_report)
        body = source[definition:source.index("\n}", definition)]
        self.assertIn("#else\n  return DISPLAY_RGB_PCLK_HZ;", body)
        self.assertIn("#define DISPLAY_RGB_PCLK_HZ 24000000", source)


if __name__ == "__main__":
    unittest.main()
