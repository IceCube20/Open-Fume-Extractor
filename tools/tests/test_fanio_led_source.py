import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"


class FanIoLedSourceTests(unittest.TestCase):
    def test_shared_configuration_and_driver(self):
        for name in ("FanIoHardwareConfig.h", "FanIoLedConfig.h", "FanIoVirtualLeds.h"):
            a = (ROOT / "Module/FanIoModule/src" / name).read_bytes()
            b = (ROOT / "Module/FanIoProModule/src" / name).read_bytes()
            self.assertEqual(a, b, name)

    def test_bulk_transport_and_disabled_default(self):
        config = (ROOT / "Module/FanIoModule/src/FanIoLedConfig.h").read_text()
        self.assertIn("cfg.pin=0xFF", config)
        for name in ("FanIoModule", "FanIoProModule"):
            code = (ROOT / "Module" / name / (name+".ino")).read_text()
            self.assertIn("dynamic_descriptor[12288]", code)
            self.assertIn("profile_slots=64", code)
            self.assertIn("LED_ENTITY_BANK;resp.payload[o++]=8", code)
            self.assertIn("role=indicator", code)
            self.assertIn("virtual_leds.allow(!fw_update_active", code)

    def test_editor_and_routing_share_the_existing_pin_map(self):
        status = (SRC / "WebStatus.inc.h").read_text(encoding="utf-8")
        self.assertIn("fanIoPinOptions(l.pin,true,'led_strip')", status)
        self.assertIn("take(l.pin,true,'LED')", status)
        self.assertIn("if(d.leds)", status)
        signals = (SRC / "WebAutomationSignals.h").read_text(encoding="utf-8")
        self.assertIn("Number(e.id)>=100&&Number(e.id)<132", signals)
        self.assertIn("addOutput('uni:'+addr+':switch:'+e.id", signals)


if __name__ == "__main__":
    unittest.main()
