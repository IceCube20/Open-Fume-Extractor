"""P4 SDK preparation safety/validation; no SDK build or installation."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "p4_sdk_prepare", ROOT / "tools/boards/p4_high_perf_sdk/prepare.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)
BOOT_SPEC = importlib.util.spec_from_file_location(
    "p4_sdk_bootstrap", ROOT / "tools/boards/p4_high_perf_sdk/bootstrap.py")
BOOT = importlib.util.module_from_spec(BOOT_SPEC)
BOOT_SPEC.loader.exec_module(BOOT)
WIN_SPEC = importlib.util.spec_from_file_location(
    "p4_windows_build", ROOT / "tools/boards/p4_high_perf_sdk/windows_build.py")
WIN = importlib.util.module_from_spec(WIN_SPEC)
WIN_SPEC.loader.exec_module(WIN)


class P4HighPerfSDK(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.baseline = self.root / "baseline"
        self.candidate = self.root / "candidate"
        self.make_sdk(self.baseline, False)
        self.make_sdk(self.candidate, True)

    def make_sdk(self, path, high_perf):
        path.mkdir()
        shutil.copyfile(SDK.HERE / "versions.txt", path / "versions.txt")
        values = {"CONFIG_IDF_TARGET": '"esp32p4"',
                  "CONFIG_ESP32P4_SELECTS_REV_LESS_V3": "y",
                  "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ": "360", "CONFIG_SPIRAM_SPEED": "200",
                  "CONFIG_ESP_HOSTED_ENABLED": "y",
                  "CONFIG_CACHE_L2_CACHE_SIZE": "0x40000" if high_perf else "0x20000",
                  "CONFIG_CACHE_L2_CACHE_LINE_SIZE": "128" if high_perf else "64"}
        (path / "sdkconfig").write_text("\n".join(f"{k}={v}" for k, v in values.items()))
        for name in ("lib/test.a", "include/test.h", "ld/test.ld", "flags/c_flags", "esp_sr/models.bin",
                     "bin/bootloader.bin", "qio_qspi/include/sdkconfig.h",
                     "dio_qspi/include/sdkconfig.h"):
            file = path / name
            file.parent.mkdir(parents=True, exist_ok=True)
            content = ("rebuilt" if high_perf else "stock")
            if name.endswith("sdkconfig.h"):
                content = "\n".join(f"#define {k} {v}" for k, v in values.items())
            file.write_text(content)

    def test_cache_only_candidate(self):
        SDK.validate_sdk(self.baseline, None, False)
        SDK.validate_sdk(self.candidate, self.baseline, True)

    def test_identical_deprecated_aliases_allowed_but_conflicts_rejected(self):
        self.assertEqual(SDK.config("CONFIG_TEST=10\nCONFIG_TEST=10\n"), {"CONFIG_TEST": "10"})
        for text in ("CONFIG_TEST=10\nCONFIG_TEST=11\n", "CONFIG_TEST=y\n# CONFIG_TEST is not set\n"):
            with self.assertRaisesRegex(ValueError, "Conflicting setting"):
                SDK.config(text)

    def test_wrong_versions_rejected(self):
        (self.candidate / "versions.txt").write_text("esp-idf: v5.4.1\n")
        with self.assertRaisesRegex(ValueError, "versions differ"):
            SDK.validate_sdk(self.candidate, self.baseline, True)

    def test_git_abbreviation_length_not_treated_as_component_upgrade(self):
        path = self.candidate / "versions.txt"
        path.write_text(path.read_text().replace("b774170ff46", "b774170"))
        SDK.validate_sdk(self.candidate, self.baseline, True)
        path.write_text(path.read_text().replace("b774170", "b774171"))
        with self.assertRaisesRegex(ValueError, "versions differ"):
            SDK.validate_sdk(self.candidate, self.baseline, True)

    def test_empty_baseline_flag_file_is_allowed(self):
        for sdk in (self.baseline, self.candidate):
            (sdk / "flags/empty_flags").write_text("")
        SDK.validate_sdk(self.candidate, self.baseline, True)

    def test_other_config_changes_rejected(self):
        path = self.candidate / "sdkconfig"
        path.write_text(path.read_text().replace("CONFIG_ESP_HOSTED_ENABLED=y", "# CONFIG_ESP_HOSTED_ENABLED is not set"))
        with self.assertRaisesRegex(ValueError, "Non-cache"):
            SDK.validate_sdk(self.candidate, self.baseline, True)

    def test_missing_archive_rejected(self):
        (self.candidate / "lib/test.a").unlink()
        with self.assertRaisesRegex(ValueError, "Incomplete SDK"):
            SDK.validate_sdk(self.candidate, self.baseline, True)

    def test_unchanged_archives_rejected(self):
        shutil.copyfile(self.baseline / "lib/test.a", self.candidate / "lib/test.a")
        with self.assertRaisesRegex(ValueError, "archives are unchanged"):
            SDK.validate_sdk(self.candidate, self.baseline, True)

    def test_stale_memory_variant_header_rejected(self):
        shutil.copyfile(self.baseline / "dio_qspi/include/sdkconfig.h",
                        self.candidate / "dio_qspi/include/sdkconfig.h")
        with self.assertRaisesRegex(ValueError, "Inconsistent dio_qspi"):
            SDK.validate_sdk(self.candidate, self.baseline, True)

    def test_workspaces_cannot_overlap(self):
        for builder in (self.baseline, self.baseline / "builder", self.root,
                        SDK.PROJECT / "sdk", self.root / "Arduino15/builder"):
            with self.subTest(builder=builder), self.assertRaises(ValueError):
                SDK.separate_workspace(builder, self.baseline)

    def test_prepare_preserves_other_targets_and_is_idempotent(self):
        builder = self.root / "builder"
        (builder / "configs").mkdir(parents=True)
        document = {"targets": [
            {"target": "esp32p4", "chip_variant": "esp32p4_es", "features": ["qio_ram"]},
            {"target": "esp32s3", "features": ["esp_sr"]}]}
        original = (json.dumps(document) + "\n").encode()
        path = builder / "configs/builds.json"
        path.write_bytes(original)
        (builder / "configs/defconfig.esp32p4_es").write_text("CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y\n")
        def git(_path, *args):
            return SDK.SOURCE_PINS["."] if args[0] == "rev-parse" else ""
        with patch.object(SDK, "git", git), patch.object(SDK.subprocess, "check_output", return_value=original):
            SDK.prepare(builder, self.baseline)
            first = path.read_bytes()
            SDK.prepare(builder, self.baseline)
            self.assertEqual(first, path.read_bytes())
            modified = json.loads(first)
            self.assertEqual(modified["targets"][1], document["targets"][1])
            self.assertEqual(modified["targets"][0]["features"][-1], SDK.FEATURE)
            path.write_text("unrelated user edit")
            with self.assertRaisesRegex(ValueError, "unrelated edits"):
                SDK.prepare(builder, self.baseline)

    def test_submodule_mismatch_on_later_line_rejected(self):
        def git(path, *args):
            if args[0] == "rev-parse":
                return SDK.SOURCE_PINS["."]
            return " abc submodule1\n+abc submodule2" if args[0] == "submodule" else ""
        with patch.object(SDK, "git", git), self.assertRaisesRegex(ValueError, "submodules"):
            SDK.check_sources(self.root)

    def lock(self):
        entries = SDK.versions((SDK.HERE / "versions.txt").read_text())
        dependencies = {name.replace("__", "/", 1): {"version": value}
                        for name, value in entries.items() if "__" in name}
        dependencies["idf"] = {"version": "5.5.5"}
        return {"target": "esp32p4", "dependencies": dependencies}

    def test_matching_dependency_lock(self):
        SDK.validate_dependency_lock(self.lock())

    def test_hosted_dependency_drift_rejected_before_build(self):
        document = self.lock()
        document["dependencies"]["espressif/esp_hosted"]["version"] = "2.13.0"
        with self.assertRaisesRegex(ValueError, "esp_hosted"):
            SDK.validate_dependency_lock(document)

    def test_other_target_dependency_lock_rejected(self):
        document = self.lock()
        document["target"] = "esp32s3"
        with self.assertRaisesRegex(ValueError, "target esp32p4"):
            SDK.validate_dependency_lock(document)

    def test_bootstrap_constraints_do_not_add_linker_requirements(self):
        manifest = BOOT.constraint_manifest()
        self.assertEqual(manifest["targets"], ["esp32p4"])
        for value in manifest["dependencies"].values():
            self.assertEqual(value["require"], "no")
            self.assertTrue(value["version"].startswith("=="))
        self.assertEqual(manifest["dependencies"]["espressif/esp_hosted"]["version"], "==2.12.13")

    def test_bootstrap_preserves_nonempty_workspace(self):
        (self.root / "user-file").write_text("preserve")
        with self.assertRaisesRegex(ValueError, "new/empty"):
            BOOT.workspace(self.root)
        self.assertEqual((self.root / "user-file").read_text(), "preserve")

    def test_bootstrap_owned_file_rejects_local_edits(self):
        path = self.root / "owned.txt"
        BOOT.owned_file(path, b"first")
        BOOT.owned_file(path, b"first")
        with self.assertRaisesRegex(ValueError, "Preserving unrelated"):
            BOOT.owned_file(path, b"changed")

    def test_windows_environment_does_not_activate_idf6(self):
        with patch.object(Path, "is_dir", return_value=True), patch.dict(WIN.os.environ, {
                "LOCALAPPDATA": "C:/local", "PATH": "old",
                "IDF_PATH": "C:/esp/v6.0/esp-idf",
                "IDF_COMPONENT_LOCAL_STORAGE_URL": "file://C:/Espressif/tools"}):
            env = WIN.environment(self.root, self.root / "host")
        self.assertEqual(env["IDF_PATH"], (self.root / "builder/esp-idf").as_posix())
        self.assertEqual(env["ESP_IDF_VERSION"], "5.5")
        self.assertEqual(env["IDF_PY_BUILD_JOBS"], "2")
        self.assertEqual(env["IDF_PYTHON_ENV_PATH"], str(self.root / "python"))
        self.assertNotIn("IDF_COMPONENT_LOCAL_STORAGE_URL", env)
        self.assertIn("3.30.2", env["PATH"])
        self.assertNotIn("esp-15.2", env["PATH"])

    def test_windows_defaults_use_only_pre_v3(self):
        builder = self.root / "builder"
        (builder / "configs").mkdir(parents=True)
        (builder / "configs/builds.json").write_text(json.dumps({"targets": [
            {"target": "esp32p4", "chip_variant": "esp32p4_es", "features": [SDK.FEATURE], "idf_libs": ["qio"]},
            {"target": "esp32p4", "features": ["other"], "idf_libs": ["qio"]}]}))
        defaults = WIN.defaults(builder)
        self.assertIn("defconfig.esp32p4_es", defaults)
        self.assertIn(SDK.FEATURE, defaults)
        self.assertNotIn("other", defaults)

    def test_windows_metadata_preserves_define_quotes(self):
        command = r'C:\esp\gcc.exe -IC:/esp/include -DIDF_VER=\"v5.5.5\" @"C:/esp/flags"'
        expected = r'/c/esp/gcc.exe -I/c/esp/include -DIDF_VER=\"v5.5.5\" @"/c/esp/flags"'
        self.assertEqual(WIN.META.msys_paths(command), expected)

    def test_windows_metadata_reads_json_instead_of_splitting_drive_colon(self):
        (self.root / "build").mkdir()
        path = self.root / "build/compile_commands.json"
        row = {"file": r"C:\esp\main\test.c", "command": r"C:\esp\gcc.exe -IC:/esp/include"}
        path.write_text(json.dumps([row]))
        self.assertEqual(WIN.META.compile_command(self.root, "test.c"), "/c/esp/gcc.exe -I/c/esp/include")
        path.write_text(json.dumps([row, row]))
        with self.assertRaisesRegex(ValueError, "Expected one"):
            WIN.META.compile_command(self.root, "test.c")

    def test_windows_link_metadata_rejects_ambiguous_fields(self):
        (self.root / "build").mkdir()
        path = self.root / "build/build.ninja"
        text = '  LINK_FLAGS = -T sections.ld\n  LINK_LIBRARIES = test.a\n  LINK_PATH = -LC:/esp/ld\n'
        path.write_text(text)
        self.assertEqual(WIN.META.link_command(self.root), '-T sections.ld test.a -L/c/esp/ld')
        path.write_text(text + '  LINK_FLAGS = other\n')
        with self.assertRaisesRegex(ValueError, "Expected one"):
            WIN.META.link_command(self.root)

    def test_windows_exporter_rejects_other_upstream_script(self):
        with self.assertRaisesRegex(ValueError, "Upstream export command changed"):
            WIN.META.exporter_script("unrelated script")

    def test_windows_archive_alias_retry_refreshes_and_exports_current_bytes(self):
        anchor = '\t' * 9 + 'if [ -f "$n_item" ]; then\n'
        source = anchor + 'keep original link handling\n'
        repaired = WIN.META.refresh_archive_aliases(source)
        self.assertIn('cp -f "$item" "$n_item" || exit 1', repaired)
        self.assertIn('LD_LIB_FILES+="$n_item "', repaired)
        self.assertIn('$EXCLUDE_LIBS != *";$lname;"*', repaired)
        self.assertTrue(repaired.endswith('keep original link handling\n'))
        for invalid in ('unrelated', source + source):
            with self.assertRaisesRegex(ValueError, 'alias handling changed'):
                WIN.META.refresh_archive_aliases(invalid)

    def test_windows_memory_variant_uses_json_and_is_idempotent(self):
        builder = self.root / 'builder'
        (builder / 'configs').mkdir(parents=True)
        entries = [
            {'targets': ['esp32p4_es'], 'out': 'lib/libspi_flash.a', 'file': 'libspi_flash.a'},
            {'targets': ['esp32s3'], 'out': 'absent', 'file': 's3.a'}]
        (builder / 'configs/builds.json').write_text(json.dumps({'mem_variants_files': entries}))
        sdk = builder / 'out/tools/esp32-arduino-libs/esp32p4_es'
        source = sdk / 'lib/libspi_flash.a'
        source.parent.mkdir(parents=True)
        source.write_bytes(b'current QIO library')
        WIN.META.publish_memory_variant(builder)
        WIN.META.publish_memory_variant(builder)
        self.assertFalse(source.exists())
        self.assertEqual((sdk / 'qio_qspi/libspi_flash.a').read_bytes(), b'current QIO library')
        self.assertFalse((sdk / 'qio_qspi/s3.a').exists())
        source.write_bytes(b'rebuilt QIO library')
        WIN.META.publish_memory_variant(builder)
        self.assertEqual((sdk / 'qio_qspi/libspi_flash.a').read_bytes(), b'rebuilt QIO library')
        (sdk / 'qio_qspi/libspi_flash.a').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing common memory variant'):
            WIN.META.publish_memory_variant(builder)

    def test_windows_memory_variant_rejects_external_output(self):
        (self.root / 'configs').mkdir()
        (self.root / 'configs/builds.json').write_text(json.dumps({'mem_variants_files': [
            {'targets': ['esp32p4_es'], 'out': '../../../../escape.a', 'file': 'test.a'}]}))
        with self.assertRaisesRegex(ValueError, 'escapes SDK'):
            WIN.META.publish_memory_variant(self.root)

    def test_windows_export_cannot_escape_owned_workspace(self):
        with patch.object(Path, "is_relative_to", return_value=False):
            with self.assertRaisesRegex(ValueError, "escapes the owned workspace"):
                WIN.export_libraries(self.root, {})

    def test_windows_header_copy_recovers_transitive_relative_includes(self):
        builder = self.root / 'builder'
        component = builder / 'components/lib'
        source = component / 'include/public'
        source.mkdir(parents=True)
        (component / 'private').mkdir()
        (component / 'more').mkdir()
        (source / 'main.h').write_text('#include "../../private/a.h"\n')
        (component / 'private/a.h').write_text('#include "../more/b.inc"\n')
        (component / 'more/b.inc').write_text('content\n')
        (source / 'skip.c').write_text('not a header')
        destination = builder / 'out/tools/esp32-arduino-libs/esp32p4_es/include/lib/include/public'
        WIN.META.copy_headers(builder, source, destination, component)
        parent = destination.parents[1]
        self.assertEqual((parent / 'private/a.h').read_text(), '#include "../more/b.inc"\n')
        self.assertEqual((parent / 'more/b.inc').read_text(), 'content\n')
        self.assertFalse((destination / 'skip.c').exists())
        (destination / 'main.h').write_text('preserved')
        WIN.META.copy_headers(builder, source, destination, component)
        self.assertEqual((destination / 'main.h').read_text(), 'preserved')

    def test_windows_header_copy_rejects_external_destination(self):
        component = self.root / 'components/lib'
        component.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, 'escapes'):
            WIN.META.copy_headers(self.root, component, self.root / 'other-output', component)

    def test_windows_header_copy_ignores_includes_outside_sdk(self):
        component = self.root / 'components/lib'
        component.mkdir(parents=True)
        (component / 'a.h').write_text('#include "../../../../../../../../escape.h"\n')
        destination = self.root / 'out/tools/esp32-arduino-libs/esp32p4_es/include/lib'
        WIN.META.copy_headers(self.root, component, destination, component)
        self.assertEqual([p.name for p in destination.iterdir()], ['a.h'])

    def test_windows_resume_requires_a_complete_common_export(self):
        with patch.object(WIN.SDK, 'check_sources'):
            with self.assertRaisesRegex(ValueError, 'common SDK export'):
                WIN.finish_sdk(self.root, {}, skip_export=True)

    def test_windows_resume_rejects_changed_exported_config(self):
        builder = self.root / 'builder'
        candidate = builder / 'out/tools/esp32-arduino-libs/esp32p4_es'
        for relative in ('sdkconfig', 'flags/ld_libs', 'lib/libesp_system.a', 'qio_qspi/include/sdkconfig.h'):
            file = candidate / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('exported')
        (builder / 'sdkconfig').write_text('different')
        with patch.object(WIN.SDK, 'check_sources'):
            with self.assertRaisesRegex(ValueError, 'configuration differs'):
                WIN.finish_sdk(self.root, {}, skip_export=True)


if __name__ == "__main__":
    unittest.main()
