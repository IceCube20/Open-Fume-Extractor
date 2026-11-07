from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("DisplayModule_320x480", "DisplayModule_800x480",
         "DisplayModule_ST7796_320x480", "DisplayModule_P4_800x480")


class DisplayUpdateExitTests(unittest.TestCase):
    def test_shared_policy_and_ui(self):
        for file in ("OfeDisplayUpdateExit.h", "OfeDisplayUpdateExitUi.inc.h"):
            copies = [(ROOT / "Module" / name / "src" / file).read_bytes() for name in NAMES]
            self.assertTrue(all(data == copies[0] for data in copies))

    def test_abort_and_gui_ownership(self):
        for name in NAMES:
            with self.subTest(display=name):
                source = (ROOT / "Module" / name / (name + ".ino")).read_text(encoding="utf-8-sig")
                abort = source.split("static void fw_update_abort_local() {", 1)[1].split("\n}", 1)[0]
                self.assertNotIn("display_update_exit.requestAbort()", abort)
                timeout = source.split("static void fw_update_check_timeout() {", 1)[1].split("\n}", 1)[0]
                self.assertLess(timeout.index("fw_update_abort_local();"), timeout.index("display_update_exit.requestAbort();"))
                command = source.split("    case CMD_FW_ABORT:\n      if (fw_update_reboot_ms.load())", 1)[1].split("    case CMD_FW_REBOOT:", 1)[0]
                self.assertLess(command.index("send_status_response(req, STATUS_OK)"), command.index("display_update_exit.requestAbort()"))
                self.assertNotIn("lv_", abort)
                self.assertNotIn('show_status_message(update_message_text("Update aborted"));', source)
                gui = source.split("static void process_display_ui_once() {", 1)[1].split("void setup()", 1)[0]
                self.assertLess(gui.index("apply_deferred_ui_updates();"), gui.index("update_exit_tick();"))
                self.assertNotIn("update_exit_tick();", source.split("void loop() {", 1)[1])
                status = source.split("static void lv_create_status_screen() {", 1)[1].split("\n}", 1)[0]
                self.assertIn("LV_SYMBOL_LEFT, lv_update_abort_back_event", status)
                self.assertIn("LV_ALIGN_BOTTOM_LEFT, 12, -12", status)
                self.assertIn("LV_OBJ_FLAG_HIDDEN", status)
                self.assertIn("  ui_status_back = nullptr;", source)


if __name__ == "__main__":
    unittest.main()
