import pathlib
import unittest


SRC = pathlib.Path(__file__).resolve().parents[2] / "OpenFumeExtractorMaster" / "src"


class RequestWaitIntegrationTests(unittest.TestCase):
    def test_discovery_guard_covers_physical_path_not_successful_network_route(self):
        bus = (SRC / "bus/Rs485PeripheralBus.cpp").read_text(encoding="utf-8")
        send = bus.split("void Link::send(", 1)[1].split("void Link::sendPhysical(", 1)[0]
        self.assertIn("send_route_(context_, frame)", send)
        self.assertIn("sendPhysical(frame);", send)
        self.assertNotIn("physical_send_guard_", send)
        physical = bus.split("void Link::sendPhysical(", 1)[1].split("void Link::sendWakePreamble(", 1)[0]
        self.assertLess(physical.index("physical_send_guard_(physical_guard_context_)"),
                        physical.index("stream_.write("))
        scheduler = (SRC / "MasterScheduler.cpp").read_text(encoding="utf-8")
        self.assertIn("link_.setPhysicalSendGuard(this", scheduler)
        request = scheduler.split("bool MasterScheduler::request(", 1)[1].split(
            "bool MasterScheduler::startAsyncDisplayRequest(", 1)[0]
        self.assertLess(request.index("link_.send(req);"), request.index("const uint32_t start = millis();"))

    def test_all_discovery_rounds_reserve_window_after_transmission(self):
        scheduler = (SRC / "MasterScheduler.cpp").read_text(encoding="utf-8")
        hotplug = scheduler.split("void MasterScheduler::pollHotplugDiscovery()", 1)[1].split(
            "void MasterScheduler::pollOfflineModules()", 1)[0]
        manual = scheduler.split("uint8_t MasterScheduler::autoAddressModules(", 1)[1].split(
            "// Wireless displays are not participants", 1)[0]
        for method in (hotplug, manual):
            self.assertLess(method.index("link_.send(req);"), method.index("reserveDiscoveryWindow();"))
        self.assertIn("millis() - hotplug_discovery_window_until_ms_", manual)
        self.assertNotIn("420UL", manual)

    def test_probe_wraps_real_synchronous_receive_loop(self):
        text = (SRC / "MasterScheduler.cpp").read_text(encoding="utf-8")
        request = text.split("bool MasterScheduler::request(", 1)[1].split(
            "bool MasterScheduler::startAsyncDisplayRequest(", 1
        )[0]
        self.assertIn("RequestWaitProbe wait_probe(micros(), link_.stats());", request)
        self.assertLess(request.index("wait_probe.sample(micros());"), request.index("if (link_.poll(resp))"))
        self.assertLess(request.index("if (physical) return false;"), request.index("wait_probe.finish("))
        self.assertIn("d.last_timeout_probe_valid = !wifi_display_active;", request)
        self.assertIn("d.last_timeout_budget_ms = timeout_ms;", request)
        self.assertIn("wait_probe.finish(micros(), link_.stats())", request)
        self.assertIn("d.last_timeout_probe.bad_seq = d.bad_seq - initial_bad_seq;", request)
        self.assertIn("d.last_timeout_probe.bad_cmd = d.bad_cmd - initial_bad_cmd;", request)
        self.assertIn("d.last_timeout_probe.late_responses = d.late_responses - initial_late;", request)

    def test_length_snapshot_belongs_to_current_wait_only(self):
        text = (SRC / "MasterScheduler.cpp").read_text(encoding="utf-8")
        request = text.split("bool MasterScheduler::request(", 1)[1].split(
            "bool MasterScheduler::startAsyncDisplayRequest(", 1
        )[0]
        self.assertIn("d.last_timeout_length_error = d.last_timeout_probe.bad_length", request)
        self.assertIn("? link_.lastLengthError() : LengthErrorSnapshot();", request)
        web = (SRC / "WebDiagnostics.inc.h").read_text(encoding="utf-8")
        gate = web.split("if (bd.last_timeout_probe_valid)", 1)[1].split(
            'json += F(",\\\"timeout_universal', 1
        )[0]
        self.assertIn("if (probe.bad_length)", gate)
        self.assertIn("bd.last_timeout_length_error", gate)
        self.assertNotIn("link_.lastLengthError()", web)

    def test_async_timeout_invalidates_old_synchronous_observation(self):
        text = (SRC / "MasterScheduler.cpp").read_text(encoding="utf-8")
        timeout = text.split("void MasterScheduler::recordRequestTimeout(", 1)[1].split(
            "void MasterScheduler::completeDisplayCacheResponse(", 1
        )[0]
        self.assertIn("d.last_timeout_probe_valid = false;", timeout)

    def test_version_and_physical_snapshots_use_current_wait_deltas(self):
        text = (SRC / "MasterScheduler.cpp").read_text(encoding="utf-8")
        request = text.split("bool MasterScheduler::request(", 1)[1].split(
            "bool MasterScheduler::startAsyncDisplayRequest(", 1
        )[0]
        self.assertIn("d.last_timeout_version_error = d.last_timeout_probe.bad_version", request)
        self.assertIn("? link_.lastVersionError() : VersionErrorSnapshot();", request)
        self.assertIn("d.last_timeout_physical_frame = d.last_timeout_probe.rx_frames", request)
        self.assertIn("? link_.lastPhysicalFrame() : PhysicalFrameSnapshot();", request)
        self.assertIn("d.last_timeout_unmatched_frame = unmatched_frame;", request)
        self.assertIn("RequestUnmatchedFrame unmatched_frame;", request)
        for reason in ("ASYNC_DISPLAY", "COMMAND", "LATE", "SEQUENCE", "OTHER_ADDRESS"):
            self.assertIn("WAIT_FRAME_" + reason, request)
        self.assertLess(request.index("const bool rx_via_network"), request.index("handleAsyncDisplayFrame(resp)"))
        parser = (SRC / "bus/Rs485PeripheralBus.cpp").read_text(encoding="utf-8")
        self.assertIn("last_physical_frame_ = {out.dst, out.src, out.seq, out.cmd, out.len, filtered};", parser)
        self.assertLess(parser.index("last_physical_frame_ ="), parser.index("if (filtered) continue;"))

    def test_detailed_json_is_server_gated(self):
        text = (SRC / "WebDiagnostics.inc.h").read_text(encoding="utf-8")
        start = text.index("    if (developer_mode_enabled) {")
        end = text.index('json += F(",\\\"timeout_universal', start)
        gate = text[start:end]
        self.assertIn("io_timeouts", gate)
        self.assertIn("timeout_probe", gate)
        self.assertIn("if (bd.last_timeout_probe_valid)", gate)
        for field in ("version_error", "last_physical_frame", "unmatched_frame"):
            self.assertIn(field, gate)
            self.assertNotIn(field, text[:start])
        self.assertNotIn("link_.lastVersionError()", text)
        self.assertNotIn("link_.lastPhysicalFrame()", text)
        self.assertNotIn("io_timeouts", text[:start])

    def test_get_and_set_io_are_counted_without_changing_io_class(self):
        text = (SRC / "MasterScheduler.cpp").read_text(encoding="utf-8")
        function = text.split("static void bus_diag_record_timeout_class(", 1)[1].split(
            "static void bus_diag_record_latency_class(", 1
        )[0]
        self.assertIn("if ((cmd & 0x7F) == CMD_GET_IO) ++d.timeout_get_io;", function)
        self.assertIn("if ((cmd & 0x7F) == CMD_SET_IO) ++d.timeout_set_io;", function)
        self.assertIn("case CMD_GET_IO:", function)
        self.assertIn("case CMD_SET_IO:", function)
        self.assertIn("++d.timeout_io;", function)


if __name__ == "__main__":
    unittest.main()
