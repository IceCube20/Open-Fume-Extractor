"""Read-only master, telemetry and profile values."""
from homeassistant.components.sensor import SensorEntity
from .entity import OfeEntity, setup_entities


async def async_setup_entry(hass, entry, async_add_entities):
    await setup_entities(entry, async_add_entities, "sensor", OfeSensor)


class OfeSensor(OfeEntity, SensorEntity):
    @property
    def native_value(self):
        value = self.spec.value
        if isinstance(value, float) and value.is_integer():
            return int(value)
        return value if value is None or not isinstance(value, str) else value[:255]

    @property
    def native_unit_of_measurement(self):
        return self.spec.unit

    @property
    def device_class(self):
        return self.spec.device_class

    @property
    def state_class(self):
        return self.spec.state_class

    @property
    def suggested_display_precision(self):
        return self.spec.precision
