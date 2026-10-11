"""Suction level and descriptor-backed selections."""
from homeassistant.components.select import SelectEntity
from homeassistant.exceptions import HomeAssistantError
from .entity import OfeEntity, setup_entities


async def async_setup_entry(hass, entry, async_add_entities):
    await setup_entities(entry, async_add_entities, "select", OfeSelect)


class OfeSelect(OfeEntity, SelectEntity):
    @property
    def options(self):
        return list(self.spec.options)

    @property
    def current_option(self):
        return self.spec.value

    async def async_select_option(self, option):
        if option not in self.spec.options:
            raise HomeAssistantError("Unsupported option")
        await self.coordinator.command(self.spec, self.spec.option_values[self.spec.options.index(option)])
