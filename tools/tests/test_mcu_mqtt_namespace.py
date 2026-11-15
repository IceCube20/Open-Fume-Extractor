"""Two MCUs must not share factory state, commands, or broker Client IDs."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
VC = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe")


class McuMqttNamespaceTests(unittest.TestCase):
    @unittest.skipUnless(VC.is_file(), "MSVC native test compiler required")
    def test_actual_two_mcu_discovery_and_namespace_migration(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/tests/mcu_mqtt_discovery_native/run.py")],
                                cwd=ROOT, check=True, capture_output=True, text=True, timeout=90)
        self.assertIn("no shared-root deletes", result.stdout)

    def test_load_save_reset_manifest_and_connect_use_device_namespace(self):
        for variant in ("S3", "P4"):
            src = ROOT / "OpenFumeExtractorMaster" / ("OpenFumeExtractorMCU_" + variant) / "src"
            network = (src / "MasterNetwork.inc.h").read_text(encoding="utf-8-sig")
            mqtt = (src / "MasterMqtt.inc.h").read_text(encoding="utf-8-sig")
            self.assertIn('mqtt_normalized_base_topic(net_prefs.getString(MasterSettingsStore::KEY_MQTT_TOPIC, ""))', network)
            self.assertIn("const String mqtt_topic = mqtt_normalized_base_topic(mqtt_topic_arg);", network)
            self.assertIn("MasterSettingsStore::KEY_MQTT_TOPIC, mqtt_topic)", network)
            self.assertIn('mqtt_normalized_base_topic("").toCharArray(mqtt_base_topic', network)
            self.assertIn("mqtt_normalized_base_topic(mqtt_base_topic).toCharArray(manifest.base_topic", mqtt)
            self.assertIn("const String client_id = mqtt_client_identity();", mqtt)
            self.assertIn("mqtt_client.connect(client_id.c_str()", mqtt)
            self.assertNotIn('snprintf(client_id, sizeof(client_id), "%s", master_hostname)', mqtt)


if __name__ == "__main__":
    unittest.main()
