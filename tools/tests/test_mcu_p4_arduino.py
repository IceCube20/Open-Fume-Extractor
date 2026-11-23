"""The IDE profiles must pair the compiled SDK and the emitted image limits."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class McuP4ArduinoTests(unittest.TestCase):
    def test_fixed_profiles_match_sdk_flash_and_revision(self):
        values = dict(line.split('=', 1) for line in
                      (ROOT/'tools/boards/mcu_p4/boards.txt').read_text().splitlines()
                      if line and not line.startswith('#'))
        for board, sdk, flash, minimum, maximum, clock in (
                ('ofemcu_p4', 'esp32p4_es', '16MB', '1', '199', '360000000L'),
                ('ofemcu_p4_poe', 'esp32p4', '32MB', '301', '399', '400000000L')):
            for key, expected in (('chip_variant', sdk), ('flash_size', flash),
                                  ('min_revision', minimum), ('max_revision', maximum), ('f_cpu', clock)):
                self.assertEqual(values[board+'.build.'+key], expected)
            self.assertEqual(values[board+'.upload.maximum_size'], '5242880')
            self.assertEqual(values[board+'.build.partitions'], '')
            self.assertEqual(values[board+'.build.cdc_on_boot'], '0')
            self.assertIn('BOARD_HAS_PSRAM', values[board+'.build.defines'])
            self.assertFalse(any(key.startswith(board+'.menu.ChipVariant.') for key in values))

    def test_separate_platform_pins_both_esptool_recipes(self):
        source = (ROOT/'tools/install_mcu_p4_arduino.ps1').read_text()
        self.assertIn('hardware\\ofemcu\\esp32', source)
        self.assertIn('Assert-Junction $destination $platform', source)
        self.assertIn('Unowned runtime exists', source)
        self.assertIn('3\\.3\\.12', source)
        for recipe in ('recipe.hooks.prebuild.4.pattern_args=', 'recipe.objcopy.bin.pattern_args='):
            line = next(line for line in source.splitlines() if recipe in line)
            self.assertIn('--min-rev-full {build.min_revision}', line)
            self.assertIn('--max-rev-full {build.max_revision}', line)
        self.assertIn('compiler.sdk.path={ofe.sdk.path}', source)
        self.assertNotIn('Remove-Item', source)


if __name__ == '__main__':
    unittest.main()
