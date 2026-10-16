"""Reconcile only this integration's confirmed hardware entity inventory."""
import re
from homeassistant.core import callback
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import device_registry as dr
from .const import DOMAIN, MASTER_DEVICE_NAME, MASTER_DEVICE_MODEL, module_device_name


def _module_uid(master_uid, identity):
    prefix = master_uid + ":"
    if not identity.startswith(prefix):
        return None
    uid = identity[len(prefix):].partition(":")[0]
    return uid if re.fullmatch(r"[0-9A-F]{16}", uid) and uid != master_uid else None


@callback
def registered_module_uids(hass, entry_id, master_uid):
    """Include stale devices/entities which predate this coordinator's startup."""
    uids = set()
    entities = er.async_get(hass)
    for item in er.async_entries_for_config_entry(entities, entry_id):
        if item.platform == DOMAIN and (uid := _module_uid(master_uid, item.unique_id)):
            uids.add(uid)
    devices = dr.async_get(hass)
    for device in dr.async_entries_for_config_entry(devices, entry_id):
        for domain, identity in device.identifiers:
            if domain == DOMAIN and (uid := _module_uid(master_uid, identity)):
                uids.add(uid)
    return uids


@callback
def reconcile_removed_devices(hass, entry_id, master_uid, removed_uids):
    """Remove only confirmed absent devices exclusively owned by this entry."""
    if not removed_uids:
        return
    registry = dr.async_get(hass)
    for device in list(dr.async_entries_for_config_entry(registry, entry_id)):
        owner = getattr(device, "config_entry_id", None)
        if owner is not None:
            if owner != entry_id or getattr(device, "is_composite_device", False):
                continue
        elif getattr(device, "config_entries", set()) != {entry_id}:
            continue  # Older HA devices can be shared with another integration.
        if any(domain == DOMAIN and identity == f"{master_uid}:{uid}"
               for domain, identity in device.identifiers for uid in removed_uids if uid != master_uid):
            registry.async_remove_device(device.id)


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
