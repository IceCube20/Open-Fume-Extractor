"""Entity lifecycle contracts, including HA's reserved platform attribute."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import AsyncMock, Mock

ROOT = Path(__file__).resolve().parents[1] / "custom_components/open_fume_extractor"
PREFIX = "ofe_entity_test"
package = types.ModuleType(PREFIX)
package.__path__ = [str(ROOT)]
sys.modules[PREFIX] = package
for name in ("homeassistant", "homeassistant.helpers", "homeassistant.helpers.update_coordinator"):
    sys.modules.setdefault(name, types.ModuleType(name))


class CoordinatorEntityStub:
    def __init__(self, coordinator):
        self.coordinator = coordinator

    @property
    def available(self):
        return self.coordinator.last_update_success

    async def async_added_to_hass(self):
        pass


registry = types.ModuleType("homeassistant.helpers.device_registry")
registry.async_get = Mock()
core = types.ModuleType("homeassistant.core")
core.callback = lambda value: value
base_entity = types.ModuleType("homeassistant.helpers.entity")
base_entity.DeviceInfo = dict
base_entity.EntityCategory = types.SimpleNamespace(DIAGNOSTIC="diagnostic")
sys.modules["homeassistant.core"] = core
sys.modules["homeassistant.helpers.entity"] = base_entity
sys.modules["homeassistant.helpers.device_registry"] = registry
sys.modules["homeassistant.helpers"].device_registry = registry
sys.modules["homeassistant.helpers.update_coordinator"].CoordinatorEntity = CoordinatorEntityStub


def load(name):
    spec = importlib.util.spec_from_file_location(PREFIX + "." + name, ROOT / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


load("const")
model = load("model")
entity = load("entity")

for name, base in (("sensor", "SensorEntity"), ("binary_sensor", "BinarySensorEntity"), ("number", "NumberEntity"), ("text", "TextEntity")):
    module = types.ModuleType("homeassistant.components." + name)
    setattr(module, base, type(base, (), {}))
    if name == "number":
        module.NumberMode = types.SimpleNamespace(SLIDER="slider", BOX="box")
    if name == "text":
        module.TextEntity.native_max = property(lambda self: self._attr_native_max)
    sys.modules.setdefault("homeassistant.components", types.ModuleType("homeassistant.components"))
    sys.modules[module.__name__] = module
sensor = load("sensor")
binary_sensor = load("binary_sensor")
number = load("number")
exceptions = sys.modules.setdefault("homeassistant.exceptions", types.ModuleType("homeassistant.exceptions"))
if not hasattr(exceptions, "HomeAssistantError"):
    exceptions.HomeAssistantError = type("HomeAssistantError", (Exception,), {})
text = load("text")


class EntityTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.spec = model.Spec("MODULE", "power", "sensor", "Power", 55)
        self.coordinator = types.SimpleNamespace(
            specs={self.spec.id: self.spec}, master_uid="MASTER", master_device_id="master-device-id",
            modules={"MODULE": {"addr": 32, "display_name": "Fan", "type_name": "Fan/IO", "fw": "1.1.70RC"}},
            data={"master_fw": "2.0.32RC"}, api=types.SimpleNamespace(host="http://example.local"),
            last_update_success=True, entity_revisions={}, async_add_listener=Mock(),
        )
        self.entity = entity.OfeEntity(self.coordinator, self.spec)

    def test_module_name_is_short_and_device_owns_address_and_alias(self):
        self.spec.name = "Ventil"
        self.assertEqual(self.entity.name, "Ventil")
        self.assertTrue(self.entity._attr_has_entity_name)
        self.assertEqual(self.entity.extra_state_attributes["ofe_name"], "Ventil")
        self.assertEqual(self.entity.device_info["name"], "0x20 Fan")
        self.coordinator.modules["MODULE"]["display_name"] = "Changed alias"
        self.assertEqual(self.entity.name, "Ventil")
        self.assertEqual(self.entity.device_info["name"], "0x20 Changed alias")

    def test_address_changes_device_not_entity_caption_or_identity(self):
        identity = self.entity._attr_unique_id
        self.coordinator.modules["MODULE"]["addr"] = 0x21
        self.assertEqual(self.entity.name, "Power")
        self.assertEqual(self.entity.device_info["name"], "0x21 Fan")
        self.assertEqual(self.entity._attr_unique_id, identity)

    def test_master_keeps_home_assistant_device_naming(self):
        spec = model.Spec("MASTER", "power", "sensor", "Power", 55)
        item = entity.OfeEntity(self.coordinator, spec)
        self.assertTrue(item._attr_has_entity_name)
        self.assertEqual(item.name, "Power")
        self.assertEqual(item.device_info["name"], "0x01 Open Fume Extractor Master")
        self.assertEqual(item.device_info["model"], "Open Fume Extractor Master")

    def test_card_metadata_uses_live_module_type_and_transport(self):
        self.coordinator.modules["MODULE"].update(type=3, transport="rs485")
        attributes = self.entity.extra_state_attributes
        self.assertEqual(attributes["ofe_device_model"], "Fan/IO")
        self.assertEqual(attributes["ofe_device_type"], 3)
        self.assertEqual(attributes["ofe_transport"], "rs485")
        self.coordinator.modules["MODULE"]["transport"] = "wifi"
        self.assertEqual(self.entity.extra_state_attributes["ofe_transport"], "wifi")
        master = entity.OfeEntity(self.coordinator, model.Spec("MASTER", "power", "sensor", "Power", 55))
        self.assertEqual(master.extra_state_attributes["ofe_device_type"], 0)
        self.assertEqual(master.extra_state_attributes["ofe_device_model"], "Open Fume Extractor Master")

    async def test_removed_channel_can_be_discovered_again_without_duplicates(self):
        add = Mock()
        entry = types.SimpleNamespace(runtime_data=self.coordinator, async_on_unload=Mock())
        await entity.setup_entities(entry, add, "sensor", sensor.OfeSensor)
        discover = self.coordinator.async_add_listener.call_args.args[0]
        discover()
        self.assertEqual(add.call_count, 1)
        self.coordinator.specs.clear()
        key = (self.spec.id, self.spec.platform)
        self.coordinator.entity_revisions[key] = 1
        discover()
        self.coordinator.specs[self.spec.id] = self.spec
        discover()
        self.assertEqual(add.call_count, 2)
        discover()
        self.assertEqual(add.call_count, 2)

    def test_whole_cpu_and_module_count_have_no_fractional_state_or_precision(self):
        for uid, key, value in (("MASTER", "cpu_load_pct", 30.0), ("MODULE", "module_cpu_load_pct", 30.0),
                                ("MASTER", "modules_count", 7.0)):
            state = {"ui_lang": "de"}
            spec = model.Spec(uid, key, "sensor", "Count", value, unit="%" if "cpu" in key else None)
            model.presentation(spec)
            model.mqtt_presentation(spec, state, "MASTER")
            item = sensor.OfeSensor(self.coordinator, spec)
            self.assertIsInstance(item.native_value, int)
            self.assertEqual(item.suggested_display_precision, 0)
        self.spec.value = 30.5
        self.assertEqual(sensor.OfeSensor(self.coordinator, self.spec).native_value, 30.5)

    def test_uptime_text_has_raw_seconds_attribute_without_numeric_metadata(self):
        self.coordinator.data["uptime_ms"] = 90060999
        self.coordinator.modules["MODULE"]["module_uptime_s"] = 3660
        for uid, key, text, seconds in (("MASTER", "uptime_ms", "1d 1h 1m", 90060),
                                        ("MODULE", "module_uptime_s", "1h 1m", 3660)):
            spec = model.Spec(uid, key, "sensor", "Uptime", text, diagnostic=True)
            model.presentation(spec)
            item = sensor.OfeSensor(self.coordinator, spec)
            self.assertEqual(item.native_value, text)
            self.assertEqual(item.extra_state_attributes["uptime_seconds"], seconds)
            self.assertIsNone(item.native_unit_of_measurement)
            self.assertIsNone(item.state_class)
        self.assertNotIn("uptime_seconds", self.entity.extra_state_attributes)

    def test_missing_or_invalid_address_does_not_make_up_a_bus_address(self):
        for value in (None, True, "32", -1, 256):
            self.coordinator.modules["MODULE"]["addr"] = value
            self.assertEqual(self.entity.name, "Power")

    def test_available_after_home_assistant_assigns_entity_platform(self):
        # Entity.add_to_platform_start assigns an EntityPlatform, not a string.
        self.entity.platform = types.SimpleNamespace(platform_name="sensor")
        self.assertTrue(self.entity.available)
        self.assertEqual(self.entity.entity_kind, "sensor")
        self.assertEqual(self.entity.platform.platform_name, "sensor")

    def test_failed_update_removed_or_changed_type_is_unavailable(self):
        self.coordinator.last_update_success = False
        self.assertFalse(self.entity.available)
        self.coordinator.last_update_success = True
        self.coordinator.specs[self.spec.id] = model.Spec("MODULE", "power", "number", "Power", 55)
        self.assertFalse(self.entity.available)
        self.coordinator.specs.clear()
        self.assertFalse(self.entity.available)

    def test_offline_module_is_unavailable(self):
        self.spec.valid = False
        self.assertFalse(self.entity.available)

    def test_device_info_never_passes_deprecated_via_device(self):
        self.assertNotIn("via_device", self.entity.device_info)

    def test_device_serial_is_same_hardware_uid_as_serial_sensor(self):
        for uid in ("MASTER", "MODULE"):
            spec = model.Spec(uid, "serial_number", "sensor", "Serial number", uid)
            item = sensor.OfeSensor(self.coordinator, spec)
            self.assertEqual(item.device_info["serial_number"], item.native_value)

    def test_card_attributes_follow_alias_and_do_not_expose_credentials(self):
        attrs = self.entity.extra_state_attributes
        self.assertEqual(attrs["ofe_uid"], "MODULE")
        self.assertEqual(attrs["ofe_master_uid"], "MASTER")
        self.assertTrue(attrs["ofe_active"])
        self.coordinator.modules["MODULE"]["display_name"] = "Changed"
        self.assertEqual(self.entity.extra_state_attributes["ofe_device_name"], "0x20 Changed")
        self.coordinator.specs.clear()
        self.assertFalse(self.entity.extra_state_attributes["ofe_active"])
        self.assertNotIn("password", attrs)

    def test_replaced_entity_kind_is_excluded_from_auto_cards(self):
        self.coordinator.specs[self.spec.id] = model.Spec("MODULE", "power", "number", "Power", 55)
        self.assertFalse(self.entity.extra_state_attributes["ofe_active"])

    def test_sensor_platform_exposes_metadata_and_full_precision_raw_value(self):
        self.spec.value, self.spec.unit = 175.71484375, "KiB"
        model.presentation(self.spec)
        item = sensor.OfeSensor(self.coordinator, self.spec)
        self.assertEqual(item.native_value, 175.71484375)
        self.assertEqual(item.native_unit_of_measurement, "KiB")
        self.assertEqual(item.suggested_display_precision, 0)
        self.assertEqual(item.device_class, "data_size")
        self.assertEqual(item.state_class, "measurement")

    def test_binary_sensor_platform_uses_connectivity_device_class(self):
        spec = model.Spec("MODULE", "online", "binary_sensor", "Connected", True)
        model.presentation(spec)
        item = binary_sensor.OfeBinarySensor(self.coordinator, spec)
        self.assertEqual(item.device_class, "connectivity")
        self.assertTrue(item.is_on)

    def test_number_platform_uses_box_for_afterrun(self):
        spec = model.Spec("MASTER", "delay_work", "number", "Work afterrun", 12)
        model.presentation(spec)
        item = number.OfeNumber(self.coordinator, spec)
        self.assertEqual(item.mode, "box")
        self.assertEqual(item.icon, "mdi:timer-cog-outline")

    async def test_text_control_enforces_utf8_payload_limit_before_sending(self):
        spec = model.Spec("MODULE", "entity_20_name", "text", "Alias", "Hello", action="entity")
        self.coordinator.command = AsyncMock()
        item = text.OfeText(self.coordinator, spec)
        self.assertEqual(item.native_value, "Hello")
        for value in ("", "A" * 188, "\u00e4" * 94):
            await item.async_set_value(value)
            self.coordinator.command.assert_awaited_with(spec, value)
        self.coordinator.command.reset_mock()
        for value in ("A" * 189, "\u00e4" * 95, None):
            with self.assertRaises(exceptions.HomeAssistantError):
                await item.async_set_value(value)
        self.coordinator.command.assert_not_awaited()

    async def test_module_linked_by_registry_id_after_add(self):
        device_registry = Mock()
        device_registry.async_get_device.return_value = types.SimpleNamespace(id="module-device-id", via_device_id=None)
        registry.async_get.return_value = device_registry
        self.entity.hass = object()
        await self.entity.async_added_to_hass()
        device_registry.async_update_device.assert_called_once_with("module-device-id", via_device_id="master-device-id")

    async def test_existing_parent_link_not_rewritten(self):
        device_registry = Mock()
        device_registry.async_get_device.return_value = types.SimpleNamespace(id="module-device-id", via_device_id="master-device-id")
        registry.async_get.return_value = device_registry
        self.entity.hass = object()
        await self.entity.async_added_to_hass()
        device_registry.async_update_device.assert_not_called()


if __name__ == "__main__":
    unittest.main()
