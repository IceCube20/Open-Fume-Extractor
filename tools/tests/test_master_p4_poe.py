import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('p4_poe_prepare', ROOT/'tools/boards/prepare_master_p4.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class P4PoeTests(unittest.TestCase):
    def test_separate_profile_and_private_exclusion(self):
        files = prepare.generated_files(poe=True)
        self.assertIn('OpenFumeExtractorMCU_P4_POE.ino', files)
        self.assertNotIn('OpenFumeExtractorMCU_P4.ino', files)
        self.assertFalse(any('PrivateConfig' in name or 'LocalConfig' in name for name in files))
        self.assertIn(b'-DOFE_MASTER_P4_WAVESHARE_POE=1', files['build_opt.h'])
        manifest = json.loads(files[prepare.MANIFEST])
        self.assertIn('WIFI6-POE-ETH', manifest['board'])
        self.assertIn('ChipVariant=postv3', manifest['fqbn'])
        self.assertIn('FlashSize=32M', manifest['fqbn'])
        self.assertFalse(manifest['private_config_copied'])
        self.assertIn('testing required', manifest['status'])

    def test_same_application_not_a_fork(self):
        files = prepare.generated_files(poe=True)
        for name in ('src/MasterScheduler.cpp', 'src/MasterMqtt.inc.h', 'src/WebStatus.inc.h', 'src/MasterUsbStorage.cpp'):
            self.assertEqual(files[name], (prepare.SOURCE/name).read_bytes())
        self.assertEqual(files['OpenFumeExtractorMCU_P4_POE.ino'], (prepare.SOURCE/'OpenFumeExtractorMCU_S3.ino').read_bytes())

    def test_partitions_fit_actual_32mb_and_preserve_nvs_and_apps(self):
        def parse(raw):
            entries = {}
            for line in raw.decode().splitlines():
                if not line.strip() or line.lstrip().startswith('#'):
                    continue
                name, kind, sub, offset, size, *_ = [part.strip() for part in line.split(',')]
                entries[name] = (int(offset, 0), int(size, 0))
            return entries
        entries = parse(prepare.generated_files(poe=True)['partitions.csv'])
        old = parse(prepare.generated_files()['partitions.csv'])
        for name in ('otadata', 'app0', 'app1', 'nvs'):
            self.assertEqual(entries[name], old[name])
        previous = 0x10000
        for name, (offset, size) in sorted(entries.items(), key=lambda entry: entry[1]):
            self.assertLessEqual(offset + size, 32*1024*1024)
            if name == 'otadata':
                continue
            self.assertGreaterEqual(offset, previous)
            previous = offset + size
        self.assertEqual(previous, 32*1024*1024)
        self.assertGreater(entries['spiffs'][1], old['spiffs'][1])

    def test_legacy_profile_still_pre_v3_and_16mb(self):
        files = prepare.generated_files()
        manifest = json.loads(files[prepare.MANIFEST])
        self.assertIn('ChipVariant=prev3', manifest['fqbn'])
        self.assertIn('FlashSize=16M', manifest['fqbn'])
        self.assertNotIn(b'WAVESHARE_POE', files['build_opt.h'])

    def test_runtime_revision_and_board_identity(self):
        source = prepare.SOURCE/'src'
        update = (source/'WebUpdate.inc.h').read_text(encoding='utf-8-sig')
        self.assertIn('master_firmware_header.reset(ESP.getChipRevision())', update)
        self.assertIn('header/chip/revision mismatch', update)
        network = (source/'MasterNetworkInterface.h').read_text()
        self.assertIn('CONFIG_ESP32P4_REV_MIN_FULL < 301', network)
        self.assertIn('CONFIG_ESP32P4_REV_MAX_FULL >= 300', network)
        c6 = (source/'HostedC6Firmware.h').read_text()
        self.assertIn('waveshare-p4-wifi6-poe-eth-v3', c6)
        self.assertIn('waveshare-p4-module-dev-kit-prev3', c6)

    def test_both_image_recipes_have_revision_limits(self):
        script = (ROOT/'tools/build_master_p4.ps1').read_text(encoding='utf-8-sig')
        self.assertIn('$minimum = if ($Poe) { 301 } else { 1 }', script)
        self.assertIn('$maximum = if ($Poe) { 399 } else { 199 }', script)
        self.assertIn('--min-rev-full $minimum --max-rev-full $maximum', script)
        self.assertIn('recipe.hooks.prebuild.4.pattern_args=$bootRecipe', script)
        self.assertIn('recipe.objcopy.bin.pattern_args=$appRecipe', script)
        self.assertIn('[BitConverter]::ToUInt16($bytes,15) -ne $minimum', script)


if __name__ == '__main__':
    unittest.main()
