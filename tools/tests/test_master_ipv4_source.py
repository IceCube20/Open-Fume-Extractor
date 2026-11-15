from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2] / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"


class MasterIpv4SourceTests(unittest.TestCase):
    def source(self,name):
        return (ROOT / name).read_text(encoding="utf-8-sig")

    def test_independent_form_names_and_initial_lan_setup(self):
        source=self.source("WebConfig.inc.h")
        self.assertIn('web_append_ipv4_settings(html,"","WLAN",wifi_config)',source)
        self.assertIn('web_append_ipv4_settings(html,"eth_","LAN / Ethernet",master_ethernet_settings())',source)
        self.assertIn('!ssid.length() && !master_ethernet_supported()',source)
        self.assertEqual(source.count('captive_active && !web_password_change_required && !master_ethernet_supported()'),2)

    def test_save_rejects_conflicts_before_writes(self):
        source=self.source("MasterNetwork.inc.h")
        save=source[source.index('static bool netcfg_save('):source.index('static bool netcfg_save_wifi(')]
        self.assertLess(save.index('master_ipv4_conflict('),save.index('save_prefs.begin('))
        self.assertIn('ethernet_config=nullptr',save)
        self.assertIn('if (ok && ethernet_config)',save)
        self.assertLess(save.index('if (!ok) return false'),save.index('master_ethernet_settings()=*ethernet_config'))
        self.assertIn('loadEthernetOpen(net_prefs)',source)

    def test_backup_preserves_old_wifi_keys_and_optional_lan(self):
        source=self.source("WebConfig.inc.h")
        export=source[source.index('static String build_config_backup_json'):source.index('static bool apply_config_backup_json_impl')]
        for name in ('eth_static','eth_ip','eth_gateway','eth_subnet','eth_dns1','eth_dns2'):
            self.assertIn('"'+name+'"',export)
            self.assertIn('"'+name+'"',source[source.index('static bool apply_config_backup_json_impl'):])
        restore=source[source.index('static bool apply_config_backup_json_impl'):]
        self.assertIn('auto ethernet_config=master_ethernet_settings()',restore)
        self.assertIn('ethernet_fields[i]->toString()',restore)
        self.assertLess(restore.index('master_ipv4_conflict('),restore.index('if (validate_only)'))

    def test_ethernet_requires_live_link_and_applies_all_static_fields(self):
        source=self.source('MasterNetworkInterface.h')
        self.assertIn('ETH.linkUp() && ETH.hasIP()',source)
        self.assertIn('ETH.config(c.ip,c.gateway,c.subnet,c.dns1,c.dns2)',source)
        self.assertIn('ETH.end()',source)


if __name__=='__main__':
    unittest.main()
