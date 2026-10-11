"""Diagnostics explicitly omit credentials, CSRF tokens and network identity."""


async def async_get_config_entry_diagnostics(hass, entry):
    coordinator = entry.runtime_data
    return {
        "master_firmware": coordinator.data.get("master_fw"),
        "last_update_success": coordinator.last_update_success,
        "entity_count": len(coordinator.specs),
        "modules": [{"type": m.get("type_name"), "firmware": m.get("fw"),
                     "online": m.get("online"), "transport": m.get("transport")}
                    for m in coordinator.modules.values()],
    }
