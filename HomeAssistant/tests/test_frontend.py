"""Exercise production resource registration without a running HA instance."""
import ast
import asyncio
import json
import logging
from pathlib import Path
from types import SimpleNamespace
import unittest
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "custom_components/open_fume_extractor"


class ResourceError(Exception):
    pass


class Resources:
    def __init__(self, items=(), error=False):
        self.items = [dict(item) for item in items]
        self.error = error
        self.loaded = False
        self.creates = self.updates = self.deletes = 0

    async def async_get_info(self):
        if self.error:
            raise ResourceError("storage unavailable")
        self.loaded = True
        await asyncio.sleep(0)
        return {"resources": len(self.items)}

    def async_items(self):
        assert self.loaded
        return self.items

    async def async_create_item(self, values):
        assert values["res_type"] == "module"
        self.creates += 1
        self.items.append({"id": "new", "url": values["url"], "type": values["res_type"]})

    async def async_update_item(self, item_id, values):
        self.updates += 1
        item = next(item for item in self.items if item["id"] == item_id)
        item.update(url=values["url"], type=values["res_type"])

    async def async_delete_item(self, item_id):
        self.deletes += 1
        self.items[:] = [item for item in self.items if item["id"] != item_id]


namespace = {"urlsplit": urlsplit, "ResourceStorageCollection": Resources,
             "HomeAssistantError": ResourceError, "LOVELACE_DATA": "lovelace",
             "logging": logging, "__name__": "ofe_resource_test"}
tree = ast.parse((COMPONENT / "frontend.py").read_text())
exec(compile(ast.Module(body=[node for node in tree.body
                             if not isinstance(node, (ast.Import, ast.ImportFrom))], type_ignores=[]),
             str(COMPONENT / "frontend.py"), "exec"), namespace)
register = namespace["async_register_card_resource"]
URL = namespace["CARD_URL"]
PATH = namespace["CARD_PATH"]


class FrontendTests(unittest.IsolatedAsyncioTestCase):
    async def test_new_registration_and_idempotent_reload(self):
        resources = Resources()
        hass = SimpleNamespace(data={"lovelace": SimpleNamespace(resources=resources)})
        self.assertTrue(await register(hass))
        self.assertTrue(await register(hass))
        self.assertEqual(resources.items, [{"id": "new", "url": URL, "type": "module"}])
        self.assertEqual((resources.creates, resources.updates, resources.deletes), (1, 0, 0))

    async def test_old_manual_url_type_and_duplicates_migrate_only_owned_bundle(self):
        unrelated = [
            {"id": "other", "url": "/hacsfiles/other/card.js", "type": "module"},
            {"id": "external", "url": "https://example.com" + PATH, "type": "module"},
            {"id": "lookalike", "url": PATH + ".backup", "type": "js"},
        ]
        resources = Resources([
            {"id": "keep-id", "url": PATH + "?v=0.1.0rc13", "type": "js"},
            {"id": "duplicate", "url": PATH, "type": "module"}, *unrelated,
        ])
        hass = SimpleNamespace(data={"lovelace": SimpleNamespace(resources=resources)})
        self.assertTrue(await register(hass))
        self.assertEqual(resources.items, [{"id": "keep-id", "url": URL, "type": "module"}, *unrelated])
        self.assertEqual((resources.creates, resources.updates, resources.deletes), (0, 1, 1))

    async def test_yaml_and_missing_lovelace_are_not_modified(self):
        yaml = SimpleNamespace(items=[{"url": PATH, "type": "module"}])
        self.assertFalse(await register(SimpleNamespace(data={"lovelace": SimpleNamespace(resources=yaml)})))
        self.assertEqual(yaml.items, [{"url": PATH, "type": "module"}])
        with self.assertLogs("ofe_resource_test", level="WARNING"):
            self.assertFalse(await register(SimpleNamespace(data={})))

    async def test_storage_failure_is_nonfatal_and_can_be_retried(self):
        resources = Resources(error=True)
        hass = SimpleNamespace(data={"lovelace": SimpleNamespace(resources=resources)})
        with self.assertLogs("ofe_resource_test", level="WARNING"):
            self.assertFalse(await register(hass))
        resources.error = False
        self.assertTrue(await register(hass))
        self.assertEqual(resources.creates, 1)

    async def test_multiple_entries_share_existing_frontend_lock(self):
        source = (COMPONENT / "__init__.py").read_text()
        setup = next(node for node in ast.parse(source).body if getattr(node, "name", "") == "async_setup_entry")
        lock = next(node for node in setup.body if isinstance(node, ast.AsyncWith))
        self.assertIn("await async_register_card_resource(hass)", ast.unparse(lock))
        resources = Resources()
        hass = SimpleNamespace(data={"lovelace": SimpleNamespace(resources=resources)})
        guard = asyncio.Lock()
        async def entry():
            async with guard:
                return await register(hass)
        self.assertEqual(await asyncio.gather(entry(), entry(), entry()), [True] * 3)
        self.assertEqual(resources.creates, 1)

    def test_versions_and_lovelace_setup_order_match_bundle(self):
        manifest = json.loads((COMPONENT / "manifest.json").read_text())
        self.assertEqual(namespace["CARD_VERSION"], manifest["version"])
        self.assertIn("lovelace", manifest["after_dependencies"])
        self.assertIn(f'const VERSION = "{manifest["version"]}";',
                      (COMPONENT / "frontend/ofe-cards.js").read_text())
        owned = namespace["_is_owned_resource"]
        for bad in (None, 1, "//example.com" + PATH, "http://[bad", "/other.js"):
            self.assertFalse(owned(bad))
