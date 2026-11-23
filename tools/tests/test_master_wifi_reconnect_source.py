"""Protect AP fallback recovery and automatic WLAN OTA sizing."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"


class MasterWifiReconnectTests(unittest.TestCase):
    def test_scan_pauses_only_pending_association_and_preserves_credentials(self):
        source=(SRC/'WebServer.inc.h').read_text(encoding='utf-8-sig')
        helper=source.split('static void wifi_pause_connection_for_scan()',1)[1].split('static void wifi_on_sta_connected()',1)[0]
        self.assertIn('!wifi_sta_pending || WiFi.status()==WL_CONNECTED',helper)
        self.assertIn('WiFi.disconnect(false,false)',helper)
        self.assertIn('wifi_sta_pending=false',helper)
        self.assertIn('wifi_next_retry_ms=millis()+30000UL',helper)
        self.assertIn('static void wifi_pause_connection_for_scan();',(SRC/'MasterPrototypes.inc.h').read_text())
        config=(SRC/'WebConfig.inc.h').read_text(encoding='utf-8-sig')
        scan=config.split('static void web_handle_wifi_scan()',1)[1].split('static void web_append_wifi_scan_controls',1)[0]
        self.assertLess(scan.index('wifi_pause_connection_for_scan()'),scan.index('WiFi.scanNetworks(true, true)'))

    def test_time_sync_follows_network_and_retries_without_blocking_http(self):
        source = (SRC / "WebServer.inc.h").read_text(encoding="utf-8-sig")
        service = source[source.index("static void master_time_service_tick("):source.index("static void wifi_start_setup_ap(")]
        self.assertIn("master_time_sync.poll(now,master_network_ready()", service)
        self.assertIn("master_ethernet_ready(),time(nullptr)>1700000000,blocked)", service)
        self.assertIn("master_network_dns(0)", service)
        self.assertNotIn("getLocalTime", service)
        self.assertNotIn("delay(", service)
        self.assertEqual(source.count("configTzTime("), 1)
        self.assertNotIn("wifi_time_configured", source)
        self.assertLess(source.index("master_time_service_tick(now);"), source.index("if (ethernet) return;"))

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
