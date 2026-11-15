# MCU 2.1.8: Active Network In Home Assistant

- MQTT publishes `network_transport` using the same active-interface helper as
  the native `/state` API. Ethernet has priority only when linked with an IP;
  otherwise the value follows WiFi, access point or offline status.
- MQTT discovery and native HA 0.1.31rc3 add the Network diagnostic sensor
  (`Netzwerk` in German). States and the `mdi:lan` icon match in both paths.
- Native entity attributes and OFE card badges now follow Ethernet/WiFi instead
  of hard-coding WiFi for every MCU. Existing entity identifiers are unchanged.
- The native sensor works with the existing network-aware firmware API. MQTT
  needs the newly compiled MCU 2.1.8 firmware and enabled HA discovery.

No display update, firmware flash or GitHub publication is performed here.
The P4 remains an RC; the existing S3 build suffix is unchanged.
