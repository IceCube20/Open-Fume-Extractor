import json
import unittest
from tools.tests.display_ui_invariants import FIXTURE, fingerprints


class PreservedDisplayUi(unittest.TestCase):
    def test_screensaver_live_updates_and_shared_helpers_unchanged(self):
        expected = json.loads(FIXTURE.read_text())
        for name, blocks in fingerprints().items():
            for symbol, digest in blocks.items():
                with self.subTest(display=name, function=symbol):
                    self.assertEqual(expected[name][symbol], digest)
