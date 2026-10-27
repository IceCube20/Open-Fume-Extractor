"""S3 network memory experiment preparation; never build or touch installed SDKs."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "s3_wifi_sdk", ROOT / "tools/boards/s3_wifi_memory_sdk/prepare.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)


class S3WifiMemorySDK(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base = self.root / "stock/esp32s3"
        self.candidate = self.root / "new/esp32s3"
        self.base.mkdir(parents=True)
        self.candidate.mkdir(parents=True)
        values = dict(SDK.PROFILE["preserve"], **{SDK.FLAG: "y"})
        values["CONFIG_SPIRAM_MODE_QUAD"] = "n"
        values["CONFIG_ALIAS"] = "y"
        self.header = "\n".join(f"#define {k} {'1' if v == 'y' else v}"
                                for k, v in values.items() if v != "n" and k != "CONFIG_ALIAS")
        self.header += "\n#define CONFIG_ALIAS CONFIG_SPIRAM_MODE_OCT\n"
        # Match the real SDK: common sdkconfig is QUAD/non-XIP, active header OCT/XIP.
        values["CONFIG_SPIRAM_MODE_QUAD"] = "y"
        values["CONFIG_SPIRAM_MODE_OCT"] = "n"
        values["CONFIG_SPIRAM_XIP_FROM_PSRAM"] = "n"
        (self.base / "sdkconfig").write_text("\n".join(f"{k}={v}" for k, v in values.items()))
        header = SDK.sdk_header(self.base)
        header.parent.mkdir(parents=True)
        header.write_text(self.header)
        versions = {"lib-builder": SDK.PROFILE["sources"]["."]["commit"],
                    "esp-idf": SDK.PROFILE["sources"]["esp-idf"]["commit"],
                    "arduino": SDK.PROFILE["sources"]["components/arduino"]["commit"],
                    "tinyusb": SDK.PROFILE["sources"]["components/arduino_tinyusb/tinyusb"]["commit"],
                    "example__component": "1.0.0"}
        version_text = "\n".join(f"{k}: {v}" for k, v in versions.items()) + "\n"
        for sdk in (self.base, self.candidate):
            (sdk.parent / "versions.txt").write_text(version_text)
            (sdk / "lib").mkdir()
            for name in ("libesp_wifi.a", "liblwip.a"):
                (sdk / "lib" / name).write_bytes(b"stock" if sdk == self.base else b"rebuilt")
        new_header = SDK.sdk_header(self.candidate)
        new_header.parent.mkdir(parents=True)
        new_header.write_text(self.header.replace(f"#define {SDK.FLAG} 1\n", ""))
        self.workspace = SDK.workspace(self.root / "experiment")
        SDK.prepare(self.workspace, self.base)

    def test_defaults_follow_active_octal_header_and_only_change_one_flag(self):
        old = SDK.parse_config((self.workspace / "baseline-r5.defaults").read_text())
        new = SDK.parse_config((self.workspace / "candidate-r5.defaults").read_text())
        self.assertEqual(old["CONFIG_SPIRAM_MODE_OCT"], "y")
        self.assertEqual(old["CONFIG_SPIRAM_MODE_QUAD"], "n")
        self.assertEqual(old["CONFIG_SPIRAM_XIP_FROM_PSRAM"], "y")
        self.assertEqual(old["CONFIG_ALIAS"], "y")
        self.assertEqual({k: v for k, v in new.items() if old.get(k) != v}, SDK.PROFILE["changes"])
        SDK.prepare(self.workspace, self.base)

    def test_candidate_preflight(self):
        SDK.audit(self.workspace, self.candidate)

    def test_header_only_changes_not_accepted(self):
        (self.candidate / "lib/libesp_wifi.a").write_bytes(b"stock")
        with self.assertRaisesRegex(ValueError, "Unrebuilt archive"):
            SDK.audit(self.workspace, self.candidate)

    def test_unrelated_config_change_rejected(self):
        header = SDK.sdk_header(self.candidate)
        header.write_text(header.read_text() + "#define CONFIG_OTHER_SETTING 1\n")
        with self.assertRaisesRegex(ValueError, "beyond the one"):
            SDK.audit(self.workspace, self.candidate)

    def test_xip_or_cache_regression_rejected(self):
        header = SDK.sdk_header(self.candidate)
        for key in ("CONFIG_SPIRAM_XIP_FROM_PSRAM", "CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE"):
            header.write_text(self.header.replace(f"#define {key} 1\n", "")
                              .replace(f"#define {key} 64", f"#define {key} 32"))
            with self.assertRaisesRegex(ValueError, "Unexpected active"):
                SDK.audit(self.workspace, self.candidate)

    def test_managed_version_change_rejected(self):
        path = self.candidate.parent / "versions.txt"
        path.write_text(path.read_text().replace("example__component: 1.0.0", "example__component: 1.0.1"))
        with self.assertRaisesRegex(ValueError, "versions differ"):
            SDK.audit(self.workspace, self.candidate)

    def test_unexpected_source_version_rejected(self):
        path = self.base.parent / "versions.txt"
        path.write_text(path.read_text().replace("2f7dcd862a", "0000000000"))
        with self.assertRaisesRegex(ValueError, "Wrong esp-idf"):
            SDK.prepare(self.workspace, self.base)

    def test_snapshot_tampering_rejected(self):
        snapshot = self.workspace / "baseline-qio_opi-sdkconfig.h"
        snapshot.write_text(snapshot.read_text() + "\n")
        with self.assertRaisesRegex(ValueError, "Baseline header changed"):
            SDK.audit(self.workspace, self.candidate)

    def test_unrelated_workspace_and_overwrite_rejected(self):
        path = self.root / "other"
        path.mkdir()
        (path / "user.txt").write_text("keep")
        with self.assertRaisesRegex(ValueError, "unrelated"):
            SDK.workspace(path)
        (self.workspace / "candidate-r5.defaults").write_text("user change")
        with self.assertRaisesRegex(ValueError, "Preserving other file"):
            SDK.prepare(self.workspace, self.base)

    def test_project_and_install_paths_rejected(self):
        for path in (ROOT / "build-sdk", self.root / "Arduino15/test"):
            with self.assertRaises(ValueError):
                SDK.workspace(path)

    def test_config_conflicts_and_alias_cycles_rejected(self):
        with self.assertRaisesRegex(ValueError, "Conflicting"):
            SDK.parse_config("CONFIG_A=y\n# CONFIG_A is not set\n")
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            SDK.resolve_aliases({"CONFIG_A": "CONFIG_B", "CONFIG_B": "CONFIG_A"})
        with self.assertRaisesRegex(ValueError, "Missing configuration alias"):
            SDK.resolve_aliases({"CONFIG_A": "CONFIG_B"})


if __name__ == "__main__":
    unittest.main()
