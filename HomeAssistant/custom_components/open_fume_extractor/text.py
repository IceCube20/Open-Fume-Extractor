"""Writable descriptor text entities, using the same ENTITY_SET as MQTT."""
from homeassistant.components.text import TextEntity
from homeassistant.exceptions import HomeAssistantError
from .entity import OfeEntity, setup_entities


async def async_setup_entry(hass, entry, async_add_entities):
    await setup_entities(entry, async_add_entities, "text", OfeText)


class OfeText(OfeEntity, TextEntity):
    _attr_native_min = 0
    _attr_native_max = 188

    @property
    def native_value(self):
        return self.spec.value

    async def async_set_value(self, value):
        if not isinstance(value, str) or len(value.encode("utf-8")) > self.native_max:
            raise HomeAssistantError("Text exceeds the module payload limit")
        await self.coordinator.command(self.spec, value)
