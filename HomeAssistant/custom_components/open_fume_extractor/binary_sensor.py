"""Connection, activity and profile inputs."""
from homeassistant.components.binary_sensor import BinarySensorEntity
from .entity import OfeEntity, setup_entities


async def async_setup_entry(hass, entry, async_add_entities):
    await setup_entities(entry, async_add_entities, "binary_sensor", OfeBinarySensor)


class OfeBinarySensor(OfeEntity, BinarySensorEntity):
    @property
    def device_class(self):
        return self.spec.device_class

    @property
    def is_on(self):
        return None if self.spec.value is None else bool(self.spec.value)
