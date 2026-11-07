"""Protect AP fallback recovery and automatic WLAN OTA sizing."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "OpenFumeExtractorMaster/src"


class MasterWifiReconnectTests(unittest.TestCase):
    def test_fallback_preserves_sta_and_portal_until_connection_returns(self):
        source = (SRC / "WebServer.inc.h").read_text(encoding="utf-8-sig")
        self.assertIn("WiFi.mode(wifi_ssid[0] ? WIFI_AP_STA : WIFI_AP);", source)
        self.assertIn("WiFi.mode(captive_active ? WIFI_AP_STA : WIFI_STA);", source)
        self.assertIn("WiFi.setAutoReconnect(true);", source)
        self.assertIn("if (!wifi_ssid[0]) return;", source)
        self.assertIn("if (!wifi_sta_was_connected || wifi_sta_pending || captive_active)", source)
        self.assertIn("dns.stop();", source)
        self.assertIn("WiFi.softAPdisconnect(true);", source)
        self.assertNotIn("if (!captive_active && strlen(wifi_ssid)", source)

    def test_packet_size_is_automatic_without_the_removed_test_control(self):
        source = (SRC / "WebUpdate.inc.h").read_text(encoding="utf-8-sig")
        self.assertIn("module_update_wifi_bulk_limit = ofe_wifi::BULK_DATA_MAX;", source)
        self.assertNotIn("requested_bulk", source)
        self.assertNotIn("bulkLimitQuery", source)
        self.assertNotIn("WiFi packet test requires developer mode", source)
        self.assertIn("? (size_t)module_update_wifi_bulk_limit", source)
        self.assertIn("module_update_fw_chunk_size(target_addr)", source)


if __name__ == "__main__":
    unittest.main()
