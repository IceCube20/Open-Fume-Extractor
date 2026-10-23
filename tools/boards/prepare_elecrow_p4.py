"""Generate a separate P4 port from the stable 800x480 OFE UI.

No S3 source, installed library, SDK, signing key or release file is modified.
Generated files are hash-checked; local edits require explicit --force to replace.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Module/DisplayModule_800x480"
DEST = ROOT / "Module/DisplayModule_P4_800x480"
TEMPLATES = Path(__file__).resolve().parent / "elecrow_p4_5inch"
P4_FW_PATCH = 7


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Source boundary changed: " + old[:100])
    return text.replace(old, new, 1)


def replace_function(text, name, body):
    # These selected functions are followed by another static function.
    start = text.index("static ", text.index(name) - 40)
    # Locate the definition, not a forward declaration or call site.
    signature = text[start:text.index("{", start)]
    if name not in signature or ";" in signature:
        raise ValueError("Unknown function boundary: " + name)
    end = text.index("\n}\n", start) + 3
    return text[:start] + signature + "{\n" + body + "\n}\n" + text[end:]


def generated_files():
    original = (SOURCE / (SOURCE.name + ".ino")).read_text(encoding="utf-8-sig")
    source = original
    # Keep performance reports visible on UART0 as well as native USB CDC.
    source = once(source, '  Serial.printf(\n    "LVGL PERF:',
                  '  debug_printf(\n    "LVGL PERF:')
    source = once(source, '  Serial.printf("DISPLAY BENCHMARK:',
                  '  debug_printf("DISPLAY BENCHMARK:')
    source = once(source, "#include <Arduino.h>\n", '#include <Arduino.h>\n#include "OfeP4Board.h"\n#include "src/OfeP4SdkProfile.h"\n#include "src/OfeP4Ppa.h"\n')
    guard_start = source.index("#if defined(OFE_REQUIRE_RGB_HIGH_PERF_SDK)")
    guard_end = source.index("#endif", guard_start) + len("#endif")
    source = source[:guard_start] + source[guard_end:]
    source = once(source, "#include <esp32s3/rom/cache.h>\n", "")
    source = once(source, '#include "src/OfeDisplayBle.h"', '#include "src/OfeP4DisplayBle.h"')
    source = once(source, '#define OFE_DISPLAY_HOSTNAME_PREFIX "OFE-Display-800x480"',
                  '#define OFE_DISPLAY_HOSTNAME_PREFIX "OFE-Display-P4-800x480"')
    # Retain the owner's current P4 clock; do not inherit the S3 fallback timing.
    source = once(source, "#define DISPLAY_RGB_PCLK_HZ 16000000",
                  "#define DISPLAY_RGB_PCLK_HZ 24000000")
    source = once(source,
        "// Exact pixel clock used by known JC8048W550 Arduino_GFX configurations.\n"
        "// Keep porches/polarities unchanged; only use the board's documented 16 MHz PCLK.",
        "// Retain the owner's current P4 pixel clock; keep porches/polarities unchanged.")
    source = once(source, "static const uint16_t HW_VERSION = 0x0100;", "static const uint16_t HW_VERSION = 0x0200;")
    for component in ("MINOR", "PATCH"):
        pattern = r"^#define OFE_MODULE_FW_" + component + r" \d+$"
        source, count = re.subn(pattern, "#define OFE_MODULE_FW_" + component + (f" {P4_FW_PATCH}" if component == "PATCH" else " 0"), source, flags=re.M)
        if count != 1:
            raise ValueError("Firmware version boundary changed: " + component)
    start = source.index("#if OFE_RGB_RENDER_DIAGNOSTICS\n#define OFE_MODULE_FW_SUFFIX")
    end = source.index("#define OFE_MODULE_FW_VERSION", start)
    source = source[:start] + '#define OFE_MODULE_FW_SUFFIX "alpha-P4Prep"\n' + source[end:]
    source = once(source, "target=DISPLAY_800X480;version=", "target=DISPLAY_P4_800X480;version=")
    source = source.replace("CAP_DISPLAY_800X480 | CAP_FW_UPDATE |", "CAP_DISPLAY_P4 | CAP_DISPLAY_800X480 | CAP_FW_UPDATE |")
    source = once(source, "static size_t fw_update_write_len = 0;",
                  'static size_t fw_update_write_len = 0;\n#include "src/OfeP4FirmwareGuard.h"\nstatic OfeP4FirmwareGuard p4_firmware_guard;')
    source = once(source, "static void fw_update_buffer_reset() {\n",
                  "static void fw_update_buffer_reset() {\n  p4_firmware_guard.reset();\n")
    source = once(source, "static bool fw_update_buffer_append(const uint8_t* data, size_t len) {\n",
                  "static bool fw_update_buffer_append(const uint8_t* data, size_t len) {\n  if (!p4_firmware_guard.append(data, len)) return false;\n")
    source = once(source, "const bool ok = fw_update_buffer_flush() && Update.end(true);",
                  "const bool ok = p4_firmware_guard.complete() && fw_update_buffer_flush() && Update.end(true);")
    source = once(source, "  if (!fw_update_buffer_append(data, n)) return STATUS_BUSY;",
                  "  if (!fw_update_buffer_append(data, n)) { fw_update_abort_local(); return STATUS_BAD_VALUE; }")

    start = original.index("class OfeEspLcdRgbDisplay : public Arduino_GFX {")
    end = original.index("\n#if 0", start)
    panel = original[start:end]
    if panel.count("OfeEspLcdRgbDisplay") != 2:
        raise ValueError("RGB class/constructor boundary changed")
    panel = panel.replace("OfeEspLcdRgbDisplay", "OfeP4RgbDisplay")
    for old, new in (
        ("LCD_CLK_SRC_PLL160M", "LCD_CLK_SRC_DEFAULT"),
        (".vsync_back_porch = 8", ".vsync_back_porch = 16"),
        (".vsync_front_porch = 8", ".vsync_front_porch = 16"),
        (".hsync_idle_low = 1", ".hsync_idle_low = 0"),
        (".vsync_idle_low = 1", ".vsync_idle_low = 0"),
        (".pclk_idle_high = 0", ".pclk_idle_high = 1"),
        (".hsync_gpio_num = 39", ".hsync_gpio_num = ofe_p4::hsync"),
        (".vsync_gpio_num = 41", ".vsync_gpio_num = ofe_p4::vsync"),
        (".de_gpio_num = 40", ".de_gpio_num = ofe_p4::de"),
        (".pclk_gpio_num = 42", ".pclk_gpio_num = ofe_p4::pclk"),
        ("const int data_pins[16] = {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14};",
         "const auto& data_pins = ofe_p4::rgb_pins;"),
    ):
        panel = once(panel, old, new)
    # Remove S3-only explanatory comments; leave the actual proven drawing code intact.
    panel = panel.replace("ESP32-S3", "ESP32-P4").replace("JC8048W550", "ELECROW P4 5-inch")
    polarity_start = panel.index("          // IMPORTANT: preserve the polarity")
    polarity_end = panel.index("          .hsync_idle_low", polarity_start)
    panel = panel[:polarity_start] + "          // Match ELECROW's bundled BusRGB defaults.\n" + panel[polarity_end:]
    start = source.index("class OfeDpiRgbDisplay : public Arduino_GFX {")
    end = source.index("// NVS/flash operations", start)
    source = source[:start] + ('#include "src/OfeP4RgbDisplay.h"\n'
        'static OfeP4RgbDisplay* gfx = new OfeP4RgbDisplay(DISPLAY_RGB_WIDTH, DISPLAY_RGB_HEIGHT, DISPLAY_ROTATION);\n\n') + source[end:]
    source = replace_function(source, "init_backlight_pwm()", "  backlight_pwm_ready = ofe_p4::beginI2c();")
    source = replace_function(source, "write_backlight_duty(uint32_t duty)",
        "  if (!ofe_p4::setBacklight(duty > 100 ? 100 : (uint8_t)duty))\n"
        '    Serial.println("P4 STC8 backlight write failed");')
    source = replace_function(source, "backlight_duty_from_percent(uint8_t pct)",
        "  return pct < 10 ? 10 : (pct > 100 ? 100 : pct);")
    source = replace_function(source, "touch_reset_f1atb_sequence()",
        "  ofe_p4::resetTouch();\n"
        "  if (gt911_probe_addr(GT911_ADDR_PRIMARY)) return;\n"
        "  if (gt911_probe_addr(GT911_ADDR_SECONDARY)) return;\n"
        "  touch_addr = GT911_ADDR_PRIMARY;\n"
        '  Serial.println("P4 GT911 not found on 0x5D/0x14");')
    source = replace_function(source, "psram_mode_text()", '  return "P4 HEX/16-bit";')
    source = replace_function(source, "psram_speed_text()",
        '#if defined(CONFIG_SPIRAM_SPEED)\n  return OFE_STR(CONFIG_SPIRAM_SPEED) " MHz";\n'
        '#else\n  return "SDK default";\n#endif')
    source = source.replace("enable OPI PSRAM in the board settings if this is an N8R16 module",
                            "enable PSRAM in the P4 board settings")
    source = once(source, "  display_wifi.prepareRadio();",
        '  if (ofe_p4::configureHostedRadio()) display_wifi.prepareRadio();\n'
        '  else Serial.println("P4 hosted SDIO pin configuration failed; use RS485");')
    source = source.replace("(DISPLAY_RGB_HEIGHT + 8 + 4 + 8)", "(DISPLAY_RGB_HEIGHT + 16 + 4 + 16)")
    source = once(source, "SYNCFIX-v20 JC8048W550 BOUNCE8 PROFILER-LIGHT LIVE-HOME-MODULES", "P4-PREP-v1 ELECROW 5inch RGB ST7262")
    source = once(source, "H/V idle-low=1, lv_color_t", "H/V idle-low=0, lv_color_t")
    start = source.index("#if defined(CONFIG_ESP32S3_DATA_CACHE_LINE_64B)\n", source.index("void setup()"))
    end = source.index('  Serial.printf("LVGL renderer:', start)
    source = source[:start] + ('  ofe_p4_log_sdk_cache_profile();\n'
        '  Serial.println("P4: board-owner RNG/RGB test profile; no S3 SDK required");\n'
        '  Serial.println("P4: C6 hosted BLE pairing + RS485/WiFi OTA; requires P4-aware Master");\n'
        '  Serial.println("P4 BLE ECC: software-only + P4 hardware RNG + self-test v3");\n') + source[end:]
    source = source.replace("display800>", "displayP4>")
    source = once(source, "// --- LVGL performance profiler", '''static void p4_print_renderer_stats() {
  const auto stats = ofe_p4_ppa::snapshot();
  debug_printf("P4 RENDERER: %s | fills=%lu errors=%lu max=%lu us | draw alignment=%u B\\n",
    OFE_P4_PPA_IMG ? "PPA-fill+image (blocking)" : (OFE_P4_PPA ? "PPA-fill (blocking)" : "software"),
    (unsigned long)stats.fills, (unsigned long)stats.errors, (unsigned long)stats.max_us,
    (unsigned)OFE_P4_DRAW_ALIGNMENT);
  if (OFE_P4_PPA_IMG) {
    debug_printf("P4 PPA IMAGE: blends=%lu errors=%lu max=%lu us; unsupported tasks use software\\n",
      (unsigned long)stats.blends, (unsigned long)stats.blend_errors, (unsigned long)stats.blend_max_us);
  }
}

// --- LVGL performance profiler''')
    source = once(source, '  else if (!strcmp(command, "perf")) serial_cli_print_perf();',
        '  else if (!strcmp(command, "perf")) { serial_cli_print_perf(); p4_print_renderer_stats(); }')
    source = once(source, "  ofe_p4_log_sdk_cache_profile();", "  ofe_p4_log_sdk_cache_profile();\n  p4_print_renderer_stats();")
    source = once(source, "  render_diagnostics_report(now);\n  display_benchmark_reset_counters(now);",
        "  p4_print_renderer_stats();\n  render_diagnostics_report(now);\n  display_benchmark_reset_counters(now);")
    source = once(source, "    64, render_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);",
        "    OFE_P4_DRAW_ALIGNMENT, render_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);")
    source = source.replace("heap_caps_malloc(lvgl_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)",
        "heap_caps_aligned_alloc(OFE_P4_DRAW_ALIGNMENT, lvgl_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)")
    source = once(source, 'bytes in %s, 64B aligned\\n', 'bytes in %s, 128B aligned\\n')
    source = once(source, "  prepare_large_lvgl_tiles(screen_w);", '''  prepare_large_lvgl_tiles(screen_w);
  if ((reinterpret_cast<uintptr_t>(lvgl_draw_buf) % OFE_P4_DRAW_ALIGNMENT) ||
      (lvgl_buf_bytes % OFE_P4_DRAW_ALIGNMENT)) {
    debug_println("P4 draw buffer alignment invalid; UI not started");
    return;
  }''')
    source = once(source, "display_ble.configure(module_uid(),1,nullptr,", "display_ble.configure(module_uid(),3,nullptr,")

    outputs = {}
    paths = [p for p in SOURCE.iterdir() if p.is_file()] + list((SOURCE / "src").rglob("*"))
    for path in paths:
        relative = path.relative_to(SOURCE).as_posix()
        if not path.is_file() or path.suffix.lower() not in (".c", ".cpp", ".h"):
            continue
        if relative in ("build_opt.h", "OfeBleBuildConfig.h", "src/OfeDisplayBle.h"):
            continue
        outputs[relative] = path.read_bytes()
    outputs["DisplayModule_P4_800x480.ino"] = (
        "// Generated by tools/boards/prepare_elecrow_p4.py; see docs/Display-P4-Preparation.md.\n" + source).encode()
    outputs["src/OfeP4RgbDisplay.h"] = ("// Generated from the stable OFE drawing implementation and verified ELECROW timings.\n#pragma once\n" + panel).encode()
    outputs["OfeP4Board.h"] = (TEMPLATES / "OfeP4Board.h").read_bytes()
    outputs["src/OfeP4DisplayBle.h"] = (TEMPLATES / "OfeP4DisplayBle.h").read_bytes()
    outputs["src/OfeP4FirmwareGuard.h"] = (TEMPLATES / "OfeP4FirmwareGuard.h").read_bytes()
    outputs["src/OfeP4SdkProfile.h"] = (TEMPLATES / "OfeP4SdkProfile.h").read_bytes()
    for name in ("OfeP4PpaConfig.h", "OfeP4Ppa.h", "OfeP4Ppa.cpp"):
        outputs["src/" + name] = (TEMPLATES / name).read_bytes()
    memory = outputs["src/OfeDisplayMemory.h"].decode("utf-8-sig")
    memory = once(memory, "#include <esp_heap_caps.h>", '#include <esp_heap_caps.h>\n#include "OfeP4PpaConfig.h"')
    memory = once(memory, "heap_caps_aligned_alloc(64, bytes, caps)",
                  "heap_caps_aligned_alloc(OFE_P4_DRAW_ALIGNMENT, bytes, caps)")
    outputs["src/OfeDisplayMemory.h"] = memory.encode()
    config = outputs["ofe_lv_conf.h"].decode("utf-8-sig")
    config = once(config, "#define LV_CONF_H", '#define LV_CONF_H\n#include "src/OfeP4PpaConfig.h"')
    config = once(config, "#define LV_DRAW_BUF_ALIGN                       4", '''#if OFE_P4_PPA
#define LV_DRAW_BUF_ALIGN OFE_P4_DRAW_ALIGNMENT
#ifndef CONFIG_LV_DRAW_BUF_ALIGN
#define CONFIG_LV_DRAW_BUF_ALIGN LV_DRAW_BUF_ALIGN
#endif
#else
#define LV_DRAW_BUF_ALIGN 4
#endif''')
    config = once(config, "#define LV_USE_PPA  0", "#define LV_USE_PPA OFE_P4_PPA")
    config = once(config, "#define LV_USE_PPA_IMG      0", "#define LV_USE_PPA_IMG OFE_P4_PPA_IMG")
    outputs["ofe_lv_conf.h"] = config.encode()
    for path in sorted(TEMPLATES.glob("OfeP4BleEcc*")):
        outputs["src/" + path.name] = path.read_bytes()
    upstream = json.loads((TEMPLATES / "tinycrypt/UPSTREAM.json").read_text())
    for name, info in upstream["files"].items():
        if hashlib.sha256((TEMPLATES / "tinycrypt" / name).read_bytes()).hexdigest() != info["sha256"]:
            raise ValueError("P4 ECC backport differs from pinned upstream source: " + name)
    for path in sorted((TEMPLATES / "tinycrypt").iterdir()):
        if path.is_file():
            outputs["src/tinycrypt/" + path.name] = path.read_bytes()
    outputs["build_opt.h"] = b'-O3\n-DLV_CONF_PATH=\\"ofe_lv_conf.h\\"\n'
    wifi = outputs["src/OfeDisplayWifi.h"].decode("utf-8-sig")
    wifi = once(wifi, '#include "OfeDisplayTunnel.h"', '#include "OfeDisplayTunnel.h"\n#include "../OfeP4Board.h"')
    wifi = once(wifi, '  bool startRadio(const char* phase) {',
                '  bool startRadio(const char* phase) {\n'
                '    if (!ofe_p4::hostedPinsReady()) return false; // Never fall back to EV-board SDIO pins.')
    outputs["src/OfeDisplayWifi.h"] = wifi.encode()
    manifest = {
        "firmware_version": f"1.0.{P4_FW_PATCH}alpha-P4Prep",
        "status": "software port; hosted BLE and RS485/WiFi OTA enabled; hardware validation pending",
        "base_sketch_sha256": hashlib.sha256(original.encode()).hexdigest(),
        "vendor_repository": "https://github.com/Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen",
        "vendor_blobs": {
            "board_config.h (Lesson07)": "129f7aa1a1d28d2bf4535115220e411b30bf81ff",
            "STC8 header": "e67dd3f0c0fadf88333698ec2e0ad19b4a1f272e",
            "SDIO config (Lesson16)": "91117626fcae0fe0d5c7e40bbec1f86e06a46236",
            "UART config (Lesson04)": "26b3b43bea9729160896989fecb338d456f9d613",
            "BusRGB defaults": "89ea7e38b4121abf784869e62bc5cbf6b3960090",
        },
        "generated_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in outputs.items()},
    }
    outputs["p4-preparation-manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--force", action="store_true", help="Explicitly replace locally edited generated files")
    args = parser.parse_args()
    outputs = generated_files()
    manifest = DEST / "p4-preparation-manifest.json"
    previous = json.loads(manifest.read_text())["generated_sha256"] if manifest.exists() else {}
    # Preflight every file before writing, so local edits cannot produce a mixed snapshot.
    for name, data in outputs.items():
        path = DEST / name
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise SystemExit("P4 preparation is stale: " + name)
        elif path.exists() and path.read_bytes() != data and name != manifest.name and not args.force:
            if hashlib.sha256(path.read_bytes()).hexdigest() != previous.get(name):
                raise SystemExit("Locally edited P4 file preserved: " + name + "; review before --force")
    if not args.check:
        for name, data in outputs.items():
            path = DEST / name
            if not path.exists() or path.read_bytes() != data:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
    print("PASS: P4 preparation verified" if args.check else "PASS: separate P4 preparation created; S3 sources untouched")


if __name__ == "__main__":
    main()
