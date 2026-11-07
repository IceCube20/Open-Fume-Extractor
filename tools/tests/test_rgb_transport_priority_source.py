"""Keep the S3 buffer experiment separate from priority and polling changes."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULES = ROOT / "Module"
NAMES = (
    "DisplayModule_320x480", "DisplayModule_ST7796_320x480",
    "DisplayModule_800x480", "DisplayModule_P4_800x480",
)


def sketch(name):
    return (MODULES / name / (name + ".ino")).read_text(encoding="utf-8-sig")


class RgbTransportPriorityTests(unittest.TestCase):
    def test_baseline_priority_is_restored_and_override_is_preserved(self):
        expected = (
            "#ifndef DISPLAY_RS485_TASK_PRIORITY\n"
            "// The priority-2 S3 OTA experiment did not improve the reported transfer.\n"
            "// Restore the baseline before testing WiFi buffers separately.\n"
            "#define DISPLAY_RS485_TASK_PRIORITY 1\n"
            "#endif"
        )
        for name in NAMES[2:]:
            self.assertIn(expected, sketch(name), name)
        for name in NAMES:
            source = sketch(name)
            for setting, value in (
                ("TASK_CORE", 0), ("TASK_STACK", 8192),
                ("ACTIVE_YIELD_MS", 1), ("IDLE_DELAY_MS", 2), ("PERIODIC_MS", 5),
            ):
                self.assertRegex(source, rf"#define DISPLAY_RS485_{setting}\s+{value}\b")

    def test_wifi_receive_path_and_config_worker_are_unchanged(self):
        receive = None
        for name in NAMES:
            source = (MODULES / name / "src/OfeDisplayWifi.h").read_text(encoding="utf-8-sig")
            begin = source.index("  bool poll(jbc_rs485::Frame& frame)")
            end = source.index("  static void workerHook", begin)
            if receive is None:
                receive = source[begin:end]
            self.assertEqual(receive, source[begin:end], name)
            if name in NAMES[:2]:
                self.assertIn("WiFi.useStaticBuffers(true);", source)
            self.assertIn('workerHook,"display-wifi",6144,this,1,&worker_,0', source)

    def test_sdk_buffers_are_s3_only_overridable_and_set_before_radio_start(self):
        expected = (
            "#ifndef OFE_DISPLAY_WIFI_SDK_BUFFERS\n"
            "#if defined(CONFIG_IDF_TARGET_ESP32S3)\n"
            "// Match the small S3 panels: retain the SDK's eight static RX buffers.\n"
            "#define OFE_DISPLAY_WIFI_SDK_BUFFERS 1\n"
            "#else\n"
            "// Preserve the generated P4 port's existing hosted-WiFi configuration.\n"
            "#define OFE_DISPLAY_WIFI_SDK_BUFFERS 0\n"
            "#endif\n#endif"
        )
        for name in NAMES[2:]:
            source = (MODULES / name / "src/OfeDisplayWifi.h").read_text(encoding="utf-8-sig")
            self.assertIn(expected, source, name)
            start = source.index("  bool startRadio(")
            radio = source[start:source.index("  void finishScan(", start)]
            self.assertLess(radio.index("WiFi.useStaticBuffers(OFE_DISPLAY_WIFI_SDK_BUFFERS != 0);"),
                            radio.index("WiFi.mode(WIFI_STA)"))
            self.assertIn("CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM", radio)

    def test_poll_budget_and_yield_remain_at_baseline(self):
        for name in NAMES:
            source = sketch(name)
            small = name in NAMES[:2]
            budget = 12 if small else 6
            self.assertIn(f"while (frames < {budget} && bus.poll(req))", source)
            pause = "yield();" if small else "vTaskDelay(pdMS_TO_TICKS(1));"
            self.assertIn(f"if (frames >= {budget}) {pause}", source)


if __name__ == "__main__":
    unittest.main()
