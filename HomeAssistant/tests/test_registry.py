"""Registry reconciliation with explicit ownership and incomplete-cache cases."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[1] / "custom_components/open_fume_extractor"
PREFIX = "ofe_registry_test"
package = types.ModuleType(PREFIX)
package.__path__ = [str(ROOT)]
sys.modules[PREFIX] = package
for name in ("homeassistant", "homeassistant.helpers"):
    sys.modules.setdefault(name, types.ModuleType(name))
core = sys.modules.setdefault("homeassistant.core", types.ModuleType("homeassistant.core"))
core.callback = lambda value: value
er = types.ModuleType("homeassistant.helpers.entity_registry")
dr = types.ModuleType("homeassistant.helpers.device_registry")
er.async_get = Mock()
er.async_entries_for_config_entry = lambda registry, entry_id: [
    item for item in registry.items.values() if item.config_entry_id == entry_id]
dr.async_get = Mock()
for name, module in (("entity_registry", er), ("device_registry", dr)):
    sys.modules[module.__name__] = module
    setattr(sys.modules["homeassistant.helpers"], name, module)


def load(name):
    spec = importlib.util.spec_from_file_location(PREFIX + "." + name, ROOT / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


const = load("const")
registry = load("registry")


class RegistryTests(unittest.TestCase):
    def setUp(self):
        self.entries = {}
        self.backend = Mock(items=self.entries)
        self.backend.async_remove.side_effect = self.entries.pop
        er.async_get.return_value = self.backend

    def add(self, key, platform="sensor", uid="MODULE", owner=const.DOMAIN, entry="entry", **updates):
        item = types.SimpleNamespace(entity_id=platform + "." + key, platform=owner, options={},
                                     config_entry_id=entry, unique_id=f"MASTER:{uid}:{key}:{platform}", **updates)
        self.entries[item.entity_id] = item
        return item

    def desired(self, key, platform="sensor", uid="MODULE", precision=None):
        spec = types.SimpleNamespace(id=uid + ":" + key, uid=uid, platform=platform, precision=precision)
        return {spec.id: spec}

    def test_removed_channel_and_old_duplicate_are_really_removed(self):
        channel = self.add("entity_20_valve")
        duplicate = self.add("rpm")
        retained = self.add("entity_62_rpm")
        removed = registry.reconcile_entities(None, "entry", "MASTER", self.desired("entity_62_rpm"), {"MODULE"})
        self.assertEqual(set(self.entries), {retained.entity_id})
        self.assertEqual(removed, {("MODULE:entity_20_valve", "sensor"), ("MODULE:rpm", "sensor")})
        self.assertEqual(self.backend.async_remove.call_count, 2)

    def test_unconfirmed_offline_or_missing_module_is_preserved(self):
        self.add("old")
        self.assertEqual(registry.reconcile_entities(None, "entry", "MASTER", {}, {"MASTER"}), set())
        self.backend.async_remove.assert_not_called()

    def test_developer_off_removes_only_own_diagnostics_even_for_offline_modules(self):
        for key in ("module_heap_free", "module_heap_min", "module_loop_max_ms"):
            self.add(key)
        self.add("heap_free", uid="MASTER")
        self.add("loop_max_ms", uid="MASTER")
        self.add("mqtt_heap", owner="mqtt")
        self.add("other_entry_heap", entry="other")
        self.add("module_cpu_load_pct")
        removed = registry.reconcile_entities(None, "entry", "MASTER", {}, set(), developer_mode=False)
        self.assertEqual(len(removed), 5)
        self.assertEqual(set(self.entries), {"sensor.mqtt_heap", "sensor.other_entry_heap", "sensor.module_cpu_load_pct"})

    def test_missing_developer_state_never_prunes_unconfirmed_inventory(self):
        self.add("module_heap_free")
        registry.reconcile_entities(None, "entry", "MASTER", {}, set(), developer_mode=None)
        self.backend.async_remove.assert_not_called()

    def test_jbc_class_change_removes_only_unsupported_station_entities(self):
        self.add("jbc_p1_selected_temperature")
        self.add("jbc_p1_future_mode")
        retained = self.add("jbc_p1_cleaning_mode")
        registry.reconcile_entities(None, "entry", "MASTER", self.desired("jbc_p1_cleaning_mode"), {"MODULE"})
        self.assertEqual(set(self.entries), {retained.entity_id})

    def test_component_change_removes_old_component_and_disabled_stale_entries(self):
        self.add("entity_20_valve", disabled_by="user")
        removed = registry.reconcile_entities(None, "entry", "MASTER", self.desired("entity_20_valve", "number"), {"MODULE"})
        self.assertEqual(removed, {("MODULE:entity_20_valve", "sensor")})
        self.assertFalse(self.entries)

    def test_never_removes_mqtt_other_entry_other_master_or_unknown_identity(self):
        self.add("mqtt", owner="mqtt")
        self.add("other_entry", entry="different")
        other = self.add("other_master")
        other.unique_id = "OTHER:MODULE:old:sensor"
        malformed = self.add("unknown")
        malformed.unique_id = "MASTER:legacy"
        registry.reconcile_entities(None, "entry", "MASTER", {}, {"MODULE"})
        self.assertEqual(len(self.entries), 4)
        self.backend.async_remove.assert_not_called()

    def test_precision_updates_old_default_without_overwriting_user_options(self):
        item = self.add("cpu_load_pct", uid="MASTER")
        item.options = {"sensor": {"suggested_display_precision": 1, "display_precision": 2,
                                   "unit_of_measurement": "%"}, "other": {"custom": True}}
        registry.reconcile_entities(None, "entry", "MASTER", self.desired("cpu_load_pct", uid="MASTER", precision=0), {"MASTER"})
        self.backend.async_update_entity_options.assert_called_once_with(item.entity_id, "sensor", {
            "suggested_display_precision": 0, "display_precision": 2, "unit_of_measurement": "%"})
        self.assertEqual(item.options["other"], {"custom": True})

    def test_unchanged_precision_does_not_write_registry_again(self):
        item = self.add("modules_count", uid="MASTER")
        item.options = {"sensor": {"suggested_display_precision": 0}}
        registry.reconcile_entities(None, "entry", "MASTER", self.desired("modules_count", uid="MASTER", precision=0), {"MASTER"})
        self.backend.async_update_entity_options.assert_not_called()

    def test_master_and_module_device_metadata_match_mqtt_preserving_user_name(self):
        master = types.SimpleNamespace(id="master-device", name="Open Fume Extractor", model="Master",
                                       sw_version="2.0.37RC", name_by_user="My workshop")
        child = types.SimpleNamespace(id="module-device", name="Old alias", model="Fan/IO Pro", sw_version="1.1.70RC")
        backend = Mock()
        backend.async_get_device.side_effect = lambda **kw: master if (const.DOMAIN, "MASTER:MASTER") in kw["identifiers"] else child
        dr.async_get.return_value = backend
        registry.reconcile_devices(None, "MASTER", {"master_fw": "2.0.37RC"}, {
            "MODULE": {"addr": 32, "display_name": "Fan", "type_name": "Fan/IO Pro", "fw": "1.1.70RC"}})
        backend.async_update_device.assert_any_call("master-device", name="0x01 Open Fume Extractor Master", model="Open Fume Extractor Master")
        backend.async_update_device.assert_any_call("module-device", name="0x20 Fan")
        self.assertEqual(master.name_by_user, "My workshop")

    def test_master_device_constants_match_actual_mqtt_firmware_metadata(self):
        mqtt = (ROOT.parents[2] / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(encoding="utf-8")
        self.assertIn(r'\"name\":\"' + const.MASTER_DEVICE_NAME + r'\"', mqtt)
        self.assertIn(r'\"mdl\":\"' + const.MASTER_DEVICE_MODEL + r'\"', mqtt)


if __name__ == "__main__":
    unittest.main()
