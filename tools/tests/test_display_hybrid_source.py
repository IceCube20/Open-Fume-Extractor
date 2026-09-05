"""Source-level integration guards; hardware/network timing requires target tests."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MASTER = ROOT / "OpenFumeExtractorMaster"
DISPLAYS = [ROOT / "Module" / ("DisplayModule_" + resolution)
            for resolution in ("320x480", "800x480")]


def read(path):
    return path.read_text(encoding="utf-8-sig")


class DisplayHybridIntegration(unittest.TestCase):
    def test_tunnel_copies(self):
        expected = read(MASTER / "src/bus/OfeDisplayTunnel.h")
        for display in DISPLAYS:
            self.assertEqual(expected, read(display / "src/OfeDisplayTunnel.h"))

    def test_display_implementation_copies(self):
        for file in ("OfeDisplayWifi.h", "OfeDisplayWifiUi.inc.h", "OfeDisplayMemory.h", "OfeSerialPortFont.h"):
            self.assertEqual(read(DISPLAYS[0] / "src" / file),
                             read(DISPLAYS[1] / "src" / file))

    def test_bus_extension_copies(self):
        for name in ("Rs485PeripheralBus.h", "Rs485PeripheralBus.cpp"):
            expected = read(MASTER / "src/bus" / name)
            for path in (ROOT / "Module").glob("*/src/" + name):
                actual = read(path)
                if name.endswith(".h"):
                    # Older modules intentionally have different JBC USB command enums.
                    self.assertIn("CAP_DISPLAY_HYBRID = 1UL << 25", actual)
                    self.assertEqual(expected.split("class Link {", 1)[1],
                                     actual.split("class Link {", 1)[1], str(path))
                else:
                    self.assertEqual(expected, actual, str(path))

    def test_display_integration_and_offline_access(self):
        for display in DISPLAYS:
            sketch = read(display / (display.name + ".ino"))
            setup = sketch[sketch.index("void setup()"):]
            self.assertLess(setup.index("display_wifi.begin("),
                            setup.index("start_rs485_task_if_enabled();"))
            self.assertIn("if (display_wifi.handleConfig(req)) return;", sketch)
            self.assertIn("CAP_DISPLAY_HYBRID", sketch)
            self.assertIn("OfeDisplayWifiUi::openEvent, LV_EVENT_CLICKED", sketch)
            self.assertIn('lv_small_button(system_body', sketch)
            self.assertIn("OfeDisplayWifiUi::isOpen()", sketch)
            self.assertIn("pending_display_event = DISPLAY_EVENT_NONE;", setup)

    def test_network_work_does_not_run_in_lvgl(self):
        transport = read(DISPLAYS[0] / "src/OfeDisplayWifi.h")
        ui = read(DISPLAYS[0] / "src/OfeDisplayWifiUi.inc.h")
        self.assertIn('xTaskCreatePinnedToCore(workerHook,"display-wifi"', transport)
        worker = transport[transport.index("void worker()"):]
        self.assertIn("WiFi.scanNetworks(true)", worker)
        self.assertIn("WiFi.hostByName", worker)
        self.assertIn('prefs.putBytes("config"', worker)
        self.assertIn('prefs.getBytes("config"', worker)
        self.assertIn("WiFi.persistent(false)", transport)
        self.assertNotIn("WiFi.begin", ui)
        self.assertNotIn("prefs.put", ui)
        self.assertIn("scan_revision_!=v.scan_revision || strcmp(networks_text_,v.networks)", ui)

    def test_wifi_memory_precedes_optional_draw_buffer(self):
        for display in DISPLAYS:
            sketch = read(display / (display.name + ".ino"))
            setup = sketch[sketch.index("void setup()"):]
            self.assertLess(setup.index("gfx->begin("), setup.index("display_wifi.prepareRadio();"))
            self.assertLess(setup.index("display_wifi.prepareRadio();"), setup.index("lvgl_init_ui();"))
            if "800x480" in display.name:
                self.assertLess(setup.index("display_wifi.prepareRadio();"),
                                setup.index("reserve_lvgl_draw_buffer_early("))
                self.assertLess(setup.index("init_psram_caches();"),
                                setup.index("reserve_lvgl_draw_buffer_early("))
                self.assertIn("#define LVGL_DRAW_BUFFER_ALLOW_PSRAM_FALLBACK 0", sketch)
                self.assertIn("constexpr size_t pool_bytes = LV_MEM_SIZE;", sketch)
                self.assertIn("static void* widget_pools[2]", sketch)
                self.assertIn("lv_mem_add_pool(candidate, pool_bytes)", sketch)
                self.assertIn("(64 * 1024U)", read(display / "ofe_lv_conf.h"))
                self.assertIn("LV_CONF_PATH", read(display / "build_opt.h"))
            else:
                self.assertIn("#define LVGL_DRAW_BUFFER_ALLOW_PSRAM_FALLBACK 1", sketch)
            self.assertIn("ofe_display_memory::allocateDraw(bytes_try, display_wifi.drawBufferReserve())", sketch)
        memory = read(DISPLAYS[0] / "src/OfeDisplayMemory.h")
        self.assertIn("heap_caps_get_free_size(caps) < reserve", memory)
        self.assertIn("heap_caps_free(p)", memory)

    def test_scan_retry_and_completion(self):
        transport = read(DISPLAYS[0] / "src/OfeDisplayWifi.h")
        self.assertIn("scan_requested_=scan_requested_ || scan", transport)
        self.assertIn("if (!scanning) { finishScan(result);", transport)
        self.assertIn("finishScan(count); scanning=false;", transport)
        self.assertIn("if (WiFi.status()!=WL_CONNECTED) {\n          WiFi.setAutoReconnect(false);", transport)
        self.assertIn("else if (shared_.result==5) shared_.result=0;", transport)
        self.assertIn("c.mode==WIRED && !scan && !scanning && radio", transport)

    def test_master_authentication_and_identity(self):
        master = read(MASTER / "src/MasterDisplayWifi.cpp")
        web = read(MASTER / "src/WebDisplayWifi.inc.h")
        self.assertIn("registry_->bindUidToAddress(uid,addr)", master)
        self.assertIn("rec->type!=MODULE_DISPLAY", master)
        self.assertIn("if (!derive(uid,key) || !decode", master)
        self.assertIn("inSession(msg", master)
        self.assertIn("p->wired_probes>=2", master)
        self.assertIn("if (!rec->caps) rec->caps=CAP_DISPLAY | CAP_DISPLAY_HYBRID;", master)
        self.assertIn("if (!web_require_auth()) return;", web)
        self.assertIn("web.method()==HTTP_POST", web)

    def test_backup_and_ota_boundaries(self):
        config = read(MASTER / "src/WebConfig.inc.h")
        scheduler = read(MASTER / "src/MasterScheduler.cpp")
        network = read(DISPLAYS[0] / "src/OfeDisplayWifi.h")
        self.assertIn('"display_pairing_root"', config)
        self.assertIn("restoreRoot(display_root.c_str())", config)
        self.assertIn("display configuration (redacted)", scheduler)
        self.assertNotIn("Display OTA requires RS485", scheduler)
        self.assertNotIn("frame.cmd<CMD_FW_REBOOT", network)
        self.assertIn("master_display_wifi.beginFirmware(addr)", scheduler)
        self.assertIn("wifi_update ? 3 : 1", scheduler)
        self.assertIn("if (d->updateWireless()) return false;", network)
        self.assertIn("if (update_transport_.load()==1) return false;", network)
        self.assertIn("!save_pending_ && !shared_.scanning && !update_transport_.load()", network)
        route = read(MASTER / "src/MasterDisplayWifi.cpp").split("bool MasterDisplayWifi::route(", 1)[1].split("bool MasterDisplayWifi::receive", 1)[0]
        self.assertIn("p->uid!=firmware_uid_", route)
        self.assertLess(route.index("firmware_addr_"), route.index("if (physical_)"))
        self.assertIn("normal transport try the physical bus", route)
        self.assertIn("return send(*p,DATA,&frame);", route)
        for display in DISPLAYS:
            sketch = read(display / (display.name + ".ino"))
            self.assertIn("fw_update_offset != fw_update_size", sketch)
            self.assertIn("requested_size != fw_update_size", sketch)
            self.assertIn("if (ok && wifi_update) fw_update_reboot_ms.store(millis());", sketch)
            self.assertIn("if (ok && !wifi_update)", sketch)
            self.assertIn("committed_ms) >= 12000UL", sketch)
        page = read(MASTER / "src/WebUpdate.inc.h")
        self.assertIn("data-transport=", page)
        self.assertIn("updateProgressText(p,transport,", page)

    def test_serial_symbol(self):
        import re
        font = read(DISPLAYS[0] / "src/OfeSerialPortFont.h")
        bitmap = font.split("bitmap[] = {", 1)[1].split("}", 1)[0]
        data = bytes(int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", bitmap))
        self.assertEqual(len(data), 34)
        for y in range(3, 22):
            for x in range(5, 19):
                # Rectangles from the supplied serial-port.svg.
                expected = ((7 <= x < 17 and 3 <= y < 5) or
                            (5 <= x < 19 and 5 <= y < 8) or
                            (8 <= x < 16 and 8 <= y < 14) or
                            (17 <= x < 19 and 9 <= y < 14) or
                            (5 <= x < 7 and 9 <= y < 14) or
                            (11 <= x < 13 and 15 <= y < 22))
                i = (y - 3) * 14 + x - 5
                self.assertEqual(bool(data[i // 8] & (0x80 >> (i % 8))), expected)
        self.assertIn("value.fallback=fallback", font.replace(" ", ""))
        for display in DISPLAYS:
            sketch = read(display / (display.name + ".ino"))
            self.assertIn("OFE_SYMBOL_SERIAL_PORT", sketch)
            self.assertIn("ofe_serial_port::font(UI_FONT_DEFAULT)", sketch)

    def test_manual_config_not_overwritten(self):
        transport = read(DISPLAYS[0] / "src/OfeDisplayWifi.h")
        handler = transport[transport.index("bool handleConfig("):transport.index("static uint32_t configHash(")]
        self.assertLess(handler.index("if (request.dst!=*address_) return true;"),
                        handler.index("Frame response;"))
        scheduler = read(MASTER / "src/MasterScheduler.cpp")
        ui = read(DISPLAYS[0] / "src/OfeDisplayWifiUi.inc.h")
        self.assertIn("reply.payload[0] == STATUS_OK && reply.payload[1]", scheduler)
        self.assertIn("c.from_master=0;", ui)
        self.assertIn("v.config.from_master=1;", ui)
        self.assertIn("get_u32_le(digest) != get_u32_le(reply.payload + 3)", scheduler)

    def test_large_scroll_and_icon_integration(self):
        for display in DISPLAYS:
            sketch = read(display / (display.name + ".ino"))
            self.assertIn("ofe_serial_port::center_icon(link_lbl, display_wifi.wireless());", sketch)
            self.assertIn("ofe_serial_port::center_icon(ui_header_link_labels[i], wifi);", sketch)
        large = read(DISPLAYS[1] / (DISPLAYS[1].name + ".ino"))
        self.assertIn('"selected_module COLD", false', large)
        self.assertIn('"detail_parse_scratch COLD", false', large)
        self.assertIn("lvgl_touch_indev = indev;", large)
        flags = large.split("const uint32_t heavy_flags =", 1)[1].split(";", 1)[0]
        self.assertIn("UI_DEFER_MODULE_DETAIL", flags)

    def test_large_renderer_and_ide_board(self):
        config = read(DISPLAYS[1] / "ofe_lv_conf.h")
        self.assertRegex(config, r"#define LV_OBJ_STYLE_CACHE\s+1\b")
        self.assertRegex(config, r"#define LV_USE_STDLIB_STRING\s+LV_STDLIB_CLIB\b")
        self.assertRegex(config, r"#define LV_DRAW_SW_CIRCLE_CACHE_SIZE\s+16\b")
        self.assertRegex(config, r"#define LV_MEM_SIZE\s+\(64 \* 1024U\)")
        board = dict(line.split("=", 1) for line in
                     read(ROOT / "tools/arduino_high_perf/boards.txt").splitlines()
                     if line and not line.startswith("#"))
        for key, value in {
            "build.boot": "qio", "build.flash_freq": "80m",
            "build.flash_size": "16MB", "build.psram_type": "opi",
            "build.f_cpu": "240000000L", "build.partitions": "default_8MB",
            "build.loop_core": "-DARDUINO_RUNNING_CORE=1",
            "build.event_core": "-DARDUINO_EVENT_RUNNING_CORE=1",
            "upload.maximum_size": "3342336", "upload.erase_cmd": "",
        }.items():
            self.assertEqual(board["ofe800." + key], value)
        self.assertIn("-DOFE_REQUIRE_RGB_HIGH_PERF_SDK=1", board["ofe800.build.defines"])

    def test_large_scroll_profiler_is_local_and_allocation_free(self):
        large = DISPLAYS[1]
        config = read(large / "ofe_lv_conf.h")
        self.assertRegex(config, r"#define LV_USE_PROFILER\s+1\b")
        self.assertRegex(config, r"#define LV_USE_PROFILER_BUILTIN\s+0\b")
        self.assertIn('LV_PROFILER_INCLUDE "ofe_lv_profiler.h"', config)
        small = read(DISPLAYS[0] / (DISPLAYS[0].name + ".ino"))
        self.assertNotIn("ofe_lv_profile_take", small)
        counters = read(large / "src/OfeLvProfileCounters.h")
        self.assertNotIn("malloc(", counters)
        self.assertNotIn("new ", counters)
        self.assertIn("if (--depth_[id] != 0) return;", counters)
        sketch = read(large / (large.name + ".ino"))
        handler = sketch.split("static uint32_t lvgl_timer_handler_profiled() {", 1)[1].split("\n}", 1)[0]
        self.assertLess(handler.index("lv_timer_handler()"), handler.index("perf_finish_window_if_due()"))
        self.assertIn("DRAW inclusive ms/frame", sketch)

    def test_large_tiles_keep_scanout_and_sram_reserve(self):
        large = read(DISPLAYS[1] / (DISPLAYS[1].name + ".ino"))
        self.assertIn("#define DISPLAY_RGB_BOUNCE_BUFFER_LINES 20", large)
        self.assertIn("#define DISPLAY_RGB_PCLK_HZ 16000000", large)
        self.assertIn("#define DISPLAY_LVGL_FULL_REFRESH 0", large)
        defaults = large.split("#ifndef DISPLAY_LVGL_LARGE_TILES", 1)[1].split("#endif", 1)[0]
        self.assertIn("#define DISPLAY_LVGL_LARGE_TILES 0", defaults)
        self.assertNotIn("#define DISPLAY_LVGL_LARGE_TILES 1", defaults)
        helper = large.split("static void prepare_large_lvgl_tiles(uint32_t screen_w) {", 1)[1].split("static void lvgl_init_ui()", 1)[0]
        self.assertIn("render_lines = 96", helper)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", helper)
        self.assertNotIn("MALLOC_CAP_INTERNAL", helper)
        self.assertIn("reinterpret_cast<uint16_t*>(lvgl_draw_buf)", helper)
        self.assertIn("lvgl_transfer_scratch_pixels = lvgl_buf_pixels", helper)
        self.assertNotIn("prepare_large_lvgl_tiles", read(DISPLAYS[0] / (DISPLAYS[0].name + ".ino")))

    def test_jbc_home_uses_station_list_without_intermediate_label(self):
        for display in DISPLAYS:
            sketch = read(display / (display.name + ".ino"))
            self.assertIn("+ home_jbc_station_models() +", sketch)
            self.assertNotIn("lv_set_text(ui_home_work, status.work_mask", sketch)
            self.assertIn("update_jbc_station_list(req, p);", sketch)
            self.assertIn("status.jbc_station_count != last_drawn_status.jbc_station_count", sketch)
            saver = sketch.split("static String screensaver_jbc_text() {", 1)[1].split("\n}\n", 1)[0]
            self.assertIn("portENTER_CRITICAL(&jbc_station_mux)", saver)
            self.assertIn("station = status.jbc_stations[index]", saver)

    def test_wifi_poll_only_copies_changed_config(self):
        for display in DISPLAYS:
            transport = read(display / "src/OfeDisplayWifi.h")
            poll = transport.split("bool poll(jbc_rs485::Frame& frame)", 1)[1].split("static void workerHook", 1)[0]
            self.assertNotIn("view()", poll)
            self.assertIn("if (config_revision_!=revision) next_config=shared_.config;", poll)
            self.assertIn("active_config_=next_config; config_revision_=revision;", poll)
            worker = transport.split("void worker()", 1)[1]
            self.assertNotIn("view()", worker)

    def test_authenticated_rejoin_does_not_force_offline(self):
        master = read(MASTER / "src/MasterDisplayWifi.cpp")
        proof = master.split("if (msg.kind==PROOF", 1)[1].split("// A lost READY", 1)[0]
        self.assertNotIn("rec->online=false", proof)
        self.assertIn("rec->last_seen_ms=now;", proof)
        scheduler = read(MASTER / "src/MasterScheduler.cpp")
        discovery = scheduler.split("void MasterScheduler::noticeDiscoveryResponse", 1)[1].split("int16_t MasterScheduler::busDiagIndex", 1)[0]
        self.assertIn("link_.lastRxWasNetwork()", discovery)
        self.assertIn("rec->caps != 0 && !wifi_join", discovery)

    def test_authenticated_wifi_ota_bulk_path_and_fallback(self):
        tunnel = read(MASTER / "src/bus/OfeDisplayTunnel.h")
        master = read(MASTER / "src/MasterDisplayWifi.cpp")
        update = read(MASTER / "src/WebUpdate.inc.h")
        self.assertIn("constexpr uint16_t BULK_DATA_MAX = 1024", tunnel)
        self.assertIn('ack ? "OFA1" : "OFB1"', tunnel)
        self.assertIn("decodeBulk(raw,n,key,ack)", master)
        self.assertIn("firmware_bulk_confirmed_=true", master)
        self.assertIn("module_update_send_legacy_chunks", update)
        self.assertIn("firmwareBulkConfirmed", update)
        for display in DISPLAYS:
            transport = read(display / "src/OfeDisplayWifi.h")
            sketch = read(display / (display.name + ".ino"))
            self.assertIn("decodeBulk(raw,n,active_config_.key,bulk)", transport)
            self.assertIn("handle_wifi_fw_bulk", sketch)
            self.assertIn("offset + n <= fw_update_offset", sketch)


if __name__ == "__main__":
    unittest.main()
