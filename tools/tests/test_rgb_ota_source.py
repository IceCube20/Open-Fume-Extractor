"""Keep sector-wise OTA local to S3 RGB without changing other display writers."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class RgbOtaSourceTests(unittest.TestCase):
    def test_rgb_selects_sequential_writer_only_for_s3(self):
        source = (ROOT / "Module/DisplayModule_800x480/DisplayModule_800x480.ino").read_text(encoding="utf-8-sig")
        self.assertIn('#if defined(CONFIG_IDF_TARGET_ESP32S3)\n#define OFE_DISPLAY_OTA_TIMING 1\n#include "src/OfeRgbOta.h"\nstatic OfeRgbOta display_firmware_writer;', source)
        self.assertIn('#else\nstatic auto& display_firmware_writer = Update;', source)
        for operation in ("begin", "write", "end", "abort"):
            self.assertIn(f"display_firmware_writer.{operation}(", source)
        self.assertIn("#define FW_UPDATE_WRITE_BUFFER_SIZE 4096", source)
        self.assertIn("OTA FLASH: IDF sequential", source)

    def test_boot_selection_follows_idf_image_validation(self):
        source = (ROOT / "Module/DisplayModule_800x480/src/OfeRgbOta.h").read_text(encoding="utf-8-sig")
        self.assertIn("esp_ota_begin(partition_, OTA_WITH_SEQUENTIAL_WRITES", source)
        self.assertIn("written_ != expected_", source)
        self.assertIn("if (result == ESP_OK) result = esp_ota_set_boot_partition(partition_);", source)
        self.assertLess(source.index("esp_ota_end(handle_)"), source.index("esp_ota_set_boot_partition(partition_)"))

    def test_small_ports_retain_arduino_update(self):
        for name in ("DisplayModule_320x480", "DisplayModule_ST7796_320x480"):
            source = (ROOT / "Module" / name / f"{name}.ino").read_text(encoding="utf-8-sig")
            self.assertIn("Update.write(fw_update_write_buffer, n)", source)
            self.assertNotIn("OfeRgbOta", source)
            self.assertNotIn("#define OFE_DISPLAY_OTA_TIMING 1", source)

    def test_wifi_stage_diagnostics_observe_ack_result_without_changing_retries(self):
        source = (ROOT / "Module/DisplayModule_800x480/src/OfeDisplayWifi.h").read_text(encoding="utf-8-sig")
        self.assertIn("if (ok) ota_timing_.reset();", source)
        self.assertIn("if (updateWireless()) ota_timing_.poll(micros());", source)
        self.assertIn("ota_timing_.checked(uint32_t(micros()-received_us),valid_bulk);", source)
        self.assertIn("ota_timing_.handled(uint32_t(micros()-handle_started_us),duplicate);", source)
        self.assertIn("const bool ack_sent=sendBulkPacket", source)
        self.assertIn("ota_timing_.acknowledged(uint32_t(micros()-ack_started_us),ack_sent);", source)
        self.assertIn("if (!valid_bulk) return false;", source)
        self.assertIn("OTA WIFI: rx=", source)


if __name__ == "__main__":
    unittest.main()
