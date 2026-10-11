"""Explicit restart and profile pulse actions; no automatic retries."""
from homeassistant.components.button import ButtonEntity
from .entity import OfeEntity, setup_entities


async def async_setup_entry(hass, entry, async_add_entities):
    await setup_entities(entry, async_add_entities, "button", OfeButton)


class OfeButton(OfeEntity, ButtonEntity):
    async def async_press(self):
        await self.coordinator.command(self.spec, "1" if self.spec.action == "entity" else None)
