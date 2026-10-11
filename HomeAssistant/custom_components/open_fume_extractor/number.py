"""Range-checked numeric settings."""
import math
from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.exceptions import HomeAssistantError
from .entity import OfeEntity, setup_entities


async def async_setup_entry(hass, entry, async_add_entities):
    await setup_entities(entry, async_add_entities, "number", OfeNumber)


class OfeNumber(OfeEntity, NumberEntity):
    @property
    def mode(self):
        return NumberMode.BOX if self.spec.number_mode == "box" else NumberMode.SLIDER

    @property
    def native_value(self):
        return self.spec.value

    @property
    def native_unit_of_measurement(self):
        return self.spec.unit

    @property
    def native_min_value(self):
        return self.spec.minimum

    @property
    def native_max_value(self):
        return self.spec.maximum

    @property
    def native_step(self):
        return self.spec.step

    async def async_set_native_value(self, value):
        if not math.isfinite(value) or not self.native_min_value <= value <= self.native_max_value:
            raise HomeAssistantError("Value outside supported range")
        steps = (value - self.native_min_value) / self.native_step
        if not math.isclose(steps, round(steps), abs_tol=1e-6):
            raise HomeAssistantError("Value does not match the supported step")
        await self.coordinator.command(self.spec, format(value, ".12g"))
