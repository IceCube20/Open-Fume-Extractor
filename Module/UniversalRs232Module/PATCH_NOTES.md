# UniversalRs232Module 1.0.11beta

Descriptor robustness:
- Descriptor now contains only profile-created entities plus a short header.
- Fixed system/debug entities are no longer duplicated into the descriptor; the Master adds those as built-in definitions.
- This greatly reduces CMD_DESCRIPTOR_GET size and makes larger Community profiles more reliable.


## 1.0.12beta
- Profil-Entities melden im Descriptor jetzt explizit `source=profile group=control ui=control`.
- Das macht die UI-Zuordnung im Master eindeutiger.

## 1.0.65 alpha
- Added generic `protocol=BINARY` runtime.
- Added IDLE/FIXED/length-field framing and optional sync prefix.
- Added binary field decoding, typed binary TX placeholders and raw checksum modes.
- Existing ASCII/Weller profiles remain compatible.

## JBC separation
- Removed the `JBC_DLE_BCC` placeholder from UniversalRs232Module.
- Removed JBC checksum/protocol aliases from the Universal parser.
- Removed `JBC_DLE_BCC` from the Master Universal Profile Builder and validation.
- JBC remains handled exclusively by the dedicated `JbcBusModule`, which implements the required handshake/session/DLE framing behavior.
