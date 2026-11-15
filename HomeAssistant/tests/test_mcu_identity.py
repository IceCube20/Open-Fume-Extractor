import importlib.util
from pathlib import Path
import unittest

PATH = Path(__file__).resolve().parents[1] / 'custom_components/open_fume_extractor/const.py'
spec = importlib.util.spec_from_file_location('ofe_mcu_const', PATH)
const = importlib.util.module_from_spec(spec)
spec.loader.exec_module(const)


class McuIdentityTests(unittest.TestCase):
    def test_mcu_models_and_legacy_fallback(self):
        for model in ('MCU-S3', 'MCU-P4'):
            state = {'master_model': model}
            self.assertEqual(const.master_device_model(state), model)
            self.assertEqual(const.master_device_name(state), '0x01 Open Fume Extractor ' + model)
        for state in ({}, {'master_model': 'unknown'}, {'master_model': None}):
            self.assertEqual(const.master_device_model(state), const.MASTER_DEVICE_MODEL)
            self.assertEqual(const.master_device_name(state), const.MASTER_DEVICE_NAME)
