"""Guard the session-bound bulk recovery and its developer-only diagnostics."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "OpenFumeExtractorMaster/src"


class WifiOtaRecoveryTests(unittest.TestCase):
    def test_success_and_failure_use_the_recovery_policy(self):
        source = (SRC / "MasterDisplayWifi.cpp").read_text(encoding="utf-8-sig")
        bulk = source.split("bool MasterDisplayWifi::firmwareBulkChunk", 1)[1].split(
            "bool MasterDisplayWifi::rebindAddress", 1)[0]
        self.assertIn("firmwareBulkSucceeded();", bulk)
        self.assertIn("firmwareBulkFailed();", bulk)
        self.assertIn("ack.offset==offset+len", bulk)
        self.assertIn("p->uid!=firmware_uid_", bulk)
        self.assertIn("constexpr uint8_t attempts=3;", bulk)
        self.assertIn("constexpr uint32_t ack_timeout_ms=750;", bulk)

    def test_actual_transport_and_fallbacks_are_reported(self):
        source = (SRC / "WebUpdate.inc.h").read_text(encoding="utf-8-sig")
        self.assertIn("module_update_last_bulk=bulk_ok;", source)
        self.assertIn("module_update_bulk_fallbacks++;", source)
        self.assertIn("module_update_last_bulk = false;", source)
        self.assertIn("module_update_packet_limit = 0;", source)
        self.assertIn("(s.wifi_bulk?'Bulk':'OFE')", source)
        self.assertIn("if(!DEV_MODE||!s)return '';", source)
        self.assertIn("module_update_send_legacy_chunks(target_addr,target_offset,frame,n)", source)

    def test_bulk_ack_validation_reasons_and_unclaimed_packets_are_observed(self):
        source = (SRC / "MasterDisplayWifi.cpp").read_text(encoding="utf-8-sig")
        for name in ("Tx", "TxFail", "Rx", "Invalid", "Endpoint", "Session", "Request",
                     "Offset", "Status", "Timeout", "Unclaimed", "ReadFail"):
            self.assertIn(f"ofe_wifi_ota::{name}", source)
        self.assertIn("firmware_bulk_diagnostics_.reset();", source)
        update = (SRC / "WebUpdate.inc.h").read_text(encoding="utf-8-sig")
        self.assertIn("ACK-Diag tx/rx", update)
        self.assertIn("if(!DEV_MODE||!d)return '';", update)
        self.assertIn("bulk_diag.poll_gap_max_us", update)


if __name__ == "__main__":
    unittest.main()
