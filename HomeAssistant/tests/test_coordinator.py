"""Coordinator contract tests using small HA stubs, not a HA runtime."""
import importlib.util
from datetime import timedelta
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import AsyncMock, Mock, patch

ROOT = Path(__file__).resolve().parents[1] / "custom_components/open_fume_extractor"
PREFIX = "ofe_coordinator_test"
package = types.ModuleType(PREFIX)
package.__path__ = [str(ROOT)]
sys.modules[PREFIX] = package


class HomeAssistantError(Exception):
    pass


class ConfigEntryAuthFailed(HomeAssistantError):
    pass


class UpdateFailed(Exception):
    pass


class CoordinatorStub:
    def __class_getitem__(cls, _):
        return cls

    def __init__(self, *args, **kwargs):
        self.hass = args[0]
        self.update_interval = kwargs.get("update_interval")
        self.last_update_success = True
        self.async_request_refresh = AsyncMock()


# Only imports used by the coordinator are stubbed; production code is loaded unchanged.
for name in ("homeassistant", "homeassistant.exceptions", "homeassistant.helpers",
             "homeassistant.helpers.update_coordinator"):
    sys.modules[name] = types.ModuleType(name)
sys.modules["homeassistant.exceptions"].HomeAssistantError = HomeAssistantError
sys.modules["homeassistant.exceptions"].ConfigEntryAuthFailed = ConfigEntryAuthFailed
sys.modules["homeassistant.helpers.update_coordinator"].DataUpdateCoordinator = CoordinatorStub
sys.modules["homeassistant.helpers.update_coordinator"].UpdateFailed = UpdateFailed
core = sys.modules.setdefault("homeassistant.core", types.ModuleType("homeassistant.core"))
core.callback = lambda value: value
for kind in ("entity_registry", "device_registry"):
    module = types.ModuleType("homeassistant.helpers." + kind)
    module.async_get = Mock()
    sys.modules[module.__name__] = module
    setattr(sys.modules["homeassistant.helpers"], kind, module)
sys.modules["homeassistant.helpers.entity_registry"].async_entries_for_config_entry = Mock(return_value=[])
sys.modules["homeassistant.helpers.device_registry"].async_get.return_value.async_get_device.return_value = None
sys.modules["homeassistant.helpers.device_registry"].async_entries_for_config_entry = Mock(return_value=[])


def load(name):
    spec = importlib.util.spec_from_file_location(PREFIX + "." + name, ROOT / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


api = load("api")
model = load("model")
load("const")
coordinator = load("coordinator")
MASTER = "0000000012345678"
MODULE = "2000000012345678"
DEF = {"id": 20, "key": "out3", "label": "Valve", "mode": "rw", "type": "switch", "meta": {}}


def state(crc=1, definitions=None, addr=32):
    module = {"uid": MODULE, "addr": addr, "type": 7, "caps": 1 << 17, "online": True, "universal_descriptor_valid": True,
              "universal_descriptor_crc": crc, "universal_entities_valid": True,
              "universal_entities": [{"id": 20, "len": 1, "value_bool": True}]}
    if definitions is not None:
        module["universal_entity_defs"] = definitions
    return {"master_uid": MASTER, "master_fw": "2.0.31RC", "modules": [module], "modules_count": 1, "uptime_ms": 60000}


class CoordinatorTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.api = types.SimpleNamespace(identity={"master_uid": MASTER}, connect=AsyncMock(),
                                         state=AsyncMock(), command=AsyncMock())
        entry = types.SimpleNamespace(unique_id=MASTER, entry_id="test-entry", options={})
        self.coordinator = coordinator.OfeCoordinator(None, entry, self.api)

    def test_removed_modules_require_two_complete_lists_and_preserve_offline(self):
        empty = {**state(), "modules": [], "modules_count": 0}
        with patch.object(coordinator, "registered_module_uids", return_value={MODULE}):
            self.assertEqual(self.coordinator._confirmed_removed_modules(empty), set())
            self.assertEqual(self.coordinator._confirmed_removed_modules(empty), {MODULE})
            offline = state()
            offline["modules"][0]["online"] = False
            for _ in range(3):
                self.assertEqual(self.coordinator._confirmed_removed_modules(offline), set())

    def test_partial_startup_ota_and_wrong_identity_break_removal_confirmation(self):
        empty = {**state(), "modules": [], "modules_count": 0}
        invalid = [
            {**empty, "modules_count": 1}, {**empty, "modules_count": None},
            {**empty, "uptime_ms": 0}, {**empty, "module_fw_update_active": True},
            {**empty, "master_uid": MODULE},
            {**state(), "modules": [{"uid": ""}]},
            {**state(), "modules": [{"uid": "0000000000000000"}]},
            {**state(), "modules": [{"uid": MASTER}]},
            {**state(), "modules": [{"uid": MODULE}, {"uid": MODULE}], "modules_count": 2},
        ]
        with patch.object(coordinator, "registered_module_uids", return_value={MODULE}):
            for snapshot in invalid:
                with self.subTest(snapshot=snapshot):
                    self.coordinator._last_module_uids = None
                    self.assertEqual(self.coordinator._confirmed_removed_modules(empty), set())
                    self.assertEqual(self.coordinator._confirmed_removed_modules(snapshot), set())
                    self.assertEqual(self.coordinator._confirmed_removed_modules(empty), set())
                    self.assertEqual(self.coordinator._confirmed_removed_modules(empty), {MODULE})

    async def test_old_device_and_entities_are_removed_after_restart_and_can_reappear(self):
        registries = sys.modules[PREFIX + ".registry"]
        spec_id = MODULE + ":entity_20_out3"
        entity = types.SimpleNamespace(entity_id="switch.old_valve", platform="open_fume_extractor",
                                      options={}, unique_id=f"{MASTER}:{spec_id}:switch")
        entities = {entity.entity_id: entity}
        device = types.SimpleNamespace(id="old-device", config_entry_id="test-entry",
                                       is_composite_device=False,
                                       identifiers={("open_fume_extractor", f"{MASTER}:{MODULE}")})
        devices = {device.id: device}
        eb, db = Mock(), Mock()
        eb.async_remove.side_effect = entities.pop
        db.async_remove_device.side_effect = devices.pop
        db.async_get_device.return_value = None
        empty = {**state(), "modules": [], "modules_count": 0}
        self.api.state.side_effect = [empty, empty, state(), state(definitions=[DEF])]
        self.coordinator.definitions[MODULE] = [DEF]
        self.coordinator.crcs[MODULE] = 1
        with patch.object(registries.er, "async_get", return_value=eb), patch.object(
                registries.er, "async_entries_for_config_entry", side_effect=lambda *_: list(entities.values())), patch.object(
                registries.dr, "async_get", return_value=db), patch.object(
                registries.dr, "async_entries_for_config_entry", side_effect=lambda *_: list(devices.values())):
            await self.coordinator._async_update_data()
            eb.async_remove.assert_not_called(); db.async_remove_device.assert_not_called()
            await self.coordinator._async_update_data()
            eb.async_remove.assert_called_once_with(entity.entity_id)
            db.async_remove_device.assert_called_once_with(device.id)
            self.assertNotIn(MODULE, self.coordinator.definitions)
            self.assertNotIn(MODULE, self.coordinator.crcs)
            self.assertEqual(self.coordinator.entity_revisions[(spec_id, "switch")], 1)
            await self.coordinator._async_update_data()
            self.assertIn(spec_id, self.coordinator.specs)

    async def test_jbc_station_switch_removes_existing_old_entities_after_two_snapshots(self):
        registries = sys.modules[PREFIX + ".registry"]
        uid = "1100000012345678"
        old = types.SimpleNamespace(entity_id="sensor.old_sleep", platform="open_fume_extractor", options={},
                                    unique_id=f"{MASTER}:{uid}:jbc_p1_future_mode:sensor")
        items = {old.entity_id: old}
        backend = Mock(); backend.async_remove.side_effect = items.pop
        module = {"uid": uid, "type": 9, "online": True, "caps": 1 << 24,
                  "ha_jbc": {"valid": True, "jbc_link": True, "jbc_family": "CL", "jbc_port_count": 1,
                             "jbc_p1_valid": True, "jbc_p1_state": "IDLE", "controls": []}}
        snapshot = {**state(), "modules": [module]}
        self.api.state.side_effect = [snapshot, snapshot]
        with patch.object(registries.er, "async_get", return_value=backend), patch.object(
                registries.er, "async_entries_for_config_entry", side_effect=lambda *_: list(items.values())):
            await self.coordinator._async_update_data(); backend.async_remove.assert_not_called()
            await self.coordinator._async_update_data()
            backend.async_remove.assert_called_once_with(old.entity_id)

    async def test_metadata_fetched_only_on_crc_change(self):
        self.api.state.side_effect = [state(), state(definitions=[DEF]), state()]
        await self.coordinator._async_update_data()
        await self.coordinator._async_update_data()
        self.assertEqual(self.api.state.await_count, 3)
        self.assertEqual(self.api.state.await_args_list[1].kwargs, {"descriptors": True})

    async def test_station_absence_prunes_old_entities_but_not_the_usb_module(self):
        registries = sys.modules[PREFIX + ".registry"]
        uid = "1100000012345678"
        items = {}
        for key, platform in (("jbc_p1_future_mode", "sensor"), ("jbc_p2_temperature", "sensor"),
                              ("jbc_cfg_0", "number"), ("jbc_p1_cl_counter_plug_min", "sensor"),
                              ("online", "binary_sensor"), ("jbc_link", "binary_sensor")):
            item = types.SimpleNamespace(entity_id=f"{platform}.{key}", platform="open_fume_extractor", options={},
                                         unique_id=f"{MASTER}:{uid}:{key}:{platform}")
            items[item.entity_id] = item
        backend = Mock(); backend.async_remove.side_effect = items.pop
        core = {"valid": True, "jbc_link": False, "jbc_family": "UNKNOWN", "jbc_port_count": 0, "controls": []}
        module = {"uid": uid, "type": 9, "online": True, "caps": 1 << 24, "ha_jbc": core}
        snapshot = {**state(), "modules": [module]}
        self.api.state.return_value = snapshot
        with patch.object(registries.er, "async_get", return_value=backend), patch.object(
                registries.er, "async_entries_for_config_entry", side_effect=lambda *_: list(items.values())):
            await self.coordinator._async_update_data()
            backend.async_remove.assert_not_called()
            # A failed poll interrupts confirmation rather than deleting on a transient outage.
            self.api.state.side_effect = api.ApiError("Interrupted")
            with self.assertRaises(UpdateFailed):
                await self.coordinator._async_update_data()
            self.api.state.side_effect = None
            await self.coordinator._async_update_data(); backend.async_remove.assert_not_called()
            await self.coordinator._async_update_data()
            self.assertEqual(set(items), {"binary_sensor.online", "binary_sensor.jbc_link"})
            self.assertIn(uid, self.coordinator.modules)
            self.assertEqual(self.coordinator.entity_revisions[(uid + ":jbc_cfg_0", "number")], 1)
            core.update(jbc_link=True, jbc_family="SOLD", jbc_port_count=1, jbc_p1_valid=True,
                        jbc_cfg_0=350, controls=[{"id": 0, "platform": "number", "name": "Temperature"}])
            await self.coordinator._async_update_data()
            self.assertIn(uid + ":jbc_cfg_0", self.coordinator.specs)

    def test_polling_interval_default_and_boundaries(self):
        self.assertEqual(self.coordinator.update_interval, timedelta(seconds=5))
        for seconds in (1, 60):
            with self.subTest(seconds=seconds):
                entry = types.SimpleNamespace(unique_id=MASTER, entry_id="test-entry",
                                              options={"scan_interval": seconds})
                instance = coordinator.OfeCoordinator(None, entry, self.api)
                self.assertEqual(instance.update_interval, timedelta(seconds=seconds))

    async def test_clearing_profile_removes_specs(self):
        self.api.state.side_effect = [state(), state(definitions=[DEF]), state(2), state(2, [])]
        await self.coordinator._async_update_data()
        self.assertIn(MODULE + ":entity_20_out3", self.coordinator.specs)
        await self.coordinator._async_update_data()
        self.assertNotIn(MODULE + ":entity_20_out3", self.coordinator.specs)

    async def test_cleanup_requires_two_complete_matching_snapshots(self):
        self.api.state.side_effect = [state(), state(definitions=[DEF]), state()]
        await self.coordinator._async_update_data()
        expected = self.coordinator._inventories[MODULE]
        self.assertEqual(self.coordinator._confirmed_inventory(state()), {MASTER, MODULE})
        self.assertEqual(self.coordinator._inventories[MODULE], expected)
        self.coordinator.modules[MODULE]["online"] = False
        self.assertNotIn(MODULE, self.coordinator._confirmed_inventory(state()))
        self.coordinator.modules[MODULE]["online"] = True
        self.assertNotIn(MODULE, self.coordinator._confirmed_inventory(state()))
        self.assertIn(MODULE, self.coordinator._confirmed_inventory(state()))

    async def test_invalid_descriptor_and_ota_do_not_authorize_cleanup(self):
        self.api.state.side_effect = [state(), state(definitions=[DEF])]
        await self.coordinator._async_update_data()
        self.assertNotIn(MODULE, self.coordinator._confirmed_inventory({"module_fw_update_active": True}))
        self.coordinator.modules[MODULE]["universal_descriptor_valid"] = False
        for _ in range(2):
            self.assertNotIn(MODULE, self.coordinator._confirmed_inventory(state()))

    async def test_jbc_class_change_requires_linked_valid_core(self):
        self.coordinator.modules[MODULE] = {"type": 9, "online": True, "ha_jbc": {
            "valid": True, "jbc_link": True, "jbc_family": "SOLD", "jbc_port_count": 1}}
        self.assertNotIn(MODULE, self.coordinator._confirmed_inventory(state()))
        self.assertIn(MODULE, self.coordinator._confirmed_inventory(state()))
        self.coordinator.modules[MODULE]["ha_jbc"]["jbc_link"] = False
        self.coordinator.specs = {}
        self.assertNotIn(MODULE, self.coordinator._confirmed_inventory(state()))
        self.assertIn(MODULE, self.coordinator._confirmed_inventory(state()))
        self.coordinator.modules[MODULE]["ha_jbc"].pop("jbc_link")
        self.assertNotIn(MODULE, self.coordinator._confirmed_inventory(state()))

    async def test_poll_error_breaks_inventory_confirmation(self):
        self.api.state.side_effect = [state(), state(definitions=[DEF]), api.ApiError("Disconnected")]
        await self.coordinator._async_update_data()
        with self.assertRaises(UpdateFailed):
            await self.coordinator._async_update_data()
        self.assertEqual(self.coordinator._inventories, {})

    async def test_confirmed_profile_removal_cleans_registry_then_allows_reappearance(self):
        er = sys.modules[PREFIX + ".registry"].er
        spec_id = MODULE + ":entity_20_out3"
        item = types.SimpleNamespace(entity_id="switch.valve", platform="open_fume_extractor", options={},
                                     unique_id=f"{MASTER}:{spec_id}:switch")
        items = {item.entity_id: item}
        backend = Mock()
        backend.async_remove.side_effect = items.pop
        self.api.state.side_effect = [state(), state(definitions=[DEF]), state(),
                                     state(2), state(2, []), state(2), state(3), state(3, [DEF])]
        with patch.object(er, "async_get", return_value=backend), patch.object(
                er, "async_entries_for_config_entry", side_effect=lambda *_: list(items.values())):
            await self.coordinator._async_update_data()
            await self.coordinator._async_update_data()
            await self.coordinator._async_update_data()
            backend.async_remove.assert_not_called()
            await self.coordinator._async_update_data()
            backend.async_remove.assert_called_once_with("switch.valve")
            self.assertEqual(self.coordinator.entity_revisions[(spec_id, "switch")], 1)
            await self.coordinator._async_update_data()
            self.assertIn(spec_id, self.coordinator.specs)
            backend.async_remove.assert_called_once()

    async def test_language_change_refreshes_labels_without_crc_change(self):
        first = {**state(), "ui_lang": "de"}
        first_full = {**state(definitions=[{**DEF, "label": "Ventil"}]), "ui_lang": "de"}
        second = {**state(), "ui_lang": "en"}
        second_full = {**state(definitions=[DEF]), "ui_lang": "en"}
        self.api.state.side_effect = [first, first_full, second, second_full]
        await self.coordinator._async_update_data()
        self.assertEqual(self.coordinator.specs[MODULE + ":entity_20_out3"].name, "Ventil")
        await self.coordinator._async_update_data()
        self.assertEqual(self.coordinator.specs[MODULE + ":entity_20_out3"].name, "Valve")
        self.assertEqual(self.api.state.await_count, 4)

    async def test_invalidated_descriptor_does_not_keep_old_controls(self):
        cleared = state()
        cleared["modules"][0]["universal_descriptor_valid"] = False
        self.api.state.side_effect = [state(), state(definitions=[DEF]), cleared]
        await self.coordinator._async_update_data()
        await self.coordinator._async_update_data()
        self.assertNotIn(MODULE + ":entity_20_out3", self.coordinator.specs)
        self.assertNotIn(MODULE, self.coordinator.definitions)

    async def test_command_resolves_current_address_and_uid(self):
        self.api.state.side_effect = [state(), state(definitions=[DEF])]
        await self.coordinator._async_update_data()
        spec = self.coordinator.specs[MODULE + ":entity_20_out3"]
        self.coordinator.modules[MODULE]["addr"] = 35
        await self.coordinator.command(spec, "1")
        self.assertEqual(self.api.command.await_args.args[0],
                         {"action": "entity", "id": 20, "value": "1", "addr": 35, "uid": MODULE})
        self.coordinator.async_request_refresh.assert_awaited_once()

    async def test_removed_or_type_changed_entity_cannot_write(self):
        spec = model.Spec(MODULE, "entity_20", "switch", "Valve", action="entity")
        with self.assertRaises(HomeAssistantError):
            await self.coordinator.command(spec, "1")
        self.coordinator.specs[spec.id] = model.Spec(MODULE, "entity_20", "number", "Power", action="entity")
        with self.assertRaises(HomeAssistantError):
            await self.coordinator.command(spec, "1")
        self.api.command.assert_not_awaited()

    async def test_jbc_control_keeps_parameter_id_and_uses_current_module_identity(self):
        spec = model.Spec(MODULE, "jbc_cfg_19", "select", "Temperature level", "Level 1",
                          action="jbc_usb", params={"control": 19})
        self.coordinator.specs[spec.id] = spec
        self.coordinator.modules[MODULE] = {"online": True, "addr": 18}
        await self.coordinator.command(spec, "2")
        self.assertEqual(self.api.command.await_args.args[0],
                         {"action": "jbc_usb", "control": 19, "value": "2", "addr": 18, "uid": MODULE})
        self.assertEqual(spec.value, "Level 1")
        self.coordinator.specs[spec.id].valid = False
        with self.assertRaises(HomeAssistantError):
            await self.coordinator.command(spec, "1")
        self.api.command.assert_awaited_once()

    async def test_offline_or_failed_poll_cannot_write(self):
        spec = model.Spec(MASTER, "continuous", "switch", "Continuous", action="extractor")
        self.coordinator.specs[spec.id] = spec
        self.coordinator.last_update_success = False
        with self.assertRaises(HomeAssistantError):
            await self.coordinator.command(spec, "1")
        self.api.command.assert_not_awaited()

    async def test_master_identity_change_blocks_setup(self):
        self.api.identity["master_uid"] = MODULE
        with self.assertRaises(UpdateFailed):
            await self.coordinator._async_setup()

    async def test_auth_failure_starts_reauth(self):
        self.api.state.side_effect = api.InvalidAuth("Rejected")
        with self.assertRaises(ConfigEntryAuthFailed):
            await self.coordinator._async_update_data()

    async def test_connection_failure_is_update_failed(self):
        self.api.state.side_effect = api.ApiError("Connection failed")
        with self.assertRaises(UpdateFailed):
            await self.coordinator._async_update_data()

    async def test_command_timeout_not_retried_or_optimistic(self):
        spec = model.Spec(MASTER, "continuous", "switch", "Continuous", False,
                          action="extractor", argument="continuous")
        self.coordinator.specs[spec.id] = spec
        self.api.command.side_effect = api.ApiError("Connection failed")
        with self.assertRaises(HomeAssistantError):
            await self.coordinator.command(spec, "1")
        self.assertFalse(spec.value)
        self.api.command.assert_awaited_once()
        self.coordinator.async_request_refresh.assert_not_awaited()


if __name__ == "__main__":
    unittest.main()
