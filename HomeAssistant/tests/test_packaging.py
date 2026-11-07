"""Standalone installation, HACS metadata and release archive contracts."""
import ast
import importlib.util
import json
from pathlib import Path
import re
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "custom_components/open_fume_extractor"


class PackagingTests(unittest.TestCase):
    def test_hacs_layout_has_exactly_one_self_contained_integration(self):
        self.assertEqual([p.name for p in (ROOT / "custom_components").iterdir() if p.is_dir() and p.name != "__pycache__"],
                         ["open_fume_extractor"])
        hacs = json.loads((ROOT / "hacs.json").read_text())
        self.assertEqual(hacs["name"], "Open Fume Extractor")
        self.assertIs(hacs.get("hide_default_branch"), True)
        self.assertFalse(hacs.get("content_in_root", False))
        self.assertFalse(hacs.get("zip_release", False))
        self.assertTrue((COMPONENT / "brand/icon.png").is_file())

    def test_manifest_version_and_links_are_standalone(self):
        manifest = json.loads((COMPONENT / "manifest.json").read_text())
        self.assertEqual(manifest["domain"], "open_fume_extractor")
        self.assertTrue(manifest["config_flow"])
        self.assertEqual(manifest["iot_class"], "local_polling")
        self.assertEqual(manifest["codeowners"], ["@IceCube20"])
        repo = "https://github.com/IceCube20/Open-Fume-Extractor-Home-Assistant"
        self.assertEqual(manifest["documentation"], repo)
        self.assertEqual(manifest["issue_tracker"], repo + "/issues")
        version = manifest["version"]
        js = (COMPONENT / "frontend/ofe-cards.js").read_text(encoding="utf-8")
        self.assertIn(f'const VERSION = "{version}";', js)
        self.assertIn(f"ofe-cards.js?v={version}", (ROOT / "README.md").read_text())

    def test_python_sources_and_translation_keys(self):
        for path in COMPONENT.rglob("*.py"):
            ast.parse(path.read_text(encoding="utf-8"))
        def keys(value, prefix=""):
            if not isinstance(value, dict):
                return {prefix}
            return set().union(*(keys(v, prefix + "." + k) for k, v in value.items()))
        expected = keys(json.loads((COMPONENT / "strings.json").read_text(encoding="utf-8")))
        for language in ("de", "en"):
            self.assertEqual(keys(json.loads((COMPONENT / f"translations/{language}.json").read_text(encoding="utf-8"))), expected)

    def test_archive_matches_runtime_sources_without_private_files(self):
        loader = importlib.util.spec_from_file_location("ofe_build_package", ROOT / "tools/build_package.py")
        module = importlib.util.module_from_spec(loader)
        loader.loader.exec_module(module)
        with zipfile.ZipFile(module.build()) as archive:
            files = {name: archive.read(name) for name in archive.namelist()}
        for path in COMPONENT.rglob("*"):
            if path.is_file() and "__pycache__" not in path.parts and path.suffix in (".py", ".json", ".js", ".png"):
                self.assertEqual(files[path.relative_to(ROOT).as_posix()], path.read_bytes())
        self.assertEqual(files["LICENSE"], (ROOT / "LICENSE").read_bytes())
        for name in files:
            self.assertFalse(re.search(r"__pycache__|\.pem$|\.key$|\.bin$|^tests/", name), name)
