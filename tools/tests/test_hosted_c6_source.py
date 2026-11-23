import hashlib
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class HostedC6SourceTests(unittest.TestCase):
    def source(self,name):
        return (SRC / name).read_text(encoding='utf-8-sig')

    def test_exact_official_image_pin(self):
        source=self.source('HostedC6Firmware.h')
        self.assertIn('IMAGE_SIZE=1226976',source)
        self.assertIn('VERSION=0x020C0D',source)
        self.assertIn('https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.12.13.bin',source)
        digest=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',source.split('SHA256[32]=')[1].split(';')[0]))
        self.assertEqual(digest.hex(),'f8509949826c197a4fa3c7958943b4e8985cffdfdf4204c2847d8953905121c9')
        fixture=ROOT / 'tools/tests/build-checks/hosted-c6/esp32c6-v2.12.13.bin'
        if fixture.exists():
            image=fixture.read_bytes()
            self.assertEqual(len(image),1226976)
            self.assertEqual(hashlib.sha256(image).digest(),digest)
            self.assertEqual(image[0],0xe9)
            self.assertEqual(int.from_bytes(image[12:14],'little'),13)

    def test_web_upload_stages_without_flashing(self):
        source=self.source('WebHostedC6.inc.h')
        chunk=source[source.index('static void web_handle_c6_chunk_upload'):source.index('static void web_handle_c6_chunk_done')]
        self.assertNotIn('ota_write',chunk)
        self.assertIn('hosted_c6().append(',chunk)
        self.assertIn('offset==hosted_c6().received()',chunk)
        self.assertIn('web.arg("recovery")!="1"',source)
        self.assertIn('hostedIsBLEActive()',source)
        updater=self.source('HostedC6Updater.h')
        self.assertIn('MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',updater)
        self.assertIn('mbedtls_sha256(image_,received_,digest,0)',updater)
        self.assertIn('diff|=digest[i]^ofe_c6::SHA256[i]',updater)
        self.assertNotIn('developer_override',source+updater)

    def test_auth_csrf_and_exclusive_guards(self):
        server=self.source('WebServer.inc.h')
        for action in ('status','begin','chunk','seal','cancel','install'):
            route=next(line for line in server.splitlines() if f'"/update/c6/{action}"' in line)
            self.assertIn('web_require_auth()',route)
        security=self.source('WebSecurity.inc.h')
        self.assertIn('if (!web_csrf_ok())',security)
        self.assertIn('if (hosted_c6_busy() && web.method()!=HTTP_GET)',security)
        self.assertIn('if (hosted_c6_radio_owned()) return',server)
        self.assertIn('!hosted_c6_busy()',self.source('MasterMqtt.inc.h'))
        controls=self.source('WebControls.inc.h').split('static void web_handle_restart()')[1]
        self.assertLess(controls.index('hosted_c6_restart_blocked()'),controls.index('prepare_controlled_restart()'))

    def test_ui_requires_separate_confirmation_and_no_stale_enable(self):
        ui=self.source('HostedC6WebUi.h')
        self.assertIn("credentials:'omit'",ui)
        self.assertIn("'X-CSRF-Token':cfg.csrf",ui)
        self.assertIn("await request('seal',{token})",ui)
        self.assertIn("recovery:'1'",ui)
        self.assertIn("!$('recovery').checked",ui)
        self.assertIn('last=null;',ui)
        self.assertIn('token===observedToken',ui)

    def test_version_confirmed_only_after_reboot_from_journal(self):
        updater=self.source('HostedC6Updater.h')
        self.assertIn('slaveVersion()==want',updater)
        self.assertIn('record[7]==sum',updater)
        self.assertIn('if(result.committed && self.restart_)',updater)
        self.assertIn('if (!result.recovery && io.owned) hostedDeinitWiFi()',updater)

    def test_version_probe_is_deferred_read_only_and_keeps_sdk_cache(self):
        updater=self.source('HostedC6Updater.h')
        getter=updater.split('uint32_t slaveVersion() const {',1)[1].split('bool versionProbing()',1)[0]
        self.assertNotIn('esp_hosted_get_coprocessor_fwversion',getter)
        probe=updater.split('void pollVersion(',1)[1].split('uint32_t hostVersion()',1)[0]
        self.assertIn('busy() || blocked',probe)
        self.assertIn('!hostedIsWiFiActive() && !hostedIsBLEActive()',probe)
        self.assertIn('30000U',probe)
        self.assertIn('xTaskCreate(versionWorker',probe)
        worker=updater.split('static void versionWorker(',1)[1].split('struct IO {',1)[0]
        self.assertIn('hostedHasUpdate()',worker)
        self.assertIn('self.slave_=value',worker)
        self.assertLess(worker.index('self.version_last_attempt_=millis();'),worker.index('self.version_probing_=false;'))
        self.assertNotIn('hostedInitWiFi',worker)
        self.assertNotIn('ota_',worker)
        server=self.source('WebServer.inc.h')
        self.assertIn('hosted_c6().pollVersion(now,master_display_ble.active() || master_update_active || scheduler.firmwareUpdateActive() || scanning || wifi_sta_pending)',server)
        self.assertIn('c.versionError()',self.source('WebHostedC6.inc.h'))
        self.assertIn('esp_wifi_get_mode(&mode)',updater)
        self.assertIn('c.controlError()',self.source('WebHostedC6.inc.h'))
        self.assertIn('ofe_c6::RECOVERY_URL',self.source('WebHostedC6.inc.h'))


if __name__=='__main__':
    unittest.main()
