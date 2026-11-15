from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"


class MasterNetworkStatusTests(unittest.TestCase):
    def source(self, name):
        return (ROOT / name).read_text(encoding="utf-8-sig")

    def test_live_and_brief_status_report_actual_interface(self):
        for name in ("WebStatus.inc.h", "WebDisplayBle.inc.h"):
            source = self.source(name)
            for helper in ("master_network_transport_key()", "master_ethernet_ready()", "master_wifi_enabled()"):
                self.assertIn(helper, source, name)
        icons = self.source("WebTransportIcons.h")
        self.assertIn("s.network_transport", icons)
        self.assertNotIn("!!s.wifi)", icons)  # Legacy field is provisioning readiness, not the radio state.
        self.assertIn('data-network-icon="ethernet"', icons)
        self.assertIn('data-network-icon="wifi"', icons)

    def test_wifi_stop_is_policy_owned_and_not_continuously_rpc_polled(self):
        source = self.source("WebServer.inc.h")
        self.assertIn("!master_lan_wifi_policy.suspended()", source)
        self.assertIn("master_lan_wifi_policy.confirmSuspended()", source)
        self.assertIn("master_display_ble.active() || master_update_active || scheduler.firmwareUpdateActive()", source)
        self.assertNotIn("hostedDeinit", source)
        self.assertNotIn("WiFi.disconnect(true, true)", source)

    def test_network_reconnect_does_not_touch_mqtt_socket_from_http(self):
        self.assertIn("mqtt_network_reconnect_requested.store(true)", self.source("WebServer.inc.h"))
        self.assertNotIn("mqtt_client.disconnect()", self.source("WebServer.inc.h"))
        self.assertIn("mqtt_network_reconnect_requested.exchange(false)", self.source("MasterMqtt.inc.h"))

    def test_config_redirect_prefers_actual_ethernet_ip(self):
        source=self.source("WebConfig.inc.h")
        self.assertIn("const IPAddress redirect_ip=master_ethernet_ready()",source)
        self.assertIn("master_ethernet_settings().use_static ? master_ethernet_settings().ip : master_network_ip()",source)
        self.assertIn("if (redirect_ip!=IPAddress(0,0,0,0))",source)


if __name__ == "__main__":
    unittest.main()
