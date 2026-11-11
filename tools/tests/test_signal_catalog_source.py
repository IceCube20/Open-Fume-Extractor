"""Keep complete routing/designer parity in the standard regression run."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "OpenFumeExtractorMaster/src"


class SignalCatalogueTests(unittest.TestCase):
    def test_editors_share_catalogue_and_alias_resolver(self):
        status = (SRC / "WebStatus.inc.h").read_text(encoding="utf-8")
        logic = (SRC / "WebLogic.inc.h").read_text(encoding="utf-8")
        self.assertIn("return ofeRoutingSignals({...d,modules},uiLang)", status)
        self.assertIn("ofeRoutingSignals(d,logicDe()?'de':'en')", logic)
        self.assertIn("return ofeSignalModuleName(m)", logic)
        self.assertNotIn("function logicDescriptorMeta(", logic)
        self.assertNotIn("function fanIoLogicChannels(", logic)

    def test_descriptors_are_used_with_matching_crc(self):
        logic = (SRC / "WebLogic.inc.h").read_text(encoding="utf-8")
        self.assertIn("liveState=logicHydrateSignalMetadata(d,descriptors)", logic)
        self.assertIn("cached.crc===Number(m.universal_descriptor_crc||0)", logic)
        self.assertIn("logicBuildSignals(liveState)", logic)

    @unittest.skipUnless(shutil.which("node"), "Node.js required for catalogue regression")
    def test_executable_catalogue_and_output_activity(self):
        result = subprocess.run([shutil.which("node"), "tools/tests/test_signal_catalog.cjs"],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
