"""One cache-only poll per master, shared by every entity."""
from __future__ import annotations
from datetime import timedelta
import logging
import re
from homeassistant.exceptions import ConfigEntryAuthFailed, HomeAssistantError
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed
from .api import ApiError, InvalidAuth, OfeApi
from .const import CONF_INTERVAL, DEFAULT_INTERVAL
from .model import Spec, build_specs
from .registry import reconcile_entities, reconcile_devices, registered_module_uids, reconcile_removed_devices

_LOGGER = logging.getLogger(__name__)


class OfeCoordinator(DataUpdateCoordinator[dict]):
    def __init__(self, hass, entry, api: OfeApi):
        super().__init__(hass, _LOGGER, name="Open Fume Extractor", config_entry=entry,
                         update_interval=timedelta(seconds=entry.options.get(CONF_INTERVAL, DEFAULT_INTERVAL)))
        self.api = api
        self.entry = entry
        self.master_uid = entry.unique_id
        self.definitions: dict[str, list] = {}
        self.crcs: dict[str, int] = {}
        self.specs: dict[str, Spec] = {}
        self.modules: dict[str, dict] = {}
        self.language: str | None = None
        self.entity_revisions: dict[tuple[str, str], int] = {}
        self._inventories: dict[str, tuple] = {}
        self._last_module_uids: set[str] | None = None

    def _confirmed_removed_modules(self, state):
        """An offline module remains listed; only confirmed absence means removal."""
        modules = state.get("modules")
        count = state.get("modules_count")
        uptime = state.get("uptime_ms")
        if (not isinstance(modules, list) or not all(isinstance(m, dict) for m in modules)
                or state.get("master_uid", "").upper() != self.master_uid
                or type(count) is not int or count != len(modules)
                or type(uptime) is not int or uptime < 30000 or state.get("module_fw_update_active")):
            self._last_module_uids = None
            return set()
        present = {str(module.get("uid", "")).upper() for module in modules}
        if (len(present) != count or any(not re.fullmatch(r"[0-9A-F]{16}", uid)
                                       or uid in ("0000000000000000", self.master_uid) for uid in present)):
            self._last_module_uids = None
            return set()
        previous = self._last_module_uids
        self._last_module_uids = present
        if previous != present:
            return set()
        return registered_module_uids(self.hass, self.entry.entry_id, self.master_uid) - present

    def _confirmed_inventory(self, state):
        """Require two complete snapshots, including explicit station absence."""
        candidates = {self.master_uid}
        for uid, module in self.modules.items():
            typ = module.get("type")
            if (not module.get("online") or state.get("module_fw_update_active") or module.get("fw_busy")
                    or typ not in (1, 2, 3, 5, 6, 7, 8, 9)):
                continue
            if typ == 9:
                core = module.get("ha_jbc") or {}
                if (core.get("valid") is not True or type(core.get("jbc_link")) is not bool
                        or (core["jbc_link"] and (core.get("jbc_family") not in ("SOLD", "HA", "CL", "PH", "FE", "SF")
                                                  or not core.get("jbc_port_count")))):
                    continue
            if module.get("universal_descriptor_valid"):
                if uid not in self.definitions or self.crcs.get(uid) != module.get("universal_descriptor_crc"):
                    continue
            elif typ in (7, 8) or int(module.get("caps", 0)) & (1 << 17):
                continue
            if typ == 3 and not module.get("telemetry_valid"):
                continue
            candidates.add(uid)
        inventories = {}
        confirmed = set()
        for uid in candidates:
            module = self.modules.get(uid, {})
            core = module.get("ha_jbc") or {}
            fingerprint = (module.get("type"), module.get("caps"), module.get("universal_descriptor_crc"),
                           core.get("jbc_link"),
                           tuple(sorted((s.id, s.platform) for s in self.specs.values() if s.uid == uid)))
            inventories[uid] = fingerprint
            if self._inventories.get(uid) == fingerprint:
                confirmed.add(uid)
        self._inventories = inventories
        return confirmed

    async def _async_setup(self):
        try:
            await self.api.connect()
        except InvalidAuth as err:
            raise ConfigEntryAuthFailed from err
        except ApiError as err:
            raise UpdateFailed(str(err)) from err
        if self.api.identity["master_uid"] != self.master_uid:
            raise UpdateFailed("This address belongs to a different master")

    async def _async_update_data(self):
        try:
            state = await self.api.state()
            language_changed = self.language is not None and self.language != state.get("ui_lang")
            changed = any(
                m.get("universal_descriptor_valid") and
                (language_changed or self.crcs.get(str(m.get("uid", "")).upper()) != m.get("universal_descriptor_crc"))
                for m in state["modules"]
            )
            if changed:
                state = await self.api.state(descriptors=True)
            for m in state["modules"]:
                uid = str(m.get("uid", "")).upper()
                if m.get("online") and not m.get("universal_descriptor_valid"):
                    self.definitions.pop(uid, None)
                    self.crcs.pop(uid, None)
                if m.get("universal_descriptor_valid") and "universal_entity_defs" in m:
                    self.definitions[uid] = m["universal_entity_defs"]
                    self.crcs[uid] = m["universal_descriptor_crc"]
            self.language = state.get("ui_lang")
            self.modules = {str(m.get("uid", "")).upper(): m for m in state["modules"]}
            self.specs = build_specs(self.master_uid, state, self.definitions)
            removed_modules = self._confirmed_removed_modules(state)
            reconcile_devices(self.hass, self.master_uid, state, self.modules)
            removed = reconcile_entities(self.hass, self.entry.entry_id, self.master_uid,
                                         self.specs, self._confirmed_inventory(state) | removed_modules,
                                         state.get("developer_mode"))
            reconcile_removed_devices(self.hass, self.entry.entry_id, self.master_uid, removed_modules)
            for uid in removed_modules:
                self.definitions.pop(uid, None)
                self.crcs.pop(uid, None)
            for key in removed:
                self.entity_revisions[key] = self.entity_revisions.get(key, 0) + 1
            return state
        except InvalidAuth as err:
            self._inventories.clear()
            self._last_module_uids = None
            raise ConfigEntryAuthFailed from err
        except ApiError as err:
            self._inventories.clear()
            self._last_module_uids = None
            raise UpdateFailed(str(err)) from err

    async def command(self, spec: Spec, value=None):
        current = self.specs.get(spec.id)
        if (not self.last_update_success or not current or not current.valid or not current.action
                or current.platform != spec.platform or current.action != spec.action):
            raise HomeAssistantError("Control is unavailable")
        data = {"action": current.action, **current.params}
        if value is not None:
            data[current.argument] = value
        if current.uid != self.master_uid:
            module = self.modules.get(current.uid)
            if not module or not module.get("online"):
                raise HomeAssistantError("Module is offline")
            data.update(addr=module["addr"], uid=current.uid)
        try:
            await self.api.command(data)
        except InvalidAuth as err:
            raise ConfigEntryAuthFailed from err
        except ApiError as err:
            raise HomeAssistantError(str(err)) from err
        await self.async_request_refresh()
