import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Module" / "JbcBusModule" / "JbcBusModule.ino"


def handler_body(source: str, command: str) -> str:
    marker = f"if (ctrl == jbc_fe::{command}"
    start = source.index(marker)
    next_handler = source.find("\n  if (ctrl == ", start + len(marker))
    return source[start : next_handler if next_handler >= 0 else len(source)]


class JbcFaeAfterrunProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_intake_write_tracks_work_and_stand_separately(self):
        body = handler_body(self.source, "M_W_INTAKEACTIVATION")
        self.assertRegex(body, r"intake\s*=\s*len\s*>=\s*3")
        self.assertIn("intake == 0 ? work_mask : stand_mask", body)

    def test_intake_read_returns_latched_request_not_afterrun_output(self):
        body = handler_body(self.source, "M_R_INTAKEACTIVATION")
        self.assertIn("intake == 0 ? work_mask : stand_mask", body)
        self.assertNotIn("extractor_output_active ?", body)
        self.assertRegex(body, re.compile(r"uint8_t\s+out\[\]\s*=\s*\{\s*active\s*,\s*port\s*,\s*intake\s*\}"))


if __name__ == "__main__":
    unittest.main()
