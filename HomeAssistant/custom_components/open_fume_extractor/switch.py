"""Writable extractor settings and profile outputs."""
from homeassistant.components.switch import SwitchEntity
from .entity import OfeEntity, setup_entities


async def async_setup_entry(hass, entry, async_add_entities):
    await setup_entities(entry, async_add_entities, "switch", OfeSwitch)


class OfeSwitch(OfeEntity, SwitchEntity):
    @property
    def is_on(self):
        return None if self.spec.value is None else bool(self.spec.value)

    async def async_turn_on(self, **kwargs):
        await self.coordinator.command(self.spec, "1")

    async def async_turn_off(self, **kwargs):
        await self.coordinator.command(self.spec, "0")
