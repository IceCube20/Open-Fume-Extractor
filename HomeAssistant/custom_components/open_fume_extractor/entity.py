"""Hardware-based identities and dynamic entity discovery."""
from homeassistant.core import callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.entity import DeviceInfo, EntityCategory
from homeassistant.helpers.update_coordinator import CoordinatorEntity
from .const import DOMAIN, MASTER_DEVICE_NAME, MASTER_DEVICE_MODEL, module_device_name
from .model import divided


class OfeEntity(CoordinatorEntity):
    _attr_has_entity_name = True

    def __init__(self, coordinator, spec):
        super().__init__(coordinator)
        self.key = spec.id
        self.uid = spec.uid
        self.entity_kind = spec.platform
        self._initial = spec
        self._attr_unique_id = f"{coordinator.master_uid}:{spec.id}:{spec.platform}"
        if spec.diagnostic:
            self._attr_entity_category = EntityCategory.DIAGNOSTIC

    @property
    def spec(self):
        return self.coordinator.specs.get(self.key, self._initial)

    @property
    def name(self):
        return self.spec.name

    @property
    def icon(self):
        return self.spec.icon

    @property
    def extra_state_attributes(self):
        active = self.key in self.coordinator.specs and self.spec.platform == self.entity_kind
        module = self.coordinator.modules.get(self.uid, {})
        attributes = {
            "ofe_master_uid": self.coordinator.master_uid,
            "ofe_uid": self.uid, "ofe_key": self.spec.key,
            "ofe_group": self.spec.group, "ofe_name": self.name,
            "ofe_device_name": MASTER_DEVICE_NAME if self.uid == self.coordinator.master_uid else module_device_name(module),
            "ofe_device_model": MASTER_DEVICE_MODEL if self.uid == self.coordinator.master_uid else module.get("type_name"),
            "ofe_device_type": 0 if self.uid == self.coordinator.master_uid else module.get("type"),
            "ofe_transport": "wifi" if self.uid == self.coordinator.master_uid else module.get("transport"),
            "ofe_active": active,
        }
        attributes.update(self.spec.attributes)
        if self.uid == self.coordinator.master_uid and self.spec.key == "uptime_ms":
            seconds = divided(self.coordinator.data.get("uptime_ms"), 1000)
        elif self.uid == self.coordinator.master_uid and self.spec.key == "afterrun":
            seconds = divided(self.coordinator.data.get("afterrun_ms"), 1000)
            if seconds is not None and seconds >= 0:
                attributes["afterrun_seconds"] = seconds
            return attributes
        elif self.uid != self.coordinator.master_uid and self.spec.key == "module_uptime_s":
            seconds = divided(module.get("module_uptime_s"), 1)
        else:
            return attributes
        if seconds is not None and seconds >= 0:
            attributes["uptime_seconds"] = int(seconds)
        return attributes

    @property
    def available(self):
        return (super().available and self.key in self.coordinator.specs
                and self.spec.platform == self.entity_kind and self.spec.valid)

    async def async_added_to_hass(self):
        await super().async_added_to_hass()
        if self.uid != self.coordinator.master_uid:
            registry = dr.async_get(self.hass)
            device = registry.async_get_device(identifiers=self.device_info["identifiers"])
            if device and device.via_device_id != self.coordinator.master_device_id:
                registry.async_update_device(device.id, via_device_id=self.coordinator.master_device_id)

    @property
    def device_info(self):
        master = self.uid == self.coordinator.master_uid
        module = self.coordinator.modules.get(self.uid, {})
        info = DeviceInfo(
            identifiers={(DOMAIN, f"{self.coordinator.master_uid}:{self.uid}")},
            name=MASTER_DEVICE_NAME if master else module_device_name(module),
            manufacturer="IceCube20", model=MASTER_DEVICE_MODEL if master else module.get("type_name"),
            sw_version=self.coordinator.data.get("master_fw") if master else module.get("fw"),
            serial_number=self.uid, configuration_url=self.coordinator.api.host,
        )
        return info


async def setup_entities(entry, async_add_entities, platform, entity_type):
    coordinator = entry.runtime_data
    seen = {}

    @callback
    def discover():
        entities = []
        for spec in coordinator.specs.values():
            key = (spec.id, spec.platform)
            revision = coordinator.entity_revisions.get(key, 0)
            if spec.platform == platform and seen.get(key) != revision:
                seen[key] = revision
                entities.append(entity_type(coordinator, spec))
        if entities:
            async_add_entities(entities)

    discover()
    entry.async_on_unload(coordinator.async_add_listener(discover))
