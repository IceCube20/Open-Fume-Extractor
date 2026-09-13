# Master Guide

The Open Fume Extractor Master is the central controller for the OFE RS485 bus.
It discovers modules, assigns addresses, evaluates extraction and Boolean logic,
publishes Home Assistant entities, forwards alarms to displays and distributes
signed firmware updates.

## Before powering the system

- Verify all wiring against [PINOUT.md](PINOUT.md).
- Use a stable 5 V supply with enough reserve for the master, displays, modules
  and status LEDs.
- Keep the OFE bus linear, connect a common ground and terminate only the two
  physical ends with 120 ohms.
- Do not connect mains voltage, motor current or RS232 voltage levels directly
  to an ESP32 GPIO.

## Install the master firmware

For a new controller, use the
[browser-based USB flasher](https://icecube20.github.io/Open-Fume-Extractor/)
and select **Open Fume Extractor Master**. The `-merged.bin` image is a complete
first-install image and is written at address `0x0`.

Use the normal signed `.bin` file only on the master's **Updates** page. Never
upload a merged image through OTA.

## First-time setup

After a first flash or `network reset`, the master starts a setup access point.

| Setting | Default |
|---|---|
| Access point | `OpenFumeExtractor-XXXXXXXX` |
| WiFi password | `extractor123` |
| Web username | `admin` |
| Web password | `extractor123` |

1. Connect to the setup access point.
2. Open the master page and replace the default web password when prompted.
3. Select a WiFi network or enter its SSID manually.
4. Choose DHCP or enter the static IP, gateway, subnet and DNS settings.
5. Wait for the master to reconnect, then open the displayed IP address or its
   `.local` hostname.
6. Download a backup after commissioning is complete.

If the login is lost, connect the serial console at 115200 baud and run
`webauth reset`. The default credentials are setup credentials and must be
changed again after the next login.

## Discover and address modules

Open **Bus Diagnostics** after wiring the OFE bus.

1. Select **Scan modules** to discover connected and remembered devices.
2. Run **Address assignment** after adding modules or when two factory-default
   devices share an address.
3. Confirm that every device has a unique address in its module-family range.
4. Repeat the scan to reconcile the persistent inventory. Devices that are no
   longer present can then be removed from the installation and MQTT Discovery.

The master identifies modules by type and serial number. A single module of a
family is assigned the first address in that family's range.

## Configure the main signal path

The **Main extraction input** determines when extraction is requested. It may be
all JBC modules, one JBC module or a compatible module/entity input.

The **Main extraction output** controls fan enable and power. **Automatic** uses
an available compatible output and can fail over when it disappears. A manually
selected output remains explicit; if that module is offline, the system reports
that no main extraction output is available.

After selecting both sides, verify the complete path shown on the Status page.
The system is not ready while a required input/output is missing or a critical
remembered module is offline.

## Extraction settings

The Status page provides:

- Low, Medium, High and User power levels
- configurable User power
- separate Work and Stand after-run times
- optional after-run power
- Stand-intake handling
- Continuous mode

Power limits are constrained by the active output module. Slider changes are
stored with delayed/coalesced writes to reduce NVS wear.

## Additional rules

Additional rules connect one input directly to one action. They are useful for
simple tasks that do not need a complete Boolean graph. Sources include module
inputs and extractor system states. Targets include module outputs, Continuous
mode, extraction-level actions and main-output selection.

Stateful output rules are re-synchronized after a target module reconnects or
its Fan/IO hardware configuration is committed. A source does not need to
change again for the current output state to be restored.

## Boolean Logic Designer

Open **Logic Designer** from the main navigation.

1. Create or select a definition slot.
2. Add input, logic and output blocks.
3. Drag connections between block ports.
4. Assign real signals and targets in the Properties panel.
5. Use **Simulation** mode to test switches and buttons without operating real
   hardware.
6. Switch to **Live** mode to view actual node states, signal flow and remaining
   timer times.
7. Save the definition and enable it when ready.

Available blocks include AND, OR, NOT, Toggle, SR/RS latch, TON, TOF, R_TRIG and
Clock. Multiple saved definitions can be enabled independently. Use unique names
so status pages and backups remain easy to understand.

## Network and MQTT

**Network Setup** contains WiFi/IP, web login, display pairing, MQTT, status LED
and backup settings.

For Home Assistant:

1. Enable MQTT and enter broker host, port and credentials.
2. Enable TLS when required and paste the broker CA certificate.
3. Set the base topic and Discovery prefix.
4. Save the settings and wait for Discovery reconciliation.

The master publishes itself and every module as a separate device. Names,
language, dynamic profile entities and alarms follow the master. When MQTT is
disabled, availability is published before disconnecting so entities become
unavailable rather than remaining stale.

## Pair displays

Displays can use RS485, authenticated WiFi or automatic failover between both.
Pairing codes are generated for a specific display serial number under
**Network Setup**. Follow [DISPLAYS.md](DISPLAYS.md) for wired and wireless
commissioning.

## Firmware updates

The **Updates** page reads the target and version embedded in a selected file
before flashing. The target must match the master or selected module, and the
Ed25519 signature must be valid.

- Master OTA updates reboot the master when complete.
- Module OTA updates are forwarded over RS485 or the authenticated display WiFi
  tunnel where supported.
- Cancel an update with the page's abort control and wait for the target to
  leave firmware-busy state.
- Use the developer update path only on private development builds.

## Backup and restore

The JSON backup includes WiFi, web login, MQTT credentials and CA certificate,
routing, extraction settings, LED settings and Boolean definitions. It contains
sensitive information and should be stored accordingly.

A backup is applied only by an explicit restore. After restoring, verify WiFi,
main routing, extraction settings, rules, MQTT and display pairing.

## Status LEDs and alarms

The master is the authoritative alarm source for web UI, displays and MQTT.
Yellow indicates a warning, while red indicates a critical fault. Status LED
brightness can be changed under **Network Setup > Status LEDs**.

See [OPERATION.md](OPERATION.md) for LED effects and alarm behavior.

## Serial CLI

Connect at 115200 baud. Useful commands include:

```text
help
status
modules
module 0x50
routes
bus
network
mqtt
scan
monitor on
monitor off
webauth reset
network reset
restart
```

Use `network reset` only when a new first-time setup is intended. The master
reboots into setup mode after clearing network configuration.

## Commissioning checklist

- all expected modules are online at unique addresses
- main input and main output are selected
- extraction starts, stops and enters after-run correctly
- output feedback, RPM and alarms are plausible
- Additional Rules and Boolean definitions have been tested
- both display transports behave as intended
- Home Assistant entities are available and correctly named
- all settings survive a restart
- a current backup has been downloaded

