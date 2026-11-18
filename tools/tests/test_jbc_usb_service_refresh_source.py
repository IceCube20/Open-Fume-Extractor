from pathlib import Path
import unittest

SRC = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebStatus.inc.h'


class JbcServiceRefreshTests(unittest.TestCase):
    def test_live_update_uses_stable_service_controls(self):
        text = SRC.read_text(encoding='utf-8')
        update = text.split('function updateDetails(d){', 1)[1].split('function ', 1)[0]
        self.assertIn('jbcUsbRefreshSoldStation(m,kind);', update)
        self.assertNotIn("ssd.innerHTML=kind=='SOLD'", update)
        refresh = text.split('function jbcUsbRefreshSoldStation(m,kind){', 1)[1].split('function jbcUsbServiceOptions(', 1)[0]
        self.assertIn("host.querySelector('.jbu-station-live')", refresh)
        self.assertIn('host.dataset.stationIdentity!==identity', refresh)
        self.assertIn('values.innerHTML=jbcUsbStationDetailBody', refresh)
        self.assertIn('control.disabled!==disabled', refresh)


if __name__ == '__main__':
    unittest.main()
