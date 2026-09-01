# JBC_Connect interoperability reference

This note records behavior verified against the decompiled `JBC_Connect.dll` project supplied in the test archive. It is a behavioral interoperability map; it does not reproduce the proprietary implementation.

## USB serial transport

- CP210x UART: **500000 baud, 8 data bits, even parity, 1 stop bit**.
- P01 logical frame: `STX SRC DST CMD LEN DATA BCC ETX`.
- P02 logical frame: `STX SRC DST FID CMD LEN DATA BCC ETX`.
- On the wire, frames use `DLE STX ... DLE ETX`; a literal `DLE` in the body is doubled.
- BCC is XOR based so XOR of the logical bytes from STX through ETX, including BCC, is zero.
- Response frames set bit 7 of the source address.

## Discovery / handshake

### Protocol 01

The original flow is passive: wait for `NAK (0x15)`, send `SYN (0x16)`, wait for ACK, send ACK, receive the one-byte PC/source address, ACK it, then query firmware using command `0x21`.

### Protocol 02

The normal flow accepts the station-initiated handshake (`CMD 0x00`, FID `0xFD`) and answers it before reading firmware (`FID 0xED`). An active compatibility probe uses source `0x19`, target `0x10`, FID `0xFD`, command `0x00`, data `0x10`.

## SOLD status: the important 0.1.24+ correction

The DLL has **three distinct QST/port concepts** that must not be merged:

1. **Station QST settings**
   - P02: `QSTActivate 0x9C`, `QSTStatus 0x9E`
   - P01: `QSTActivate 0xD0`, `QSTStatus 0xD2`
2. **EnabledPort / port lock state**
   - P02 `M_INF_PORT (0x30)`: for QST-capable SOLD stations, status bit 5 is inverted: `0 = EnabledPort ON`, `1 = EnabledPort OFF`.
   - P02 `ReadLockPort (0x88)` also updates `EnabledPort`; despite the API name, it is not `QSTLock`.
   - P01 `ReadLockPort (0xD4)` is likewise part of the EnabledPort path.
3. **Tool QSTLock / ActiveCleaning**
   - P02 k20/k26 `M_R_TOOLLASTSTATE (0x57)` request payload is one byte: `{port}`.
   - Reply payload is two bytes: `{ToolStatus, port}`.
   - ToolStatus bits: bit0 Stand, bit1 Sleep, bit2 Hibernation, bit3 Extractor, bit4 Desolder, **bit5 QSTLock**, **bit6 ActiveCleaning**.

Therefore P02 `M_INF_PORT` bit5 must never be displayed as QSTLock.

## OFE mapping in modules 0.1.24+

- `status_flags` remains the normalized per-port live state (Stand/Sleep/Hibernation/Extractor/Desolder/etc.).
- `detail_flags` for P02 SOLD now contains the real `0x57` ToolLastStatus byte, not the `0x30` InfoPort byte.
- Existing `detail_value_flags` is reused without increasing the telemetry record size:
  - bit10 (`0x0400`): ToolLastStatus/`0x57` valid
  - bit11 (`0x0800`): EnabledPort valid
  - bit12 (`0x1000`): EnabledPort ON
- `0x57` is polled independently of the slow counters/settings round-robin so QSTLock and ActiveCleaning remain responsive.
- P01 status behavior remains separate; P01 `0x57` does not use the P02 k20/k26 QSTLock/ActiveCleaning layout.


## ReadLockPort fidelity added in module 0.1.25

`UpdateData_SOLD.UpdatePortStatus()` explicitly reads the port-lock path for every QST-capable SOLD port. The OFE module now mirrors this read-only behavior:

- P02 `ReadLockPort`: command `0x88`, request `{port}`, reply length 2. `data[0] == 0` means `EnabledPort ON`; non-zero means OFF.
- P01 `ReadLockPort`: command `0xD4`, request `{port}`, reply length 2. The original receiver ignores the returned value and derives `EnabledPort` from station `QSTActivate` and `QSTStatus`; OFE mirrors this legacy behavior only once both values are known.
- This path updates only `EnabledPort`. It does not overwrite `QSTLock` from P02 ToolLastStatus `0x57`.

## JBC continuous mode is not OFE continuous suction

The JBC API uses three commands for its high-rate measurement stream:

- `M_R_CONTIMODE = 0x80`: read stream configuration. P01 returns 2 bytes; P02 accepts 2 or 3. The first two bytes are `{speed, enabledPortsMask}`.
- `M_W_CONTIMODE = 0x81`: configure/start/stop the JBC telemetry stream. `JBCStationsData.dll` defines `OFF=0` and `T_10mS=1`. `CStation_SOLD.StartContinuousMode()` starts `{T_10mS, allStationPorts}` only when the current stream is OFF. P02 ALE uses the three-byte extended start payload `{speed, ports, 1}`.
- `M_I_CONTIMODE = 0x82`: unsolicited/high-rate continuous measurement data.

Module 0.1.27 mirrors that startup rule for SOLD. OFE first reads `0x80`; if the station reports OFF it sends `0x81` with `T_10mS` and the station port mask, then verifies with `0x80`. If a stream is already active, OFE leaves its speed/port mask untouched. This JBC stream remains completely separate from `FAST_FLAG_CONTINUOUS`, which is an OpenFumeExtractor suction command.

For normal non-ALE P02 SOLD stations, `0x82` is `sequence + 10 bytes` per enabled port: `TempA, TempB, PowerA, PowerB, Status, Extra`. PA (`tool=3`) and HT (`tool=4`) average A/B temperature and power like the DLL; other tools use channel A. P01 uses `sequence + 9 bytes` per enabled port.

OFE deliberately keeps `M_INF_PORT (0x30)` running in parallel. The two sources merge into one `JbcPortState`: continuous frames are preferred while fresh for the overlapping live values (temperature, power and live SOLD state), while `M_INF_PORT` remains authoritative for ConnectedTool, ToolError, EnabledPort, change notifications and other detail fields. If continuous frames stop for more than 750 ms, the next `M_INF_PORT` reply automatically becomes the live fallback. P02 continuous status bit5 is retained only as raw diagnostic data; it is not aliased to Stand or QSTLock. EnabledPort comes from `M_INF_PORT/0x88`, and true QSTLock comes from `0x57`.

## Feature/model table additions in module 0.1.25

Direct comparison with `CFeaturesDataInitializer` added two models that were absent from the local table:

- `CFE`: SOLD, 1 port, QST enabled; firmware/model version 7+ uses the k26 protocol path just like CDE.
- `PHXL`: PH/preheater, 1 port.

## Original classes used for verification

The supplied decompiled project was cross-checked primarily against:

- `Helpers/IO/SerialPortConfig.cs`
- protocol frame encoder/decoder and USB physical transport classes
- `Modules/SearchJBC/CSearchStationsUSB*`
- `StationsJBC/Frames/SendFrame02_k20_SOLD.cs`
- `StationsJBC/Frames/SendFrame02_k26_SOLD.cs`
- `StationsJBC/Frames/ReceiveFrame02_SOLD.cs`
- `StationsJBC/Frames/ReceiveFrame02_k20_SOLD.cs`
- `StationsJBC/Frames/ReceiveFrame02_k26_SOLD.cs`
- `DataJBC/CFeaturesDataInitializer.cs`
- `StationsJBC/CStation_SOLD.cs`

The goal is behavioral compatibility with the original host software, not binary/API cloning of the DLL.

## SOLD monitoring completion in module 0.1.26 / master 1.8.37

The remaining read-only SOLD monitoring paths were compared directly with `UpdateData_SOLD`, `CStation_SOLD`, and the P01/P02 SOLD frame classes. Hot-air behavior is intentionally unchanged in this revision.

### Normal SOLD status/update reads mirrored by OFE

- MOS temperature per port:
  - P01 `0x58`, request `{port}`, 2-byte little-endian temperature reply.
  - P02 `0x59`, request `{port}`, temperature plus port context.
- Transformer temperature: `0xAF`, no request payload, 2-byte little-endian temperature reply.
- Station connect mode/status:
  - P01 `0x1E`.
  - P02 USB `0xE0`; the original host interprets a `C` response as CONTROL and otherwise as MONITOR. OFE accepts the textual USB response robustly, including whitespace after a colon.
- P01/P02 station error polling remains separate from these diagnostics, matching the original updater.

### Slow, non-destructive SOLD diagnostic reads

For P02 SOLD, OFE additionally mirrors safe public diagnostic reads at a deliberately slow rate:

- `0x55` ToolType, request `{port}`.
- `0x56` ToolLastError, request `{port}`.
- `0x83` maximum temperature alarm configuration, reply `[temp i16 LE][delay i16 LE][port]`.
- `0x85` minimum temperature alarm configuration, same layout.
- `0xB7` transformer temperature-error threshold/trigger value.
- `0xB8` MOS temperature-error threshold/trigger value.

These reads are diagnostic enrichment; unlike MOS/transformer temperature and connection state, they are not all part of every normal `UpdateData_SOLD` cycle in the DLL. Unsupported stations can simply NAK/ignore them and the associated validity bits remain clear.

### Destructive alarm read is intentionally excluded

Command `0x87` is named `M_R_ALARMTEMPSET_NCLEAR` / read-and-clear temperature alarm triggered in the original code. OFE defines the command for interoperability documentation but **never places it in the automatic poll list**. Automatically issuing it would clear station alarm history merely by observing the station.

### D8 SOLD diagnostic telemetry extension

Master 1.8.37 understands an optional 24-byte `0xD8`/v1 SOLD diagnostic block containing the per-port MOS/tool/alarm values plus station transformer/connect/temperature-error values. The module alternates the existing `D7` SOLD detail record and the new `D8` record for the same port before advancing to the next port.

This choice keeps the RS485 payload below the existing maximum even with a 32-byte JBC UUID and a station-name `D6` suffix. Older masters continue to receive and parse regular `D7` records; they ignore `D8`. No HOT_AIR record layout is changed.

## SOLD verification status

As of 0.1.27, the SOLD **monitoring/live path** is behaviorally cross-checked against the supplied decompiled `JBC_Connect` project, including P01/P02 framing, handshake, InfoPort, ToolLastStatus, QST/EnabledPort, continuous-mode start/read/merge, MOS/transformer temperatures, connection state, and the safe k20/k26 diagnostic reads above.

This is **DLL-verified, not yet fully hardware-verified**. The next validation stage should use a real DDE to capture and compare raw requests/replies and confirm model/firmware-specific response details. General station write/configuration APIs and the destructive `0x87` read-clear operation remain outside the automatic monitoring path; the sole intentional write added here is the original JBC `0x81` continuous-telemetry start when the stream is OFF.

