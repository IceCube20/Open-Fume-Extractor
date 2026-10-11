"""Metadata, syntax and authenticated-route regression checks."""
import ast
import json
from pathlib import Path
import re
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "custom_components/open_fume_extractor"
MASTER = ROOT.parent / "OpenFumeExtractorMaster/src"


class PackagingTests(unittest.TestCase):
    def test_all_python_sources_parse(self):
        for path in COMPONENT.rglob("*.py"):
            ast.parse(path.read_text(encoding="utf-8"), filename=str(path))

    def test_manifest_and_platform_files(self):
        manifest = json.loads((COMPONENT / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["domain"], "open_fume_extractor")
        self.assertEqual(manifest["iot_class"], "local_polling")
        self.assertTrue(manifest["config_flow"])
        for platform in ("sensor", "binary_sensor", "switch", "number", "select", "button", "text"):
            self.assertTrue((COMPONENT / (platform + ".py")).is_file())

    def test_jbc_projection_is_shared_without_enlarging_regular_web_state(self):
        source = (MASTER / "MasterMqtt.inc.h").read_text(encoding="utf-8-sig")
        web = (MASTER / "WebStatus.inc.h").read_text(encoding="utf-8-sig")
        self.assertEqual(source.count("static void mqtt_append_jbc_usb_state("), 1)
        self.assertIn("mqtt_append_jbc_usb_state(json, m);", source)
        self.assertIn("mqtt_append_jbc_usb_state(core, m, true);", web)
        self.assertIn('web.arg("ha") == "1"', source)
        self.assertIn("if (include_ha)", web)
        prototypes = (MASTER / "MasterPrototypes.inc.h").read_text(encoding="utf-8-sig")
        self.assertIn("mqtt_append_jbc_usb_state(String& json, const ModuleRecord& m, bool include_controls = false);", prototypes)
        self.assertIn("mqtt_append_jbc_usb_state(String& json, const ModuleRecord& m, bool include_controls)", source)

    def test_mqtt_writable_duration_number_keeps_raw_state_and_unit(self):
        source = (MASTER / "MasterMqtt.inc.h").read_text(encoding="utf-8-sig")
        helper = source.split("static String mqtt_universal_entity_value_with_def(", 1)[1].split("static String mqtt_universal_entity_value_for_def", 1)[0]
        self.assertIn('ofe_entity::equal(def.type, "number")', helper)
        self.assertIn('ofe_entity::equal(ofe_entity::access(def), "rw")', helper)
        self.assertLess(helper.index("return raw;", helper.index('ofe_entity::equal(def.type')), helper.index("entity_display_string"))
        self.assertIn('const bool duration_text = strcmp(component, "number")', source)

    def test_mqtt_state_borrows_descriptor_without_metadata_string_allocations(self):
        source = (MASTER / "MasterMqtt.inc.h").read_text(encoding="utf-8-sig")
        state = source.split("static void mqtt_publish_module_state(", 1)[1].split("static bool mqtt_apply_module_command", 1)[0]
        self.assertIn("ofe_entity::parse({p, len}, def)", state)
        self.assertIn("mqtt_universal_entity_value_with_def(*e, def)", state)
        self.assertNotIn("MqttUniversalEntityDef", state)
        self.assertNotIn("char line[1024]", state)

    def test_current_installation_zip_matches_sources_without_private_files(self):
        version = json.loads((COMPONENT / "manifest.json").read_text(encoding="utf-8"))["version"]
        path = ROOT / "dist" / f"open-fume-extractor-ha-{version}.zip"
        with zipfile.ZipFile(path) as archive:
            files = {name.replace("\\", "/"): archive.read(name) for name in archive.namelist() if not name.endswith("/")}
        self.assertEqual(files["README.md"], (ROOT / "README.md").read_bytes())
        for path in COMPONENT.rglob("*"):
            if path.is_file() and path.suffix in (".py", ".json", ".png", ".js") and "__pycache__" not in path.parts:
                name = "custom_components/open_fume_extractor/" + path.relative_to(COMPONENT).as_posix()
                self.assertEqual(files[name], path.read_bytes(), name)
        for name in files:
            self.assertNotIn("__pycache__", name)
            self.assertNotIn("tests/", name)
            self.assertNotIn(".pem", name)

    def test_translations_have_all_keys(self):
        def keys(value, prefix=""):
            if not isinstance(value, dict):
                return {prefix}
            return set().union(*(keys(child, prefix + "." + key) for key, child in value.items()))
        expected = keys(json.loads((COMPONENT / "strings.json").read_text(encoding="utf-8")))
        for language in ("en", "de"):
            actual = json.loads((COMPONENT / "translations" / (language + ".json")).read_text(encoding="utf-8"))
            self.assertEqual(keys(actual), expected)

    def test_api_routes_require_existing_auth(self):
        source = (MASTER / "WebServer.inc.h").read_text(encoding="utf-8-sig")
        for route, method, handler in (
            ("/api/ha/session", "HTTP_GET", "web_handle_ha_session"),
            ("/api/ha/control", "HTTP_POST", "web_handle_ha_control"),
        ):
            expression = re.escape(f'web.on("{route}", {method}') + r".*web_require_auth\(\).*" + handler
            self.assertRegex(source, expression)

    def test_ha_api_does_not_add_bus_polling(self):
        source = (MASTER / "WebHomeAssistant.inc.h").read_text(encoding="utf-8-sig")
        self.assertNotIn("request(", source)
        self.assertNotIn("fastPoll", source)
        self.assertIn("module->uid", source)
        self.assertIn("moduleFirmwareUpdateActive", source)

    def test_logo_is_original_master_asset_and_packaged(self):
        source = (MASTER / "WebLogo.h").read_text(encoding="utf-8-sig")
        array = source.split("OFE_LOGO_PNG[] PROGMEM = {", 1)[1].split("}", 1)[0]
        original = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", array))
        for name in ("icon.png", "logo.png"):
            self.assertEqual((COMPONENT / "brand" / name).read_bytes(), original)
        self.assertIn("'.png'", (ROOT / "tools/build_package.ps1").read_text())

    def test_mqtt_heap_discovery_uses_binary_kibibytes(self):
        source = (MASTER / "MasterMqtt.inc.h").read_text(encoding="utf-8-sig")
        lines = [line for line in source.splitlines() if 'mqtt_publish_' in line and ('"data_size"' in line or '\\"data_size\\"' in line)]
        self.assertEqual(len(lines), 4)
        for line in lines:
            self.assertIn("KiB", line)
            self.assertNotIn('"KB"', line)
        self.assertIn("/ 1024UL", source)


if __name__ == "__main__":
    unittest.main()
