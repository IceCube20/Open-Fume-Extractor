# Open Fume Extractor 2.0.51 RC

Community release with signed OTA packages, complete merged USB images and
the native Home Assistant integration **0.1.0rc19**. Developer mode and private
credentials are not enabled in the published master. Signing keys stay private.

## Changes since 2.0.30 RC

- Direct local Home Assistant integration without an MQTT broker. MQTT remains
  available in parallel. Module controls, entity icons, numeric formatting,
  availability and dynamic entity cleanup have been aligned between both paths.
- Dashboard cards with module-specific icons, OFE/EVT indicators and alarm
  lists; configurable native polling from 1 to 60 seconds.
- MQTT publishing avoids repeatedly allocating descriptor metadata and skips
  unchanged availability publications. Network scanning is deferred to improve
  Network/System page responsiveness.
- Improved timeout diagnostics, including malformed response length/header
  details and master receive-service gaps. Transport errors are still reported;
  the update/restart window does not globally suppress bus diagnostics.
- Power-saving state is synchronized again when a module restarts.
- Weller cached filter readings remain visible when its local device is
  disconnected. Freshness checks keep stale status out of active fault handling.
- Weller filter interval can be turned Off in web, MQTT, native Home Assistant
  and all displays. Off sends `f000` then `g000` and resets operating time;
  positive intervals re-enable the timer. Reported filter condition is separate.
- All display interval lists start with Off/Aus and use the selected language.

## Updated firmware

| Target | Version |
|---|---|
| Community master | 2.0.51RC |
| Weller Zero Smog | 1.1.83RC |
| AXS15231B Display 320x480 | 1.4.3RC |
| RGB Display 800x480 | 1.4.10RC |
| ST7796 Display 320x480 | 1.0.13RC |

Other module versions are listed in [Firmware Files](https://github.com/IceCube20/Open-Fume-Extractor/blob/v2.0.51RC/docs/FIRMWARE.md).

## Install or update

All eleven targets have individual signed `.bin` OTA packages and complete
`*-merged.bin` USB images attached to this release. Never upload a merged image
through OTA; use it only for first installation at address `0x0`. The
[online USB flasher](https://icecube20.github.io/Open-Fume-Extractor/) uses the
same merged images. Choose the exact display hardware target.

For Home Assistant, install the attached `open-fume-extractor-ha-0.1.0rc19.zip`
following [the integration guide](https://github.com/IceCube20/Open-Fume-Extractor/blob/v2.0.51RC/HomeAssistant/README.md). Existing users
should replace the component folder and restart HA, not remove the integration.
The dashboard JavaScript resource is
`/open_fume_extractor/ofe-cards.js?v=0.1.0rc19`.

## Verification and limitations

Firmware builds and source/native regression tests passed. Firmware signatures
and merged-image app contents are checked before publication. Hardware tests
have shown substantially improved normal polling latency, but this is not a
guarantee of fault-free operation with every wiring layout or workload.

Occasional WLAN latency and malformed RS485 responses remain diagnosable. The
Weller `r001`/`r000` commands are not used because their purpose is unconfirmed.
The native HA integration is a community custom integration, not a claim of
Home Assistant certification or complete parity with every web editor.
