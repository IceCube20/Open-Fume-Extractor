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
            self.assertIn("monotonic_uptime_seconds()", text, str(path))
            self.assertNotRegex(text, r"put_u32_le\([^\n]*millis\(\)\s*/\s*1000", str(path))

    def test_jbc_deadline_is_rollover_safe(self):
        usb = self.read("Module/JbcUsbModule/JbcUsbModule.ino")
        self.assertNotIn("next_sold_peripheral_config_poll_ms<=now", usb)
        self.assertIn("(int32_t)(now - next_sold_peripheral_config_poll_ms) >= 0", usb)

    def test_web_status_requests_do_not_overlap(self):
        shell = self.read("OpenFumeExtractorMaster/src/WebShell.inc.h")
        status = self.read("OpenFumeExtractorMaster/src/WebStatus.inc.h")
        led = self.read("OpenFumeExtractorMaster/src/MasterMqtt.inc.h")
        self.assertIn("fetch('/led_state'", shell)
        self.assertNotIn("fetch('/state/led'", shell)
        self.assertIn("window.addEventListener('pageshow',refreshShellDevMode)", shell)
        self.assertIn("data-enabled='", shell)
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

    def test_auto_addressing_yields_and_unlocks_before_rescan(self):
        scheduler = self.read("OpenFumeExtractorMaster/src/MasterScheduler.cpp")
        auto = scheduler.split("uint8_t MasterScheduler::autoAddressModules", 1)[1].split(
            "void MasterScheduler::pushOutputIfNeeded", 1
        )[0]
        discovery = auto.split("for (uint8_t round = 0; round < 8; ++round)", 1)[1].split(
            "uint8_t changed = 0", 1
        )[0]
        readdress = auto.split("auto readdress_discovered", 1)[1].split(
            "// Dependency-aware re-addressing", 1
        )[0]
        self.assertIn("delay(1);", discovery)
        self.assertIn("SchedulerBusLock bus_lock", readdress)
        self.assertIn("return ok;", readdress)
        self.assertIn("const bool ok = readdress_discovered(i, next_addr);", auto)
        self.assertLess(
            auto.index("const bool ok = readdress_discovered(i, next_addr);"),
            auto.index("scanAddress(next_addr);"),
        )

    def test_offline_events_are_session_only(self):
        status = self.read("OpenFumeExtractorMaster/src/WebStatus.inc.h")
        scheduler = self.read("OpenFumeExtractorMaster/src/MasterScheduler.cpp")
        loop = self.read("OpenFumeExtractorMaster/src/MasterLoop.inc.h")
        self.assertIn("rec->timeout_count = 0;", status)
        self.assertNotIn("getUShort(module_snapshot_key(i, 'o').c_str()", status)
        self.assertNotIn("putUShort(module_snapshot_key(saved, 'o').c_str()", status)
        self.assertIn("remove(module_snapshot_key(i, 'o').c_str())", status)
        self.assertIn("remove(module_snapshot_key(saved, 'o').c_str())", status)
        self.assertIn("rec->timeout_count++;", scheduler)
        self.assertNotIn("module_history_dirty_", scheduler)
        self.assertNotIn("consumeModuleHistoryPersistDue", loop)

    def test_jbc_usb_cannot_be_decoded_as_fae_settings(self):
        scheduler = self.read("OpenFumeExtractorMaster/src/MasterScheduler.cpp")
        read_state = scheduler.split("bool MasterScheduler::readJbcState", 1)[1].split(
            "bool MasterScheduler::readJbcUsbState", 1
        )[0]
        self.assertIn("if (!target || !(target->caps & CAP_JBC_BUS)) return false;", read_state)
        self.assertLess(
            read_state.index("if (!target || !(target->caps & CAP_JBC_BUS)) return false;"),
            read_state.index("if (!request(addr, CMD_GET_STATE"),
        )

    def test_shared_address_families_have_deterministic_order(self):
        scheduler = self.read("OpenFumeExtractorMaster/src/MasterScheduler.cpp")
        ordering = scheduler.split("static uint8_t compact_address_type_priority", 1)[1].split(
            "static bool discovered_before_for_compact_address", 1
        )[0]
        for token in (
            "MODULE_JBC_BUS", "MODULE_JBC_USB", "MODULE_FAN_IO",
            "MODULE_FAN_IO_PRO", "CAP_DISPLAY_320X480", "CAP_DISPLAY_800X480",
        ):
            self.assertIn(token, ordering)
        self.assertIn("resp.len >= 18", scheduler)
        self.assertIn("DISC temporary uid=", scheduler)

    def test_wireless_displays_participate_in_address_assignment(self):
        scheduler = self.read("OpenFumeExtractorMaster/src/MasterScheduler.cpp")
        wifi = self.read("OpenFumeExtractorMaster/src/MasterDisplayWifi.cpp")
        self.assertIn("master_display_wifi.active(rec.addr)", scheduler)
        self.assertIn("request(found[index].addr, CMD_SET_ADDRESS", scheduler)
        self.assertIn("master_display_wifi.rebindAddress", scheduler)
        self.assertIn("bool MasterDisplayWifi::rebindAddress", wifi)

    def test_small_display_long_runtime_hardening(self):
        display = self.read("Module/DisplayModule_320x480/DisplayModule_320x480.ino")
        memory = self.read("Module/DisplayModule_320x480/src/OfeDisplayMemory.h")
        wifi = self.read("Module/DisplayModule_320x480/src/OfeDisplayWifi.h")
        self.assertIn("Reset reason:", display)
        self.assertIn("SMALL HEALTH:", display)
        self.assertIn("RUNTIME_RESERVE = 136U * 1024U", memory)
        self.assertIn("WiFi.useStaticBuffers(true)", wifi)
        self.assertIn("SCREENSAVER_REFRESH_MIN_MS = 750UL", display)
        self.assertIn("SCREENSAVER_REFRESH_MAX_MS = 5000UL", display)
        self.assertIn("screensaver_refresh_pending", display)
        self.assertIn("resolve_due && (address_missing || !connected_)", wifi)

    def test_all_module_factory_addresses_start_at_family_base(self):
        expected = {
            "Module/JbcBusModule/JbcBusModule.ino": "module_addr = 0x10",
            "Module/JbcUsbModule/JbcUsbModule.ino": "DEFAULT_MODULE_ADDR = 0x10",
            "Module/FanIoModule/FanIoModule.ino": "module_addr = 0x20",
            "Module/FanIoProModule/FanIoProModule.ino": "module_addr = 0x20",
            "Module/WellerZeroSmogModule/WellerZeroSmogModule.ino": "DEFAULT_MODULE_ADDR = 0x30",
            "Module/DisplayModule_320x480/DisplayModule_320x480.ino": "DEFAULT_MODULE_ADDR = 0x40",
            "Module/DisplayModule_800x480/DisplayModule_800x480.ino": "DEFAULT_MODULE_ADDR = 0x40",
            "Module/UniversalRs232Module/UniversalRs232Module.ino": "DEFAULT_MODULE_ADDR = 0x50",
            "Module/ModbusRtuModule/ModbusRtuModule.ino": "DEFAULT_MODULE_ADDR = 0x60",
        }
        for path, declaration in expected.items():
            with self.subTest(module=path):
                self.assertIn(declaration, self.read(path))

    def test_module_power_save_is_consistent_and_discovery_safe(self):
        bus = self.read("OpenFumeExtractorMaster/src/bus/Rs485PeripheralBus.h")
        scheduler = self.read("OpenFumeExtractorMaster/src/MasterScheduler.cpp")
        leds = self.read("OpenFumeExtractorMaster/src/OfeStatusLed.h")
        config = self.read("OpenFumeExtractorMaster/src/WebConfig.inc.h")
        self.assertIn("CMD_POWER_SAVE = 0x14", bus)
        self.assertIn("CAP_POWER_SAVE = 1UL << 26", bus)
        self.assertIn("void sendWakePreamble", bus)
        self.assertIn("setEcoMode(bool active", leds)
        self.assertIn("OFE_LED_PURPLE_WHITE_BREATH", leds)
        self.assertIn("OFE- und EVT-LED behalten ihren normalen Status", config)

        tick = scheduler.split("void MasterScheduler::tick()", 1)[1]
        self.assertLess(
            tick.index("hotplug_discovery_window_until_ms_ = 0;"),
            tick.index("updatePowerSave(now);"),
        )
        discovery = scheduler.split("void MasterScheduler::noticeDiscoveryResponse", 1)[1].split(
            "void MasterScheduler::setLedConfig", 1
        )[0]
        self.assertNotIn("rec->light_sleep", discovery)
        power_save = scheduler.split("void MasterScheduler::updatePowerSave", 1)[1].split(
            "void MasterScheduler::broadcastLedSync", 1
        )[0]
        self.assertIn("|| rec.eco_mode) continue;", power_save)
        self.assertIn("last_power_save_apply_ms_", power_save)
        self.assertIn("setModulePowerSave(rec, true);", power_save)
        self.assertNotIn("allow_light_sleep", power_save)
        self.assertNotIn("sendWakePreamble", scheduler)
        idle_check = scheduler.split("bool MasterScheduler::powerSaveIdle() const", 1)[1].split(
            "void MasterScheduler::updatePowerSave", 1
        )[0]
        self.assertIn("rec.output_status_valid && rec.output_enabled", idle_check)
        self.assertIn("rec.io_output_mask", idle_check)
        led_api = self.read("OpenFumeExtractorMaster/src/MasterMqtt.inc.h")
        web_status = self.read("OpenFumeExtractorMaster/src/WebStatus.inc.h")
        self.assertIn('json += ",\\\"master_eco\\\":"', led_api)
        self.assertIn('json += ",\\\"eco\\\":"', led_api)
        self.assertIn("kind:'purplewhite'", web_status)
        self.assertIn("!!d.master_eco", web_status)
        self.assertIn("!!m.eco", web_status)
        self.assertIn("moduleEcoBadgeHtml", web_status)
        self.assertIn("applyModuleEcoBadges(d)", web_status)
        self.assertIn("module-eco-badge", web_status)
        self.assertIn("master_eco_icon", web_status)
        self.assertIn("document.getElementById('master_eco_icon')", web_status)
        self.assertNotIn("let me=el('master_eco_icon')", web_status)
        self.assertIn("led.insertAdjacentHTML('afterend',html)", web_status)
        self.assertIn("ofeLedGlow(e,c,1);return}if(st.kind==='blink'", web_status)
        diagnostics = self.read("OpenFumeExtractorMaster/src/WebDiagnostics.inc.h")
        self.assertIn('json += F(",\\\"power_save_active\\\":")', diagnostics)
        self.assertIn('json += F(",\\\"eco_mode\\\":")', diagnostics)
        self.assertIn("diag_master_eco_icon", diagnostics)
        self.assertIn("diag-eco-banner", diagnostics)
        for path in ROOT.glob("Module/*/*.ino"):
            text = path.read_text(encoding="utf-8")
            with self.subTest(module=str(path)):
                self.assertIn("CAP_POWER_SAVE", text)
                self.assertIn("ofe_handle_power_save_command", text)

        for path in (
            "Module/FanIoModule/FanIoModule.ino",
            "Module/FanIoProModule/FanIoProModule.ino",
        ):
            fan_io = self.read(path)
            self.assertNotIn("OfeModulePowerSave", fan_io)
            self.assertNotIn("module_power_save.service", fan_io)
            command_handler = fan_io.split("if (ofe_handle_power_save_command", 1)[1].split(
                "return;", 1
            )[0]
            self.assertIn("false,", command_handler)
            self.assertFalse((ROOT / pathlib.Path(path).parent / "src/OfeModulePowerSave.h").exists())

        for path in (
            "Module/DisplayModule_320x480/DisplayModule_320x480.ino",
            "Module/DisplayModule_800x480/DisplayModule_800x480.ino",
        ):
            display = self.read(path)
            self.assertIn('#include "SproutIcon.h"', display)
            self.assertIn("lv_update_eco_indicator_ui", display)
            self.assertIn("ui_screensaver_eco_icon", display)


if __name__ == "__main__":
    unittest.main()
