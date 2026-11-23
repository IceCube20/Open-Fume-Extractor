from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2] / "OpenFumeExtractorMaster"


class StartupScanIntegrationTests(unittest.TestCase):
    def test_all_mcus_arm_after_network_and_call_recovery_outside_ota(self):
        for name in ("OpenFumeExtractorMCU_S3", "OpenFumeExtractorMCU_P4", "OpenFumeExtractorMCU_P4_POE"):
            with self.subTest(mcu=name):
                src = ROOT / name / "src"
                setup = (src / "MasterSetup.inc.h").read_text(encoding="utf-8")
                self.assertLess(setup.index("web_begin();"), setup.index("scheduler.requestStartupScan();"))
                scheduler = (src / "MasterScheduler.cpp").read_text(encoding="utf-8")
                tick = scheduler.split("void MasterScheduler::tick()", 1)[1]
                scan_work = tick.split("if (!ota_active) {", 1)[1].split("uint32_t fast_poll_interval_ms", 1)[0]
                self.assertLess(scan_work.index("pollStartupScan();"), scan_work.index("pollHotplugDiscovery();"))
                self.assertLess(tick.index("if (hotplug_discovery_window_until_ms_ != 0)"), tick.index("pollStartupScan();"))

    def test_no_unknown_jbc_shortcuts_but_known_station_polling_preserved(self):
        src = ROOT / "OpenFumeExtractorMCU_S3/src"
        header = (src / "MasterScheduler.h").read_text(encoding="utf-8")
        cpp = (src / "MasterScheduler.cpp").read_text(encoding="utf-8")
        self.assertNotIn("JBC_DEFAULT_", header)
        self.assertNotIn("last_default_jbc_", cpp)
        self.assertIn("#define JBC_DISCONNECTED_FAST_POLL_PER_MODULE_MS 500UL", header)
        self.assertIn("#define HOTPLUG_DISCOVERY_INTERVAL_MS 120000UL", header)
        synchronous_scan = cpp.split("void MasterScheduler::scanKnownModules()", 1)[1].split(
            "void MasterScheduler::requestScanKnownModules", 1)[0]
        self.assertIn("startup_scan_recovery_.cancel();", synchronous_scan)


if __name__ == "__main__":
    unittest.main()
