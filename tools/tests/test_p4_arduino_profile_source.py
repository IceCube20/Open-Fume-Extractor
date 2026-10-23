"""Guard the isolated Arduino profile against wrong SDK or flash defaults."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class P4ArduinoProfile(unittest.TestCase):
    def test_fixed_tested_board_defaults(self):
        board = ROOT / "tools/boards/p4_high_perf_sdk/boards.txt"
        props = dict(line.split("=", 1) for line in board.read_text().splitlines()
                     if line and not line.startswith("#"))
        expected = {"build.chip_variant": "esp32p4_es", "build.f_cpu": "360000000L",
                    "build.flash_size": "16MB", "build.flash_mode": "dio",
                    "build.partitions": "default_8MB", "build.usb_mode": "1",
                    "build.cdc_on_boot": "1", "upload.maximum_size": "3342336",
                    "upload.erase_cmd": ""}
        for key, value in expected.items():
            self.assertEqual(props[f"ofep4.{key}"], value, key)
        self.assertEqual(props["ofep4.build.defines"],
                         "-DBOARD_HAS_PSRAM -DOFE_REQUIRE_P4_CACHE256_SDK=1 {build.ppa_flags}")
        self.assertFalse(any("postv3" in key or "EraseFlash" in key for key in props))

    def test_ppa_modes_are_explicit_and_default_is_software(self):
        props = dict(line.split("=", 1) for line in
                     (ROOT / "tools/boards/p4_high_perf_sdk/boards.txt").read_text().splitlines()
                     if line and not line.startswith("#"))
        self.assertEqual(props["ofep4.build.ppa_flags"], "-DOFE_P4_PPA=0")
        self.assertEqual(props["ofep4.menu.Renderer.ppa.build.ppa_flags"], "-DOFE_P4_PPA=1")
        self.assertEqual(props["ofep4.menu.Renderer.ppa.build.ppa_link_flags"],
                         "-Wl,--wrap=ppa_do_fill")
        self.assertEqual(props["ofep4.menu.Renderer.software.build.ppa_link_flags"], "")
        self.assertEqual(props["ofep4.menu.Renderer.ppaimage.build.ppa_flags"],
                         "-DOFE_P4_PPA=1 -DOFE_P4_PPA_IMG=1")
        self.assertEqual(props["ofep4.menu.Renderer.ppaimage.build.ppa_link_flags"],
                         "-Wl,--wrap=ppa_do_fill -Wl,--wrap=ppa_do_blend")
        self.assertEqual([key for key in props if re.fullmatch(r"ofep4.menu.Renderer.\w+", key)],
                         ["ofep4.menu.Renderer.software", "ofep4.menu.Renderer.ppa",
                          "ofep4.menu.Renderer.ppaimage"])
        self.assertEqual(props["ofep4.compiler.c.elf.extra_flags"], "{build.ppa_link_flags}")

    def test_installation_is_owned_and_validated(self):
        source = (ROOT / "tools/install_display_p4_arduino.ps1").read_text()
        for token in ("Assert-ChildPath", "Assert-Junction", "ofe-p4-install.json",
                      "check-sdk --sdk $Sdk", "check-sdk --sdk $installedSdk",
                      "compiler.sdk.path=$installedSdk", "hardware\\ofep4\\esp32",
                      "esp-rv32\\2601", "esptool_py\\5.3.1"):
            self.assertIn(token, source)
        self.assertNotIn("Remove-Item", source)
        self.assertNotIn("write-flash", source)

    def test_profile_rejects_stock_cache_sdk(self):
        source = (ROOT / "tools/boards/elecrow_p4_5inch/OfeP4SdkProfile.h").read_text()
        self.assertIn("defined(OFE_REQUIRE_P4_CACHE256_SDK)", source)
        self.assertIn("CONFIG_CACHE_L2_CACHE_SIZE != 0x40000", source)
        self.assertIn("CONFIG_CACHE_L2_CACHE_LINE_SIZE != 128", source)
        self.assertIn("#error", source)


if __name__ == "__main__":
    unittest.main()
