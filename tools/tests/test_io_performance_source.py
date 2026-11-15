"""Developer I/O timing observes live paths without changing their cadence."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"


class IoPerformanceTests(unittest.TestCase):
    def test_status_and_ha_build_finish_before_http_send(self):
        text = (SRC / "MasterMqtt.inc.h").read_text(encoding="utf-8")
        handler = text.split("static void web_handle_state()", 1)[1].split("static void web_handle_descriptors()", 1)[0]
        self.assertIn("HttpPerfPhase::HaBuild : HttpPerfPhase::StatusBuild", handler)
        self.assertIn("HttpPerfPhase::HaSend : HttpPerfPhase::StatusSend", handler)
        self.assertLess(handler.index("build.finish("), handler.index("HttpPerfScope send("))
        self.assertLess(handler.index("web.sendContent("), handler.index("send.finish();"))
        diagnostic = (SRC / "WebDiagnostics.inc.h").read_text(encoding="utf-8").split("static void web_handle_diagnostics_state()", 1)[1]
        self.assertIn("HttpPerfScope build(HttpPerfPhase::DiagnosticsBuild)", diagnostic)
        self.assertLess(diagnostic.index("build.finish("), diagnostic.index("HttpPerfScope send(HttpPerfPhase::DiagnosticsSend)"))

    def test_transport_probe_preserves_plain_tls_and_write_results(self):
        globals_ = (SRC / "MasterGlobals.inc.h").read_text(encoding="utf-8")
        for base in ("WiFiClient", "WiFiClientSecure"):
            self.assertIn(f"OfeMqttTransportClient<{base}, mqtt_transport_probe_enabled, mqtt_transport_probe_record>", globals_)
        probe = (SRC / "MqttTransportProbe.h").read_text(encoding="utf-8")
        self.assertIn("if (!Enabled()) return Base::write(data, size);", probe)
        self.assertIn("return sent;", probe)
        self.assertNotIn("while (", probe)
        self.assertNotIn("publish(", probe)

    def test_fields_and_timers_remain_developer_only(self):
        source = (SRC / "MasterDiagnostics.inc.h").read_text(encoding="utf-8")
        helper = source.split("static void append_http_perf_json(", 1)[1].split("\n}", 1)[0]
        self.assertIn("if (!developer_mode_enabled) return;", helper)
        self.assertIn("active_(developer_mode_enabled)", source)
        self.assertIn("developer_mode_enabled && mqtt_enabled", source)
        status = (SRC / "WebStatus.inc.h").read_text(encoding="utf-8")
        self.assertIn('class="stat-cell dev-only mqtt-perf-cell http-perf-cell"', status)
        self.assertIn("updateHttpPerf(d.http_perf,devMode)", status)

    @unittest.skipUnless(shutil.which("node"), "Node is needed for the actual diagnostic renderers")
    def test_actual_renderers(self):
        subprocess.run([shutil.which("node"), str(ROOT / "tools/tests/test_mqtt_performance.cjs")],
                       cwd=ROOT, check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
