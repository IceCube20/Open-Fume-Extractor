import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class FanIoDynamicSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fan = (ROOT / "Module/FanIoModule/FanIoModule.ino").read_text(encoding="utf-8")
        cls.pro = (ROOT / "Module/FanIoProModule/FanIoProModule.ino").read_text(encoding="utf-8")
        cls.master_controls = (ROOT / "OpenFumeExtractorMaster/src/WebControls.inc.h").read_text(encoding="utf-8")
        cls.master_status = (ROOT / "OpenFumeExtractorMaster/src/WebStatus.inc.h").read_text(encoding="utf-8")
        cls.master_mqtt = (ROOT / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(encoding="utf-8")
        cls.small = (ROOT / "Module/DisplayModule_320x480/DisplayModule_320x480.ino").read_text(encoding="utf-8")
        cls.large = (ROOT / "Module/DisplayModule_800x480/DisplayModule_800x480.ino").read_text(encoding="utf-8")

    def test_hardware_config_is_persistent_and_has_eight_channels(self):
        for module in ("FanIoModule", "FanIoProModule"):
            header = (ROOT / f"Module/{module}/src/FanIoHardwareConfig.h").read_text(encoding="utf-8")
            self.assertIn("MAX_INPUTS = 8", header)
            self.assertIn("MAX_OUTPUTS = 8", header)
            self.assertIn('getBytesLength("hwcfg")', header)
            self.assertIn('putBytes("hwcfg"', header)
            self.assertIn("cfg.checksum = checksum(cfg)", header)

    def test_modules_expose_descriptor_entities_and_io_config(self):
        for source in (self.fan, self.pro):
            self.assertIn("CAP_DESCRIPTOR | CAP_ENTITY_CONTROL", source)
            self.assertIn("case CMD_IO_CONFIG:", source)
            self.assertIn("IO_CONFIG_CHANNEL", source)
            self.assertIn("current_io_alias", source)
            self.assertRegex(source, r"MAX_INPUTS; \+\+i")
            self.assertRegex(source, r"MAX_OUTPUTS; \+\+i")

    def test_filter_mode_values_match_internal_enum(self):
        header = (ROOT / "Module/FanIoProModule/src/FanIoHardwareConfig.h").read_text(encoding="utf-8")
        self.assertIn("FILTER_OFF = 0", header)
        self.assertIn("FILTER_RUNTIME = 1", header)
        self.assertIn("FILTER_PRESSURE = 2", header)
        self.assertIn("FILTER_BOTH = 3", header)
        self.assertIn("CONFIG_VERSION = 3", header)
        self.assertIn("if (filter_mode_v1 && cfg.filter_mode <= 2) cfg.filter_mode += 1", header)
        self.assertIn("values=0|1|2|3", self.pro)
        self.assertIn("filter_runtime_save_due_ms = now + 900000UL", self.pro)

    def test_master_mqtt_and_displays_use_capabilities(self):
        self.assertIn("web_handle_io_config", self.master_controls)
        self.assertIn("CAP_DESCRIPTOR", self.master_mqtt)
        self.assertIn("CAP_ENTITY_CONTROL", self.master_mqtt)
        for source in (self.small, self.large):
            self.assertIn("return (caps & CAP_DESCRIPTOR)", source)

    def test_master_uses_one_large_fanio_hardware_editor(self):
        self.assertIn("function fanIoEditorOpen", self.master_status)
        self.assertIn("function fanIoEditorAdd", self.master_status)
        self.assertIn("function fanIoEditorRemove", self.master_status)
        self.assertIn("function fanIoEditorSave", self.master_status)
        self.assertIn("fanIoEditorCard(m)", self.master_status)
        self.assertIn("card.querySelectorAll('.fanio-legacy-extra')", self.master_status)
        self.assertIn("e=>e.style.display='none'", self.master_status)
        self.assertNotIn("box.insertAdjacentHTML('beforeend',fanIoHardwareCard(m))", self.master_status)

    def test_editor_explains_reserved_and_assigned_gpio_pins(self):
        self.assertIn("fanIoReservedPins", self.master_status)
        self.assertIn("function fanIoPinUses", self.master_status)
        self.assertIn("used by:", self.master_status)
        self.assertIn("belegt: ", self.master_status)
        self.assertIn("OFE-Bus TX", self.master_status)
        self.assertIn("OFE-Bus RX", self.master_status)
        self.assertIn("pin>=34&&pin<=39", self.master_status)
        self.assertIn("kein interner Pull-up/down", self.master_status)
        self.assertIn("external resistor required", self.master_status)

    def test_fan_descriptor_exposes_editor_only_flags(self):
        for source in (self.fan, self.pro):
            self.assertIn("fan_enabled=%u", source)
            self.assertIn("fan_active_low=%u", source)

    def test_editor_uses_full_width_channels_and_readable_filter_lifetime(self):
        self.assertIn("grid-template-columns:minmax(0,1fr)", self.master_status)
        self.assertIn("Filter-Wechselintervall · Tage", self.master_status)
        self.assertIn("fanIoEditorLifetimeSet('hours'", self.master_status)
        self.assertIn("fanIoEditorLifetimeSet('minutes'", self.master_status)
        self.assertIn("Relaisausgang", self.master_status)

    def test_disabled_main_output_is_removed_from_routing(self):
        scheduler = (ROOT / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")
        self.assertIn('strstr(rec.universal_descriptor, "fan_enabled=")', scheduler)
        self.assertIn("configured[12] == '0'", scheduler)
        self.assertIn("web_caps &= ~(CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT)", self.master_status)
        self.assertIn('strstr(m.universal_descriptor, "fan_enabled=")', self.master_mqtt)

    def test_afterrun_led_uses_shared_half_second_phase(self):
        paths = [ROOT / "OpenFumeExtractorMaster/src/OfeStatusLed.h"]
        paths.extend((ROOT / "Module").glob("*/src/OfeStatusLed.h"))
        self.assertGreaterEqual(len(paths), 10)
        for path in paths:
            source = path.read_text(encoding="utf-8")
            line = next(line for line in source.splitlines() if "// afterrun" in line)
            self.assertRegex(line, r"OFE_LED_BLINK,\s+500,")


if __name__ == "__main__":
    unittest.main()
