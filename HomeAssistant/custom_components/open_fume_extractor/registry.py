"""Reconcile only this integration's confirmed hardware entity inventory."""
from homeassistant.core import callback
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import device_registry as dr
from .const import DOMAIN, MASTER_DEVICE_NAME, MASTER_DEVICE_MODEL, module_device_name


@callback
def reconcile_devices(hass, master_uid, state, modules):
    registry = dr.async_get(hass)
    metadata = {master_uid: {"name": MASTER_DEVICE_NAME, "model": MASTER_DEVICE_MODEL,
                            "sw_version": state.get("master_fw")}}
    for uid, module in modules.items():
        if module.get("type_name"):
            metadata[uid] = {"name": module_device_name(module), "model": module["type_name"],
                             "sw_version": module.get("fw")}
    for uid, values in metadata.items():
        device = registry.async_get_device(identifiers={(DOMAIN, f"{master_uid}:{uid}")})
        if device:
            changed = {key: value for key, value in values.items()
                       if value is not None and getattr(device, key) != value}
            if changed:
                registry.async_update_device(device.id, **changed)


@callback
def reconcile_entities(hass, entry_id, master_uid, specs, confirmed_uids, developer_mode=None):
    registry = er.async_get(hass)
    desired = {f"{master_uid}:{spec.id}:{spec.platform}": spec for spec in specs.values()}
    removed = set()
    prefix = master_uid + ":"
    for item in list(er.async_entries_for_config_entry(registry, entry_id)):
        if item.platform != DOMAIN or not item.unique_id.startswith(prefix):
            continue
        identity = item.unique_id[len(prefix):]
        if ":" not in identity:
            continue
        spec_id, platform = identity.rsplit(":", 1)
        uid = spec_id.partition(":")[0]
        spec = desired.get(item.unique_id)
        if spec is None:
            key = spec_id.partition(":")[2]
            developer_only = key in ("heap_free", "heap_min", "loop_max_ms", "module_heap_free", "module_heap_min", "module_loop_max_ms")
            if uid in confirmed_uids or (developer_mode is False and developer_only):
                registry.async_remove(item.entity_id)
                removed.add((spec_id, platform))
            continue
        if spec.platform == "sensor" and spec.precision is not None:
            options = dict(item.options.get("sensor", {}))
            if options.get("suggested_display_precision") != spec.precision:
                options["suggested_display_precision"] = spec.precision
                registry.async_update_entity_options(item.entity_id, "sensor", options)
    return removed
