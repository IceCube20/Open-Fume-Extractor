import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MasterHardeningSourceTest(unittest.TestCase):
    def read(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_preferences_begin_failure_releases_mutex(self):
        source = self.read("OpenFumeExtractorMaster/src/MasterGlobals.inc.h")
        begin = source[source.index("class SerializedPreferences"):source.index("static SerializedPreferences")]
        self.assertIn("const bool ok = Preferences::begin", begin)
        self.assertIn("if (!ok)", begin)
        self.assertIn("xSemaphoreGiveRecursive(mutex_)", begin)

    def test_mqtt_client_is_owned_by_mqtt_task(self):
        config = self.read("OpenFumeExtractorMaster/src/WebConfig.inc.h")
        network = self.read("OpenFumeExtractorMaster/src/MasterNetwork.inc.h")
        mqtt = self.read("OpenFumeExtractorMaster/src/MasterMqtt.inc.h")
        self.assertNotIn("mqtt_client.disconnect()", config)
        self.assertNotIn("mqtt_configure_client()", config)
        self.assertIn("mqtt_reconfigure_requested = true", network)
        self.assertIn("if (mqtt_reconfigure_requested)", mqtt)

    def test_scan_is_csrf_protected_post(self):
        server = self.read("OpenFumeExtractorMaster/src/WebServer.inc.h")
        status = self.read("OpenFumeExtractorMaster/src/WebStatus.inc.h")
        self.assertIn('web.on("/scan", HTTP_POST', server)
        self.assertNotIn('web.on("/scan", HTTP_GET', server)
        self.assertIn("fetch('/scan',{method:'POST'", status)

    def test_public_developer_mode_is_disabled(self):
        config = self.read("OpenFumeExtractorMaster/src/MasterBuildConfig.h")
        self.assertIn("#define OFE_DEVELOPER_MODE_ENABLE 0", config)
        self.assertNotIn("OFEdevelop123", config)

    def test_default_password_is_setup_only(self):
        config = self.read("OpenFumeExtractorMaster/src/MasterBuildConfig.h")
        network = self.read("OpenFumeExtractorMaster/src/MasterNetwork.inc.h")
        security = self.read("OpenFumeExtractorMaster/src/WebSecurity.inc.h")
        self.assertIn('#define MASTER_DEFAULT_PASSWORD "extractor123"', config)
        self.assertIn("web_password_change_required = web_pass == MASTER_DEFAULT_PASSWORD", network)
        self.assertIn('path == "/config" || path == "/config/save"', security)
        self.assertIn("web_password_change_blocked()", security)

    def test_telemetry_uses_monotonic_uptime(self):
        bus = self.read("OpenFumeExtractorMaster/src/bus/Rs485PeripheralBus.h")
        self.assertIn("esp_timer_get_time", bus)
        for path in ROOT.glob("Module/*/*.ino"):
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("millis() / 1000UL", text, str(path))

    def test_jbc_deadline_is_rollover_safe(self):
        usb = self.read("Module/JbcUsbModule/JbcUsbModule.ino")
        self.assertNotIn("next_sold_peripheral_config_poll_ms<=now", usb)
        self.assertIn("(int32_t)(now - next_sold_peripheral_config_poll_ms) >= 0", usb)

    def test_module_registry_does_not_block_runtime_tasks(self):
        header = self.read("OpenFumeExtractorMaster/src/ModuleRegistry.h")
        source = self.read("OpenFumeExtractorMaster/src/ModuleRegistry.cpp")
        self.assertNotIn("WriteGuard", header)
        self.assertNotIn("ReaderView", header)
        self.assertNotIn("portMAX_DELAY", source)

    def test_cli_and_leds_run_before_module_ota_pump(self):
        loop = self.read("OpenFumeExtractorMaster/src/MasterLoop.inc.h")
        self.assertLess(loop.index("serial_cli_tick();"), loop.index("module_update_pump();"))
        self.assertLess(loop.index("ofe_status_leds.tick();"), loop.index("module_update_pump();"))
        self.assertLess(loop.index("module_update_pump();"), loop.index("scheduler.tick();"))

    def test_wifi_display_ota_uses_bounded_burst(self):
        config = self.read("OpenFumeExtractorMaster/src/MasterBuildConfig.h")
        update = self.read("OpenFumeExtractorMaster/src/WebUpdate.inc.h")
        self.assertIn("#define MODULE_FW_DISPLAY_PUMP_FRAMES_PER_LOOP 1", config)
        self.assertIn("#define MODULE_FW_WIFI_DISPLAY_PUMP_FRAMES_PER_LOOP 4", config)
        self.assertIn("master_display_wifi.firmwareWireless(module_update_addr)", update)
        self.assertIn("MODULE_FW_WIFI_PUMP_BUDGET_MS", update)

    def test_bus_quality_is_visible_without_developer_mode(self):
        status = self.read("OpenFumeExtractorMaster/src/WebStatus.inc.h")
        self.assertNotIn('class="dev-only" data-i18n="comm_quality"', status)
        self.assertNotIn('<td class="dev-only ${cc}" title="${ct}">${cq}</td>', status)

    def test_large_display_uses_full_module_list_and_screensaver_height(self):
        display = self.read("Module/DisplayModule_800x480/DisplayModule_800x480.ino")
        self.assertIn("lv_obj_set_size(ui_module_list, 780, 364);", display)
        self.assertIn("show_update_notice ? 330 : 364", display)
        self.assertIn("lv_obj_set_pos(ui_screensaver_hint, shift_x, 412 + shift_y);", display)

    def test_web_status_requests_do_not_overlap(self):
        shell = self.read("OpenFumeExtractorMaster/src/WebShell.inc.h")
        status = self.read("OpenFumeExtractorMaster/src/WebStatus.inc.h")
        led = self.read("OpenFumeExtractorMaster/src/MasterMqtt.inc.h")
        self.assertIn("fetch('/state/led'", shell)
        self.assertNotIn("fetch('/state',{cache:'no-store'}).then", shell)
        self.assertIn("let stateLoadBusy=false", status)
        self.assertIn("finally{stateLoadBusy=false}", status)
        self.assertIn('json += ",\\\"developer_mode\\\":"', led)

    def test_wifi_displays_remain_visible_in_bus_diagnostics(self):
        scheduler = self.read("OpenFumeExtractorMaster/src/MasterScheduler.cpp")
        diagnostics = self.read("OpenFumeExtractorMaster/src/WebDiagnostics.inc.h")
        tx = scheduler.split("void MasterScheduler::busDiagRecordTx", 1)[1].split("void MasterScheduler::busDiagRecordRx", 1)[0]
        rx = scheduler.split("void MasterScheduler::busDiagRecordRx", 1)[1].split("bool MasterScheduler::busModuleDiag", 1)[0]
        self.assertNotIn("lastTxWasNetwork()) return", tx)
        self.assertNotIn("lastRxWasNetwork()) return", rx)
        self.assertIn('\\\"transport\\\"', diagnostics)
        self.assertIn("m.transport==='wifi'?'WLAN':'RS485'", diagnostics)


if __name__ == "__main__":
    unittest.main()
