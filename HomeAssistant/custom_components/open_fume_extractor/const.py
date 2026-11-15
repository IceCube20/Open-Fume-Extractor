"""Constants for the local Open Fume Extractor integration."""
DOMAIN = "open_fume_extractor"
MASTER_DEVICE_NAME = "0x01 Open Fume Extractor MCU"
MASTER_DEVICE_MODEL = "Open Fume Extractor MCU"
DEFAULT_INTERVAL = 5
MIN_INTERVAL = 0.5
MAX_INTERVAL = 60
CONF_INTERVAL = "scan_interval"
PLATFORMS = ("sensor", "binary_sensor", "switch", "number", "select", "button", "text")


def master_device_model(state):
    model = state.get("master_model")
    return model if model in ("MCU-S3", "MCU-P4") else MASTER_DEVICE_MODEL


def master_device_name(state):
    model = master_device_model(state)
    return f"0x01 Open Fume Extractor {model}" if model in ("MCU-S3", "MCU-P4") else MASTER_DEVICE_NAME


def module_device_name(module):
    name = module.get("display_name", "OFE module")
    address = module.get("addr")
    if isinstance(address, int) and not isinstance(address, bool) and 0 <= address <= 255:
        return f"0x{address:02X} {name}"
    return name
