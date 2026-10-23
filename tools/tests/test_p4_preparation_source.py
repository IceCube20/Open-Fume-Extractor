"""Isolated P4 preparation guards; no physical-board validation claimed."""
from pathlib import Path
import hashlib
import json
import re
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[2]
P4 = ROOT / "Module/DisplayModule_P4_800x480"
S3 = ROOT / "Module/DisplayModule_800x480"
PREPARE = runpy.run_path(str(ROOT / "tools/boards/prepare_elecrow_p4.py"))


class P4Preparation(unittest.TestCase):
    def test_no_neopixel_backend_without_a_physical_led_pin(self):
        led = (P4 / "src/OfeStatusLed.h").read_text()
        guard = "#if OFE_STATUS_LED_ENABLE && OFE_STATUS_LED_PIN >= 0 && OFE_STATUS_LED_COUNT > 0"
        self.assertEqual(led.count(guard), 5)
        self.assertNotIn("#if OFE_STATUS_LED_ENABLE\n", led)

    def test_ppa_alignment_and_software_fallback_configuration(self):
        source = (P4 / (P4.name + ".ino")).read_text()
        config = (P4 / "ofe_lv_conf.h").read_text()
        memory = (P4 / "src/OfeDisplayMemory.h").read_text()
        self.assertIn("#define LV_USE_PPA OFE_P4_PPA", config)
        self.assertIn("#define LV_USE_PPA_IMG OFE_P4_PPA_IMG", config)
        self.assertIn("#define LV_DRAW_BUF_ALIGN OFE_P4_DRAW_ALIGNMENT", config)
        self.assertIn("#define CONFIG_LV_DRAW_BUF_ALIGN LV_DRAW_BUF_ALIGN", config)
        # No padded stride: upstream PPA fill uses header width as physical width.
        self.assertIn("#define LV_DRAW_BUF_STRIDE_ALIGN                1", config)
        self.assertIn("#define LV_USE_OS   LV_OS_NONE", config)
        self.assertIn("heap_caps_aligned_alloc(OFE_P4_DRAW_ALIGNMENT, bytes, caps)", memory)
        self.assertIn("lvgl_buf_bytes % OFE_P4_DRAW_ALIGNMENT", source)
        self.assertNotIn("heap_caps_malloc(lvgl_buf_bytes", source)
        self.assertIn("P4 RENDERER: %s | fills=", source)
        self.assertIn("serial_cli_print_perf(); p4_print_renderer_stats();", source)
        guard = (P4 / "src/OfeP4PpaConfig.h").read_text()
        self.assertIn("CONFIG_CACHE_L2_CACHE_LINE_SIZE != 128", guard)
        self.assertIn("#define OFE_P4_PPA 0", guard)
        self.assertIn("#define OFE_P4_PPA_IMG 0", guard)
        self.assertIn("#if OFE_P4_PPA_IMG && !OFE_P4_PPA", guard)
        self.assertIn("P4 PPA IMAGE: blends=", source)
        self.assertIn('"PPA-fill+image (blocking)"', source)
        self.assertIn("#define esp_cache_get_line_size_by_addr ofe_p4_ppa_cache_line_size", guard)
        implementation = (P4 / "src/OfeP4Ppa.cpp").read_text()
        self.assertIn("esp_cache_get_alignment(caps, &alignment)", implementation)
        self.assertIn("esp_ptr_external_ram(address)", implementation)
        self.assertIn("esp_ptr_internal(address)", implementation)
        self.assertIn("const esp_err_t result = __real_ppa_do_fill(client, config);", implementation)
        self.assertIn("const esp_err_t result = __real_ppa_do_blend(client, config);", implementation)

    def test_performance_reports_use_mirrored_console(self):
        source = (P4 / (P4.name + ".ino")).read_text()
        self.assertIn('debug_printf(\n    "LVGL PERF:', source)
        self.assertIn('debug_printf("DISPLAY BENCHMARK:', source)
        self.assertNotIn('Serial.printf("DISPLAY BENCHMARK:', source)

    def test_generated_port_matches_source_and_templates(self):
        for name, data in PREPARE["generated_files"]().items():
            with self.subTest(file=name):
                self.assertEqual((P4 / name).read_bytes(), data)

    def test_visual_assets_and_protocol_unchanged(self):
        for name in ("OpenFumeBootLogo.h", "SolderIronIcon.h", "SproutIcon.h",
                     "openfume_montserrat_14_de.c", "openfume_montserrat_18_de.c",
                     "openfume_montserrat_22_de.c", "openfume_montserrat_28_de.c",
                     "src/Rs485PeripheralBus.h", "src/Rs485PeripheralBus.cpp"):
            self.assertEqual((P4 / name).read_bytes(), (S3 / name).read_bytes(), name)
        source = (P4 / (P4.name + ".ino")).read_text()
        self.assertIn("#define DISPLAY_STATUS_UI_MIN_INTERVAL_MS 100", source)
        self.assertNotIn("esp32s3/rom/cache.h", source)
        self.assertNotIn("class OfeDpiRgbDisplay", source)
        self.assertNotIn("Arduino_ESP32RGBPanel", source)

    def test_updates_use_unique_p4_target_and_guard(self):
        source = (P4 / (P4.name + ".ino")).read_text()
        self.assertIn("target=DISPLAY_P4_800X480;", source)
        self.assertNotIn("target=DISPLAY_800X480;", source)
        self.assertIn("CAP_DISPLAY_P4 | CAP_DISPLAY_800X480 | CAP_FW_UPDATE", source)
        self.assertNotIn("req.cmd >= CMD_FW_BEGIN && req.cmd <= CMD_FW_REBOOT", source)
        self.assertIn("}, handle_wifi_fw_bulk);", source)
        self.assertIn("p4_firmware_guard.complete() && fw_update_buffer_flush() && Update.end(true)", source)
        self.assertIn("if (!p4_firmware_guard.append(data, len)) return false", source)
        self.assertIn("p4_firmware_guard.reset();", source)

    def test_hosted_bluetooth_is_not_a_placeholder(self):
        ui = (P4 / "src/OfeDisplayWifiUi.inc.h").read_text(encoding="utf-8-sig")
        self.assertEqual(ui, (S3 / "src/OfeDisplayWifiUi.inc.h").read_text(encoding="utf-8-sig"))
        ble = (P4 / "src/OfeP4DisplayBle.h").read_text()
        self.assertNotIn("#include <NimBLEDevice.h>", ble)
        for token in ("hostedInitBLE()", "hostedDeinitBLE()", "nimble_port_init()",
                      "BLE_SM_IOACT_NUMCMP", "peer.sec_state.key_size==16", "peer.sec_state.authenticated",
                      "BLE_GATT_CHR_F_WRITE_AUTHEN", "stored_(config_)", "120000UL"):
            self.assertIn(token, ble)
        self.assertNotIn("nvs_flash_erase", ble)
        self.assertNotIn("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL", ble)

    def test_ecc_backport_is_pinned_and_p4_only(self):
        folder = P4 / "src/tinycrypt"
        manifest = json.loads((folder / "UPSTREAM.json").read_text())
        self.assertEqual(manifest["commit"], "9fd7cb7e606a06111e1b14be7f4e00d77d9cf3dd")
        for name, info in manifest["files"].items():
            self.assertEqual(hashlib.sha256((folder / name).read_bytes()).hexdigest(), info["sha256"])
        core = (folder / "ecc.inc.h").read_text()
        self.assertIn("num_bits == curve->num_n_bits", core)
        dh = (folder / "ecc_dh.inc.h").read_text()
        self.assertIn("curve->num_n_bits + 1", dh)
        self.assertNotIn("SOC_ESP_NIMBLE_CONTROLLER", dh)
        ble = (P4 / "src/OfeP4DisplayBle.h").read_text()
        self.assertLess(ble.index("ofe_p4_ble_ecc_self_test()"), ble.index("ble_hs_cfg.sm_io_cap"))
        self.assertIn("if (ecc_test) { fail(ecc_test);", ble)
        policy = (P4 / "src/OfeP4BleEccSoftware.h").read_text()
        self.assertIn("#undef SOC_ECC_SUPPORTED\n#define SOC_ECC_SUPPORTED 0", policy)
        for name in ("Core", "Dh", "Dsa", "Port"):
            wrapper = (P4 / f"src/OfeP4BleEcc{name}.c").read_text()
            self.assertTrue(wrapper.startswith('#include "OfeP4BleEccSoftware.h"'))
        kat = (P4 / "src/OfeP4BleEccSelfTest.c").read_text()
        self.assertIn("esp_fill_random(dest, size)", kat)
        self.assertLess(kat.index("uECC_set_rng(ofe_p4_tinycrypt_rng)"), kat.index("uint8_t point[64], secret[32]"))
        self.assertIn("test_public[1-i]", kat)
        self.assertIn("return base - 5", kat)
        for name in ("DisplayModule_320x480", "DisplayModule_800x480", "DisplayModule_ST7796_320x480"):
            self.assertFalse(list((ROOT / "Module" / name / "src").glob("OfeP4BleEcc*")))

    def test_master_prioritizes_p4_before_resolution(self):
        web = (ROOT / "OpenFumeExtractorMaster/src/WebUpdate.inc.h").read_text(encoding="utf-8-sig")
        for function in ("module_firmware_signature_hint_for", "module_firmware_auth_target_for",
                         "module_firmware_signature_allowed"):
            definition = re.search(r"static [^\n]+ " + function + r"\([^;\n]+\) \{", web)
            self.assertIsNotNone(definition, function)
            body = web[definition.end():web.index("\n}", definition.end())]
            self.assertLess(body.index("CAP_DISPLAY_P4"), body.index("CAP_DISPLAY_800X480"))
        self.assertIn("p4 ? 0x0012 : 0x0009", web)
        self.assertIn("CAP_DISPLAY_P4 = 1UL << 28", (P4 / "src/Rs485PeripheralBus.h").read_text())

    def test_owner_timings_and_shared_i2c_are_explicit(self):
        board = (P4 / "OfeP4Board.h").read_text()
        self.assertIn("#define DISPLAY_RGB_PCLK_HZ 24000000", board)
        self.assertIn("#define DISPLAY_RGB_BOUNCE_BUFFER_LINES 20", board)
        panel = (P4 / "src/OfeP4RgbDisplay.h").read_text()
        self.assertIn(".vsync_back_porch = 16", panel)
        self.assertIn(".vsync_front_porch = 16", panel)
        self.assertIn(".hsync_idle_low = 0", panel)
        self.assertIn(".vsync_idle_low = 0", panel)
        self.assertIn(".pclk_idle_high = 1", panel)
        self.assertIn("const auto& data_pins = ofe_p4::rgb_pins", panel)
        source = (P4 / (P4.name + ".ino")).read_text()
        self.assertIn("ofe_p4_log_sdk_cache_profile();", source)
        self.assertIn("(DISPLAY_RGB_HEIGHT + 16 + 4 + 16)", source)
        self.assertNotIn("(DISPLAY_RGB_HEIGHT + 8 + 4 + 8)", source)
        self.assertIn("if (ofe_p4::configureHostedRadio()) display_wifi.prepareRadio()", source)
        self.assertIn("if (!ofe_p4::hostedPinsReady()) return false;", (P4 / "src/OfeDisplayWifi.h").read_text())
        self.assertIn('return OFE_STR(CONFIG_SPIRAM_SPEED) " MHz";', source)
        # No LEDC call may accidentally drive RGB DE (GPIO2) as an S3 backlight.
        self.assertNotIn("ledcAttach(", source)
        self.assertNotIn("ledcWrite(", source)


if __name__ == "__main__":
    unittest.main()
