"""MCU network state is published consistently for MQTT and native HA."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class McuNetworkHaTests(unittest.TestCase):
    def test_mqtt_reports_the_same_active_network_key_as_the_api(self):
        for variant in ("S3", "P4"):
            src = ROOT / "OpenFumeExtractorMaster" / ("OpenFumeExtractorMCU_" + variant) / "src"
            mqtt = (src / "MasterMqtt.inc.h").read_text(encoding="utf-8-sig")
            api = (src / "WebStatus.inc.h").read_text(encoding="utf-8-sig")
            field = r'\"network_transport\":\"'
            self.assertIn(field + '"; state += master_network_transport_key();', mqtt)
            self.assertIn(field + '"; json += master_network_transport_key();', api)
            self.assertIn('"sensor", "network_transport", mqtt_txt("Netzwerk", "Network")', mqtt)
            icons = (src / "HaEntityIcons.h").read_text()
            self.assertIn('{"network_transport", "mdi:lan"}', icons)


if __name__ == "__main__":
    unittest.main()
