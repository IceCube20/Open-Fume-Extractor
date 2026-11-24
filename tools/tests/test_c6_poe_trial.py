from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
TRIAL = ROOT / 'tools/experiments/C6_PoE_2131_Trial'


class C6PoETrialTests(unittest.TestCase):
    def test_fixed_images_do_not_change_the_stable_approval(self):
        import json
        catalog = json.loads((ROOT / 'updates/hosted-c6-v2.json').read_text())
        for release in catalog['releases']:
            expected = '2.13.1' if release['board'] == 'waveshare-p4-wifi6-poe-eth-v3.2' else '2.12.13'
            self.assertEqual(release['coprocessor']['version'], expected)
        stable = (ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/HostedC6Firmware.h').read_text()
        self.assertIn('VERSION=0x020C0D', stable)
        policy = (TRIAL / 'TrialPolicy.h').read_text()
        for version, size in [('2.12.13', 1226976), ('2.13.1', 1244160)]:
            self.assertIn(str(size), policy)
            self.assertIn('esp32c6-v' + version + '.bin', policy)
        self.assertEqual(policy.count('https://raw.githubusercontent.com/espressif/arduino-esp32/0c2733048bd001c87b0d6912843de5f259b2a13e/hosted/'), 2)

    def test_only_explicit_commands_can_flash(self):
        source = (TRIAL / 'C6_PoE_2131_Trial.ino').read_text()
        setup = source.split('void setup() {', 1)[1].split('void loop()', 1)[0]
        self.assertNotIn('flash_image(', setup)
        self.assertIn('FLASH TEST 2.13.1', source)
        self.assertIn('RESTORE BASELINE 2.12.13', source)
        self.assertIn('if (restart_required)', source)
        self.assertIn('restart_required = true; return esp_hosted_slave_ota_begin()', source)
        self.assertIn('staged_image == &image && verify_image(image, bytes)', source)
        self.assertIn('command_overflow', source)
        self.assertIn('static bool recovery_confirmed = false;', source)
        self.assertIn('if (!recovery_confirmed)', source)
        self.assertIn('RECOVERY CHECKED', source)

    def test_tls_hash_and_runtime_board_guards(self):
        source = (TRIAL / 'C6_PoE_2131_Trial.ino').read_text()
        for guard in ('ESP.getChipRevision() == 302', 'ESP.getFlashChipSize() == 32U',
                      'a == 2 && b == 12 && c == 13', 'esp_crt_bundle_attach',
                      'cfg.disable_auto_redirect = true', 'length == image.size',
                      'mbedtls_sha256(', 'bytes[12] != 13', 'time(nullptr) < 1760000000'):
            self.assertIn(guard, source)
        self.assertNotIn('setInsecure', source)
        self.assertNotIn('nvs_flash_erase', source)

    def test_partitions_preserve_poe_addresses(self):
        rows = lambda path: [re.sub(r'\s+', '', line) for line in path.read_text().splitlines()
                             if line.strip() and not line.lstrip().startswith('#')]
        self.assertEqual(rows(TRIAL / 'partitions.csv'), rows(ROOT / 'tools/boards/master_p4_32mb.csv'))


if __name__ == '__main__':
    unittest.main()
