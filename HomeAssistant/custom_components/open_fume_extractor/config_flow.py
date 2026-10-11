"""UI configuration, discovery, credential renewal and polling options."""
import re
import voluptuous as vol
from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.helpers import selector
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from .api import ApiError, InvalidAuth, OfeApi, UnsupportedFirmware, normalize_host
from .const import CONF_INTERVAL, DEFAULT_INTERVAL, DOMAIN, MAX_INTERVAL, MIN_INTERVAL


def login_schema(defaults):
    return vol.Schema({
        vol.Required("host", default=defaults.get("host", "")): str,
        vol.Required("username", default=defaults.get("username", "admin")): str,
        vol.Required("password"): selector.TextSelector(selector.TextSelectorConfig(type=selector.TextSelectorType.PASSWORD)),
    })


class OfeConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self):
        self.defaults = {}
        self.reauth_entry = None
        self.reconfigure_entry = None

    async def async_step_user(self, user_input=None):
        errors = {}
        if user_input is not None:
            try:
                data = {**user_input, "host": normalize_host(user_input["host"])}
                api = OfeApi(async_get_clientsession(self.hass), data["host"], data["username"], data["password"])
                identity = await api.connect()
                existing = self.reauth_entry or self.reconfigure_entry
                if existing:
                    if identity["master_uid"] != existing.unique_id:
                        return self.async_abort(reason="wrong_master")
                    return self.async_update_reload_and_abort(
                        existing, data_updates=data,
                        reason="reauth_successful" if self.reauth_entry else "reconfigure_successful",
                    )
                await self.async_set_unique_id(identity["master_uid"])
                self._abort_if_unique_id_configured()
                return self.async_create_entry(title="Open Fume Extractor", data=data)
            except InvalidAuth:
                errors["base"] = "invalid_auth"
            except UnsupportedFirmware:
                errors["base"] = "unsupported_firmware"
            except (ValueError, TypeError):
                errors["host"] = "invalid_host"
            except ApiError:
                errors["base"] = "cannot_connect"
            self.defaults = {k: user_input[k] for k in ("host", "username")}
        return self.async_show_form(step_id="user", data_schema=login_schema(self.defaults), errors=errors)

    async def async_step_zeroconf(self, discovery_info):
        host = discovery_info.host
        discovered_uid = discovery_info.properties.get("ofe_uid", "")
        if isinstance(discovered_uid, str) and re.fullmatch(r"[0-9A-Fa-f]{16}", discovered_uid):
            await self.async_set_unique_id(discovered_uid.upper())
            self._abort_if_unique_id_configured()
        else:
            await self._async_handle_discovery_without_unique_id()
        self.defaults = {"host": f"http://{'[' + host + ']' if ':' in host else host}:{discovery_info.port}"}
        self.context["title_placeholders"] = {"name": "Open Fume Extractor"}
        return await self.async_step_user()

    async def async_step_reauth(self, entry_data):
        self.reauth_entry = self._get_reauth_entry()
        self.defaults = {k: entry_data[k] for k in ("host", "username")}
        return await self.async_step_user()

    async def async_step_reconfigure(self, user_input=None):
        self.reconfigure_entry = self._get_reconfigure_entry()
        self.defaults = {k: self.reconfigure_entry.data[k] for k in ("host", "username")}
        return await self.async_step_user(user_input)

    @staticmethod
    @callback
    def async_get_options_flow(config_entry):
        return OfeOptionsFlow()


class OfeOptionsFlow(config_entries.OptionsFlow):
    async def async_step_init(self, user_input=None):
        if user_input is not None:
            return self.async_create_entry(title="", data=user_input)
        return self.async_show_form(step_id="init", data_schema=vol.Schema({
            vol.Required(CONF_INTERVAL, default=self.config_entry.options.get(CONF_INTERVAL, DEFAULT_INTERVAL)):
                vol.All(vol.Coerce(int), vol.Range(min=MIN_INTERVAL, max=MAX_INTERVAL)),
        }))
