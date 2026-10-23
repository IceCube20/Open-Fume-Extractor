"""Manage the bundled Lovelace module without changing dashboard layouts."""
import logging
from urllib.parse import urlsplit

from homeassistant.components.lovelace import LOVELACE_DATA
from homeassistant.components.lovelace.resources import ResourceStorageCollection
from homeassistant.exceptions import HomeAssistantError

CARD_VERSION = "0.1.0rc25"
CARD_PATH = "/open_fume_extractor/ofe-cards.js"
CARD_URL = f"{CARD_PATH}?v={CARD_VERSION}"
_LOGGER = logging.getLogger(__name__)


def _is_owned_resource(url):
    if not isinstance(url, str):
        return False
    try:
        parts = urlsplit(url)
    except ValueError:
        return False
    return not parts.scheme and not parts.netloc and parts.path == CARD_PATH


async def async_register_card_resource(hass):
    """Called under the integration's frontend lock, once static paths exist."""
    lovelace = hass.data.get(LOVELACE_DATA)
    if lovelace is None:
        _LOGGER.warning("OFE cards: Lovelace is unavailable; register %s manually", CARD_URL)
        return False
    resources = lovelace.resources
    if not isinstance(resources, ResourceStorageCollection):
        # Never edit user-owned configuration.yaml or inject an in-memory-only
        # duplicate. An unversioned URL works after a full browser reload.
        _LOGGER.info("OFE cards: YAML resources are user-managed; use %s as a module", CARD_PATH)
        return False
    try:
        await resources.async_get_info()  # Public API ensures storage is loaded.
        owned = [item for item in resources.async_items()
                 if item.get("id") and _is_owned_resource(item.get("url"))]
        values = {"url": CARD_URL, "res_type": "module"}
        if not owned:
            await resources.async_create_item(values)
        else:
            primary = owned[0]
            if primary.get("url") != CARD_URL or primary.get("type") != "module":
                await resources.async_update_item(primary["id"], values)
            # Remove only duplicates of this exact local bundle, not other
            # cards, external URLs, HACS resources or dashboard definitions.
            for duplicate in owned[1:]:
                await resources.async_delete_item(duplicate["id"])
    except (HomeAssistantError, OSError, AttributeError, TypeError, ValueError):
        _LOGGER.warning("OFE card resource setup failed; entities remain available. "
                        "Register %s manually if needed", CARD_URL, exc_info=True)
        return False
    return True
