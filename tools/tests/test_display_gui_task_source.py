import hashlib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("DisplayModule_320x480", "DisplayModule_800x480",
         "DisplayModule_ST7796_320x480", "DisplayModule_P4_800x480")


class DisplayGuiTaskSourceTests(unittest.TestCase):
    def test_shared_task_helper(self):
        headers = [(ROOT / "Module" / name / "src/OfeDisplayGuiTask.h").read_bytes()
                   for name in NAMES]
        self.assertTrue(all(header == headers[0] for header in headers))

    def test_single_ui_owner_and_fallback(self):
        for name in NAMES:
            with self.subTest(display=name):
                source = (ROOT / "Module" / name / (name + ".ino")).read_text(encoding="utf-8-sig")
                service = source.split("static void process_display_ui_once() {", 1)[1].split("\nvoid setup()", 1)[0]
                setup, loop = source.split("void setup() {", 1)[1].split("void loop() {", 1)
                self.assertIn("if (!display_gui.running()) process_display_ui_once();", loop)
                for call in ("lvgl_timer_handler_profiled();", "apply_deferred_ui_updates();",
                             "lvgl_flush_canvas_if_dirty(fw_update_active);", "screensaver_tick();",
                             "display_benchmark_tick();", "lv_update_ble_indicator_ui();"):
                    self.assertIn(call, service)
                    self.assertNotIn(call, loop)
                self.assertLess(setup.index("start_rs485_task_if_enabled();"),
                                setup.index("display_gui.begin(rs485_task_started, lvgl_ready, process_display_ui_once);"))
                self.assertIn("if (display_gui.needsDefer()) return true;", source)
                self.assertNotIn("running_in_rs485_task()", source)
                self.assertIn("display_gui.takeMaxBusyUs()", source)
                self.assertIn("OfeDisplayGuiTask::DiagnosticLock ui_lock(display_gui, ui_diagnostic);", source)
                self.assertIn('!strcmp(command, "qspibench")', source)
                self.assertIn("display_gui.stackReserve()", source)

    def test_s3_ui_service_moved_verbatim(self):
        expected = ("6570200cfbe3db33990cfa5f50ff5f8254856c473b1df0eeac630e10c4b74ebe",
                    "331ae72583915e81178a717f6fb2f9ccadd95b7af242b72e8bbe2a31aeed0cdc",
                    "6570200cfbe3db33990cfa5f50ff5f8254856c473b1df0eeac630e10c4b74ebe")
        for name, checksum in zip(NAMES, expected):
            source = (ROOT / "Module" / name / (name + ".ino")).read_text(encoding="utf-8-sig")
            service = source.split("static void process_display_ui_once() {\n", 1)[1].split("\n}\n\nvoid setup()", 1)[0] + "\n"
            if name != "DisplayModule_800x480":
                # The small-display experiment adds a GUI-owned buffer switch,
                # not changes to the original UI service below it.
                self.assertTrue(service.startswith("  small_profile_tick();\n"))
                service = service.removeprefix("  small_profile_tick();\n")
            self.assertEqual(hashlib.sha256(service.encode()).hexdigest(), checksum, name)

    def test_transport_cannot_bypass_owner(self):
        for name in NAMES:
            stream = (ROOT / "Module" / name / "src/OfeDisplayValueStreamState.h").read_text()
            self.assertIn("if (ui_needs_defer()) ui_defer_flags", stream)


if __name__ == "__main__":
    unittest.main()
