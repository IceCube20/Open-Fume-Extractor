import importlib.util
import json
import re
from pathlib import Path
import unittest
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("master_p4_preparation", ROOT / "tools/boards/prepare_master_p4.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class MasterP4PreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = prepare.generated_files()

    def test_shared_application_is_not_forked(self):
        for name in ("src/MasterScheduler.cpp", "src/MasterMqtt.inc.h", "src/WebHomeAssistant.inc.h",
                     "src/WebServer.inc.h", "src/ExtractorLogic.cpp"):
            self.assertEqual(self.files[name], (prepare.SOURCE / name).read_bytes())
        self.assertEqual(self.files["OpenFumeExtractorMCU_P4.ino"],
                         (prepare.SOURCE / "OpenFumeExtractorMCU_S3.ino").read_bytes())

    def test_p4_release_candidate_identity_and_manifest(self):
        config = self.files["src/MasterBuildConfig.h"].decode("utf-8-sig")
        self.assertIn('#define MASTER_MCU_MODEL "MCU-P4"', config)
        self.assertIn('#if OFE_DEVELOPER_MODE_ENABLE\n#define MASTER_FW_SUFFIX "RC-Developer"\n#else\n#define MASTER_FW_SUFFIX "RC"\n#endif', config)
        version = ".".join(re.search(r"^#define MASTER_FW_" + part + r" (\d+)$", config, re.M)[1]
                           for part in ("MAJOR", "MINOR", "PATCH")) + "RC"
        manifest = json.loads(self.files[prepare.MANIFEST])
        self.assertEqual(manifest["firmware_version"], version)
        self.assertNotIn("Preview", manifest["status"])
        for name in ("tools/prepare_github_release.ps1", "tools/update_firmware_docs.ps1"):
            self.assertNotIn("RC-P4-Preview", (ROOT / name).read_text(encoding="utf-8-sig"))

    def test_p4_partition_layout_is_separate(self):
        self.assertEqual(self.files['partitions.csv'], (ROOT/'tools/boards/master_p4_16mb.csv').read_bytes())
        self.assertNotEqual(self.files['partitions.csv'], (prepare.SOURCE/'partitions.csv').read_bytes())

    def test_no_private_configuration_or_binary_is_staged(self):
        self.assertNotIn("MasterPrivateConfig.h", self.files)
        self.assertFalse(any("PrivateConfig" in name or name.endswith((".bin", ".elf", ".pem", ".key")) for name in self.files))
        self.assertEqual(self.files["build_opt.h"], b"-O3\n-DOFE_MASTER_P4_WAVESHARE=1\n")

    def test_local_board_settings_are_not_copied_or_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'source';dest=Path(folder)/'dest'
            (source/'src').mkdir(parents=True)
            for name in ('OpenFumeExtractorMCU_S3.ino','src/MasterBuildConfig.h','src/OfeDisplayBle.h'):
                (source/name).write_bytes((prepare.SOURCE/name).read_bytes())
            (source/'src/MasterBoardLocalConfig.h').write_text('#define OFE_STATUS_LED_ENABLE 1\n')
            with patch.object(prepare,'SOURCE',source),patch.object(prepare,'DEST',dest),patch('sys.argv',['prepare']):
                self.assertNotIn('src/MasterBoardLocalConfig.h',prepare.generated_files())
                prepare.main()
                local=dest/'src/MasterBoardLocalConfig.h'
                local.write_text('#define OFE_STATUS_LED_ENABLE 1\n')
                prepare.main()
                self.assertEqual(local.read_text(),'#define OFE_STATUS_LED_ENABLE 1\n')

    def test_ble_protocol_comes_from_existing_master(self):
        source = (prepare.SOURCE / "src/OfeDisplayBle.h").read_text(encoding="utf-8-sig")
        protocol = source[source.index("namespace ofe_ble {"):source.index("class OfeDisplayBle {")]
        self.assertEqual(self.files["src/p4/OfeBleProtocol.h"].decode(), "#pragma once\n" + protocol)

    def test_ecc_is_existing_tested_backport(self):
        for name in ("OfeP4BleEccFix.h", "OfeP4BleEccSelfTest.c", "OfeP4BleEccCore.c", "OfeP4BleEccPort.c"):
            self.assertEqual(self.files["src/p4/" + name], (prepare.ECC / name).read_bytes())
        self.assertIn("src/p4/tinycrypt/UPSTREAM.json", self.files)

    def test_board_profile_is_opt_in_and_pre_v3(self):
        board = self.files["src/MasterBoardConfig.h"].decode()
        self.assertIn('#if !defined(OFE_MASTER_P4_WAVESHARE)', board)
        self.assertIn('#define RS485_RX_PIN 21', board)
        self.assertIn('#define RS485_TX_PIN 20', board)
        self.assertIn('#define OFE_STATUS_LED_ENABLE 1', board)
        self.assertIn('ChipVariant=prev3', prepare.FQBN)
        self.assertIn('FlashSize=16M', prepare.FQBN)
        network = self.files["src/MasterNetworkInterface.h"].decode()
        self.assertIn('WiFi.setPins(18, 19, 14, 15, 16, 17, 54)', network)
        self.assertIn('ETH.begin(ETH_PHY_IP101, 1, 31, 52, 51, EMAC_CLK_EXT_IN)', network)

    def test_ota_chip_guard_is_not_developer_bypassed(self):
        update = self.files["src/WebUpdate.inc.h"].decode("utf-8-sig")
        start = update.index('static bool master_update_check_real_bytes(')
        function = update[start:update.index('static bool module_update_check_real_signature', start)]
        self.assertLess(function.index('master_firmware_header.append'), function.index('master_update_unsafe_fw_type'))
        self.assertIn('master_firmware_header.complete() && Update.end(true)', update)
        multipart = update.split('static void web_handle_master_update_upload()', 1)[1].split('static void module_update_queue_reset()', 1)[0]
        self.assertLess(multipart.index('!master_firmware_header.complete()'), multipart.index('master_update_ok = Update.end(true)'))
        self.assertIn('master_update_signature_reset()', multipart)
        self.assertIn('master_update_check_real_bytes(upload.buf, upload.currentSize)', multipart)


if __name__ == "__main__":
    unittest.main()
