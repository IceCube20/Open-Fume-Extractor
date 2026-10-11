"""Local Open Fume Extractor hub integration."""
from pathlib import Path
import asyncio
from homeassistant.components.http import StaticPathConfig
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers import device_registry as dr
from .api import OfeApi
from .const import DOMAIN, PLATFORMS, MASTER_DEVICE_NAME, MASTER_DEVICE_MODEL
from .coordinator import OfeCoordinator


async def async_setup_entry(hass, entry):
    domain_data = hass.data.setdefault(DOMAIN, {})
    async with domain_data.setdefault("frontend_lock", asyncio.Lock()):
        if not domain_data.get("frontend_registered"):
            root = Path(__file__).parent
            await hass.http.async_register_static_paths([
                StaticPathConfig("/open_fume_extractor/ofe-cards.js", str(root / "frontend/ofe-cards.js"), False),
                StaticPathConfig("/open_fume_extractor/logo.png", str(root / "brand/logo.png"), True),
            ])
            domain_data["frontend_registered"] = True
    api = OfeApi(async_get_clientsession(hass), entry.data["host"], entry.data["username"], entry.data["password"])
    coordinator = OfeCoordinator(hass, entry, api)
    await coordinator.async_config_entry_first_refresh()
    master = dr.async_get(hass).async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={(DOMAIN, f"{coordinator.master_uid}:{coordinator.master_uid}")},
        name=MASTER_DEVICE_NAME, manufacturer="IceCube20", model=MASTER_DEVICE_MODEL,
        sw_version=coordinator.data.get("master_fw"), serial_number=coordinator.master_uid,
        configuration_url=api.host,
    )
    coordinator.master_device_id = master.id
    entry.runtime_data = coordinator
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(async_reload_entry))
    return True


async def async_unload_entry(hass, entry):
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_reload_entry(hass, entry):
    await hass.config_entries.async_reload(entry.entry_id)
