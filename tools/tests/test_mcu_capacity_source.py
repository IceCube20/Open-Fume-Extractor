"""Capacity expansion preserves protocol geometry, S3 defaults and OTA chip checks."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class McuCapacityTests(unittest.TestCase):
    def test_architecture_specific_capacity(self):
        source = (SOURCE / 'ModuleRegistry.h').read_text()
        self.assertIn('#if defined(CONFIG_IDF_TARGET_ESP32P4)\n  static const uint8_t MAX_MODULES = 32;\n#else\n  static const uint8_t MAX_MODULES = 16;', source)
        scheduler = (SOURCE / 'MasterScheduler.cpp').read_text()
        self.assertIn('DiscoveredModule found[ModuleRegistry::MAX_MODULES]', scheduler)
        self.assertIn('found_count >= ModuleRegistry::MAX_MODULES', scheduler)

    def test_all_displays_cover_32_modules_and_mcu(self):
        for folder in (ROOT / 'Module').glob('DisplayModule_*'):
            source = (folder / (folder.name + '.ino')).read_text(encoding='utf-8-sig')
            capacity = (folder / 'src/OfeDisplayCapacity.h').read_text()
            self.assertIn('DISPLAY_MODULE_CAPACITY = 33;', capacity)
            self.assertIn('DISPLAY_UNIVERSAL_MODULE_CACHE_MAX = 32;', source)
            self.assertIn('raw_total > DISPLAY_MODULE_CAPACITY ? DISPLAY_MODULE_CAPACITY : raw_total', source)
            self.assertIn('home_output_addrs[DISPLAY_OUTPUT_OPTION_CAPACITY]', source)
            self.assertIn('status.suction_level = req.payload[17];', source)
            tx = 15 if 'ST7796' in folder.name else 17
            self.assertIn(f'#define RS485_TX_PIN {tx}', source)
            self.assertNotIn('i < 17;', source)
            activity = (folder / 'src/OfeDisplayHomeActivityUi.h').read_text()
            self.assertIn('fans[5 + DISPLAY_MODULE_CAPACITY]', activity)

    def test_chip_check_cannot_be_bypassed_by_developer_override(self):
        source = (SOURCE / 'WebUpdate.inc.h').read_text(encoding='utf-8-sig')
        self.assertIn('MasterFirmwareHeader master_firmware_header(0x0012)', source)
        self.assertIn('MasterFirmwareHeader master_firmware_header(0x0009)', source)
        check = source.split('static bool master_update_check_real_bytes(', 1)[1].split('  char expected_marker', 1)[0]
        self.assertIn('master_firmware_header.append(data, len)', check)
        self.assertIn('master_update_abort_active()', check)
        self.assertNotIn('master_update_unsafe_fw_type', check)
        self.assertEqual(source.count('master_update_check_real_bytes(upload.buf, upload.currentSize)'), 2)

    def test_release_selects_one_architecture_and_validates_images(self):
        workflow = (ROOT / '.github/workflows/release-assets.yml').read_text()
        self.assertIn('.chipFamily == "ESP32-S3"', workflow)
        self.assertIn('length == 1', workflow)
        script = (ROOT / 'tools/prepare_github_release.ps1').read_text()
        self.assertIn("Chip='ESP32-P4'", script)
        self.assertIn('$chipId -ne $expectedChip', script)
