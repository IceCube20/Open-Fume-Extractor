from pathlib import Path
import shutil
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class EthernetActivityTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'), 'Node.js required')
    def test_actual_replay_under_module_load(self):
        subprocess.run([shutil.which('node'), str(Path(__file__).resolve().parent / 'test_lan_replay.cjs')], check=True, timeout=30)

    def test_only_explicit_status_ui_requests_enable_reporting(self):
        source=(ROOT / 'MasterMqtt.inc.h').read_text(encoding='utf-8-sig')
        self.assertIn('web.arg("lan_activity")=="1"',source)
        self.assertIn('master_ethernet_ready() && web.arg("lan_activity")',source)
        self.assertIn('master_ethernet_activity().sample(millis(),true)',source)
        status=(ROOT / 'WebStatus.inc.h').read_text(encoding='utf-8-sig')
        self.assertIn('master_ethernet_activity().sample(millis())',status)
        self.assertIn("fetch('/state?lan_activity=1'",status)
        self.assertIn("fetch('/led_state?lan_activity=1'",status)
        self.assertIn('lanReceive(d,t0,t1)',status)
        self.assertNotIn('catch(e){lanSet({});console.log(e)}',status)

    def test_activity_is_real_and_reporting_has_a_lease(self):
        source=(ROOT / 'MasterEthernetActivity.h').read_text()
        self.assertIn('IP_EVENT_TX_RX,onTraffic',source)
        self.assertIn('packet.esp_netif==self.netif_.load',source)
        self.assertIn('uint32_t(now-last_sample_ms_)>=8000U',source)
        self.assertIn('esp_netif_tx_rx_event_disable(previous)',source)
        self.assertNotIn('Serial.',source)
        self.assertIn('master_ethernet_activity().tick(now)',(ROOT / 'WebServer.inc.h').read_text())


if __name__=='__main__':
    unittest.main()
