import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = ['DisplayModule_320x480', 'DisplayModule_800x480', 'DisplayModule_ST7796_320x480']
MASTER = ROOT / 'OpenFumeExtractorMaster'


class BluetoothSourceTests(unittest.TestCase):
    def test_shared_implementation(self):
        expected = (MASTER / 'src/OfeDisplayBle.h').read_text(encoding='utf-8').splitlines()[2:]
        for name in NAMES:
            self.assertEqual(expected, (ROOT / 'Module' / name / 'src/OfeDisplayBle.h').read_text(encoding='utf-8').splitlines()[2:])

    def test_security_and_bounded_lifetime(self):
        source = (MASTER / 'src/OfeDisplayBle.h').read_text(encoding='utf-8')
        for guard in ['setSecurityAuth(false,true,true)', 'BLE_HS_IO_DISPLAY_YESNO',
                      'NIMBLE_PROPERTY::WRITE_ENC|NIMBLE_PROPERTY::WRITE_AUTHEN',
                      'approved_ && peer.isEncrypted() && peer.isAuthenticated()',
                      'peer.getSecKeySize()==16', 'view_.token==token && view_.pin==pin',
                      'uint32_t(millis()-started)<120000UL', 'NimBLEDevice::deinit(true)',
                      'heap_caps_get_free_size(caps)<56U*1024U']:
            self.assertIn(guard, source)
        self.assertNotIn('lv_', source)
        finish = source.split('void finish(', 1)[1].split('void run()', 1)[0]
        self.assertNotIn('vTaskDelete', finish)
        approve = source.split('bool approve(', 1)[1].split('private:', 1)[0]
        self.assertNotIn('injectConfirmPasskey', approve)

    def test_web_auth_and_no_credentials_in_response(self):
        source = (MASTER / 'src/WebDisplayBle.inc.h').read_text(encoding='utf-8')
        handler = source.split('static void web_handle_ble_pairing()', 1)[1].split('static void append_ble', 1)[0]
        self.assertIn('if (!web_require_auth()) return;', handler)
        self.assertIn('!master_update_active && !scheduler.firmwareUpdateActive()', handler)
        response = handler.split('const auto state=', 1)[1]
        for secret in ['password', '.key', '.ssid', '.config']:
            self.assertNotIn(secret, response)

    def test_scan_does_not_keep_unrelated_devices_or_radio_while_waiting(self):
        source = (MASTER / 'src/OfeDisplayBle.h').read_text(encoding='utf-8')
        master = source.split('void runMaster(', 1)[1].split('void runDisplay(', 1)[0]
        self.assertIn('scan->setMaxResults(0);', master)
        self.assertIn('scan->setScanCallbacks(&scan_callbacks_,false);', master)
        self.assertLess(master.index('stopRadio()'), master.index('masterSelectionOrPhase(READY)'))
        self.assertIn('uint32_t(millis()-stopped_at)<3000UL', master)
        self.assertIn('if (!NimBLEDevice::isInitialized() && !startRadio()) return;', master)
        self.assertLess(master.index('if (!selected)'), master.index('runMasterPair(millis())'))
        self.assertLess(master.index('runMasterPair(millis())'), master.index('NimBLEDevice::createClient()'))
        self.assertNotIn('const auto results=', master)
        self.assertIn('NimBLEDevice::setScanDuplicateCacheSize(20);', source)
        self.assertNotIn('heap_caps_malloc_extmem_enable', source)
        self.assertNotIn('xTaskCreatePinnedToCoreWithCaps', source)

    def test_cached_pairing_and_elapsed_timer(self):
        source = (MASTER / 'src/OfeDisplayBle.h').read_text(encoding='utf-8')
        self.assertIn('constexpr uint8_t MAX_DEVICES=6;', source)
        self.assertIn('if (resumed) runMasterPair(started);', source)
        self.assertIn('view_.devices[i].paired=true;', source)
        self.assertIn('view_.pin=0; view_.token=esp_random();', source)
        web = (MASTER / 'src/WebDisplayBle.inc.h').read_text(encoding='utf-8')
        self.assertIn('ofe_ble::MAX_DEVICES-1', web)
        self.assertIn('ofe_ble::searchElapsedMs(state,millis())', web)
        self.assertIn('id=\'ble_search_timer\'', web)
        self.assertIn('button.disabled=!selectable||!!item.paired;', web)

    def test_header_text_and_icon_geometry(self):
        for name in NAMES:
            folder = ROOT / 'Module' / name
            source = (folder / f'{name}.ino').read_text(encoding='utf-8')
            font = (folder / 'openfume_montserrat_14_de.c').read_text(encoding='utf-8')
            glyphs = font.split('glyph_dsc[] = {', 1)[1].split('};', 1)[0]
            advances = [int(n) for n in re.findall(r'\.adv_w\s*=\s*(\d+)', glyphs)]
            width = sum((advances[ord(c)-32+1]+8)//16 for c in 'Open Fume Extractor')
            self.assertLessEqual(width, 158 if name != 'DisplayModule_800x480' else 320)
            icon = (folder / 'src/OfeBluetoothFont.h').read_text(encoding='utf-8')
            bitmap = icon.split('bitmap[] = {', 1)[1].split('};', 1)[0]
            data = bytes(int(n,16) for n in re.findall(r'0x[0-9a-f]+', bitmap))
            self.assertEqual(len(data), 50)
            self.assertGreater(sum(n.bit_count() for n in data), 50)
            self.assertIn('OFE_SYMBOL_BLUETOOTH', source)

    def test_ui_and_build_flags(self):
        for name in NAMES:
            folder = ROOT / 'Module' / name
            source = (folder / f'{name}.ino').read_text(encoding='utf-8')
            self.assertIn('display_ble.configure(', source)
            self.assertIn('lv_update_ble_indicator_ui', source)
            flags = (folder / 'build_opt.h').read_text()
            self.assertIn('-include "OfeBleBuildConfig.h"', flags)
            config = (folder / 'OfeBleBuildConfig.h').read_text()
            self.assertLess(config.index('#include <sdkconfig.h>'), config.index('#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL'))
            self.assertIn('#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1', config)
            self.assertIn('#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1', config)
            self.assertEqual(config, (MASTER / 'OfeBleBuildConfig.h').read_text())
            ui = (folder / 'src/OfeDisplayWifiUi.inc.h').read_text(encoding='utf-8')
            self.assertIn('Master-Kopplung löschen', ui)
            self.assertIn('display_ble.approve(ui->dialog_token_,ui->dialog_pin_,true)', ui)
            self.assertIn('display_wifi.forgetPairing()', ui)
            wifi = (folder / 'src/OfeDisplayWifi.h').read_text(encoding='utf-8')
            forget = wifi.split('bool forgetPairing()', 1)[1].split('void scan()', 1)[0]
            self.assertIn('c.from_master=0;', forget)
            self.assertNotIn('c.ssid', forget)
            self.assertNotIn('c.password', forget)


if __name__ == '__main__':
    unittest.main()
