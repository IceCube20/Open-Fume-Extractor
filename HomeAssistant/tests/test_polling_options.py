"""Exercise production options/reload methods with small HA flow stubs."""
import ast
from pathlib import Path
import types
import unittest
from unittest.mock import AsyncMock

import voluptuous as vol

ROOT = Path(__file__).resolve().parents[1] / "custom_components/open_fume_extractor"


class OptionsFlowStub:
    def async_show_form(self, **kwargs):
        return kwargs

    def async_create_entry(self, **kwargs):
        return kwargs


def load_definition(filename, name, namespace):
    tree = ast.parse((ROOT / filename).read_text(encoding="utf-8"))
    node = next(node for node in tree.body if getattr(node, "name", None) == name)
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(ROOT / filename), "exec"), namespace)
    return namespace[name]


constants = {}
exec(compile((ROOT / "const.py").read_text(encoding="utf-8"), str(ROOT / "const.py"), "exec"), constants)
flow_class = load_definition("config_flow.py", "OfeOptionsFlow", {
    **constants, "vol": vol, "config_entries": types.SimpleNamespace(OptionsFlow=OptionsFlowStub),
})
reload_entry = load_definition("__init__.py", "async_reload_entry", {})


class PollingOptionsTests(unittest.IsolatedAsyncioTestCase):
    async def test_schema_accepts_boundaries_and_rejects_outside_range(self):
        flow = flow_class()
        flow.config_entry = types.SimpleNamespace(options={})
        form = await flow.async_step_init()
        schema = form["data_schema"]
        self.assertEqual(schema({}), {"scan_interval": 5})
        for seconds in (1, 5, 60):
            with self.subTest(seconds=seconds):
                self.assertEqual(schema({"scan_interval": seconds}), {"scan_interval": seconds})
        for seconds in (0, 61):
            with self.subTest(seconds=seconds), self.assertRaises(vol.Invalid):
                schema({"scan_interval": seconds})

    async def test_selected_interval_is_saved_and_used_as_form_default(self):
        flow = flow_class()
        for seconds in (1, 60):
            flow.config_entry = types.SimpleNamespace(options={"scan_interval": seconds})
            form = await flow.async_step_init()
            self.assertEqual(form["data_schema"]({}), {"scan_interval": seconds})
            result = await flow.async_step_init({"scan_interval": seconds})
            self.assertEqual(result["data"], {"scan_interval": seconds})

    async def test_options_listener_reloads_entry(self):
        manager = types.SimpleNamespace(async_reload=AsyncMock())
        await reload_entry(types.SimpleNamespace(config_entries=manager),
                           types.SimpleNamespace(entry_id="test-entry"))
        manager.async_reload.assert_awaited_once_with("test-entry")
