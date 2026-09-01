# JBC USB protocol notes for OpenFumeExtractor

This module implements the JBC USB station link reconstructed from the supplied decompiled `JBC_Connect.dll`. It is deliberately separate from `JbcBusModule`, which remains responsible for the JBC FAE/Robot bus.

## 1. Transport

- ESP32-S3 native USB-OTG in **host** mode.
- JBC USB bridge: **Silicon Labs CP210x**.
- Accepted V1 VID/PIDs: `10C4:EA60`, `10C4:EA70`, `10C4:EA71`.
- CP210x UART configuration used by JBC Connect: **500000 baud, 8 data bits, even parity, 1 stop bit, no flow control**.
- USB host hardware must provide protected/current-limited 5 V VBUS.

The CP210x layer only transports bytes. P01/P02 framing, handshakes and station commands are implemented above it.

## 2. Common DLE framing and BCC

Both P01 and P02 use DLE/STX and DLE/ETX on the wire.

```text
DLE STX  [frame body + BCC with DLE stuffing]  DLE ETX
10  02                                            10  03
```

Every `0x10` inside the stuffed region is doubled:

```text
10 -> 10 10
```

The logical unescaped frame includes `STX` and `ETX`. BCC is XOR, and a valid received logical frame satisfies:

```text
XOR(STX ... DATA ... BCC ETX) == 0
```

## 3. Protocol 01

Logical frame:

```text
STX SRC DST CMD LEN DATA... BCC ETX
02  ..  ..  ..  ..  ....    ..  03
```

P01 has **no FID**, therefore the module permits only one outstanding P01 request at a time.

### P01 discovery/handshake

The original JBC USB search performs this raw-byte sequence before normal frames start:

```text
Station -> Host   NAK  0x15
Host    -> Station SYN 0x16
Station -> Host   ACK  0x06
Host    -> Station ACK 0x06
Station -> Host   <PC/source device byte>
Host    -> Station ACK 0x06
Host    -> Station M_FIRMWARE (0x21) as a P01 frame, target 0
```

The station-supplied byte becomes the host/source device number; the P01 station target is `0`.

Example P01 firmware request with host/source `0x19`:

```text
logical: 02 19 00 21 00 39 03
wire:    10 02 19 00 21 00 39 10 03
```

## 4. Protocol 02

Logical frame:

```text
STX SRC DST FID CMD LEN DATA... BCC ETX
02  ..  ..  ..  ..  ..  ....    ..  03
```

FID allows multiple requests to be correlated independently.

### Active P02 handshake

JBC Connect sends:

```text
Source      0x19
Destination 0x10
FID         0xFD
Command     0x00 (M_HS)
Length      0x01
Data        0x10
```

Known vector:

```text
logical: 02 19 10 FD 00 01 10 E4 03
wire:    10 02 19 10 10 FD 00 01 10 10 E4 10 03
```

The station answers with `M_HS`, FID `0xFD`, data `0x06` (ACK). The response's source/target fields establish station and host addresses. The module then requests `M_FIRMWARE (0x21)` with FID `0xED`.

### Passive P02 handshake

Some stations initiate a valid P02 HS frame themselves. The module accepts that form, learns the addresses, returns a response HS frame containing ACK, and then requests firmware/model information.

## 5. Automatic protocol detection

Connection state is intentionally dual-protocol from the start:

```text
CP210x connected
      |
      v
  DETECTING
   |     |
   |     +-- valid P02 HS / P02 active probe reply --> P02
   |
   +-------- raw NAK 0x15 --------------------------> P01
```

A short passive window is given to old P01 stations before periodic P02 handshake probes are sent. A later valid P01 NAK still switches the state machine to P01.

The module tracks **two** protocol values:

- `frame_protocol`: actual P01/P02 wire layout.
- `command_protocol`: value reported by the station in the firmware response.

JBC Connect keeps these separate, so OpenFume does too. The Web UI displays the reported command protocol as `Protocol 01 (P01)` or `Protocol 02 (P02)` and exposes the frame protocol for diagnostics.

## 6. Firmware/model identification

After either handshake, command `0x21` returns a colon-separated UTF-8 string. JBC Connect interprets it as:

```text
Protocol : Model : SoftwareVersion : HardwareVersion [: optional]
```

The model may contain:

```text
MODEL_ModelType_ModelVersion
```

OpenFume preserves and displays the **raw station-provided model string**. It additionally parses base model, model type and numeric model version. Strings containing `CDB` are normalized to base model `CD/CF`, matching JBC Connect behavior. If a five-field response has a fifth field other than `B`, JBC Connect treats that fifth field as an alternate model string; OpenFume follows that behavior.

Unknown/new models are **never rejected** merely because they are absent from the local table.

## 7. JBC model -> port count table

The supplied JBC Connect library contains this station configuration table:

| Model | Ports | Model | Ports | Model | Ports |
|---|---:|---|---:|---|---:|
| CA | 1 | CD/CF | 1 | CDN | 1 |
| CP | 1 | CS/CV | 1 | CDE | 1 |
| CAE | 1 | CPE | 1 | CSVE | 1 |
| DD | 2 | DDE | 2 | DDR | 2 |
| DI | 1 | DM | 4 | DME | 4 |
| HD | 1 | HDE | 1 | HDR | 1 |
| LC | 1 | NA | 2 | NAE | 2 |
| PSE | 4 | SM | 1 | WS | 1 |
| ALE | 1 | JT | 1 | JTSE | 1 |
| SF | 1 | F1 | 1 | F2W | 2 |
| F2 | 2 | F4W | 4 | PH | 1 |
| PHBE | 1 | PHNE | 1 | PHSE | 1 |
| CLM | 1 | | | | |

For a recognized base model, this is the displayed port count. For an unknown model, the raw model remains visible and the module safely probes ports 0..3; the highest responsive port extends the detected count.

## 8. Port status (`M_INF_PORT`, command `0x30`)

Requests use one data byte containing the port index.

Common fields decoded for both command generations:

| Offset | Meaning |
|---:|---|
| 0 | connected tool code (`0` = no tool for OFE work logic) |
| 1 | tool error |
| 2..3 | actual temperature, `uint16` little-endian, kept as JBC raw UTI |
| 6..7 | `Power_x_Mil`, `uint16` little-endian |

### P02 status

Normal SOLD responses are 12 or 14 bytes; ALE uses 15 bytes and carries the port number in `DATA[14]`.

`DATA[10]`:

```text
bit 0 Stand
bit 1 Sleep
bit 2 Hibernation
bit 3 Extractor
bit 4 Desolder
```

### P01 status

SOLD responses are 12 or 15 bytes.

`DATA[10]`:

```text
bit 0 Sleep
bit 1 Hibernation
bit 2 Extractor
bit 3 Desolder
```

P01 has no dedicated Stand bit. In the 15-byte form JBC Connect also reads:

```text
DATA[12..13] TimeToSleepHibern, uint16 LE
DATA[14]     FutureMode_Tool
```

OpenFume marks the stand phase when that countdown is active while Sleep and Hibernation are not yet active. The raw future-mode byte is retained for diagnostics.

### OpenFume work/stand masks

For each port:

```text
PARKED = Stand || Sleep || Hibernation
WORK   = tool != 0 && !PARKED
```

Ports map to bits 0..3 in the OFE `work_mask` and `stand_mask`. Stale port data ages out of the activity mask.

## 9. Web/master state

The JBC USB module sends the master:

- USB/JBC link state
- CP210x VID/PID
- detected frame protocol
- reported command protocol / raw protocol string
- station and host JBC addresses
- raw station model string
- parsed model/model type/model version
- JBC software and hardware versions
- port count and whether it came from the JBC model table
- up to four independent port states
- Work/Stand masks

The OpenFume web page therefore shows which physical station is connected, whether it is P01 or P02, how many ports it has, and the state of every port.

## 10. Read-only V1 scope

Implemented:

- ESP32-S3 USB host and CP210x transport.
- 500000/8E1 configuration.
- P01 and P02 DLE codec/BCC validation.
- P01 raw handshake.
- P02 active and passive handshake.
- Automatic P01/P02 detection.
- Firmware/model/protocol discovery.
- Complete known-model port-count table plus unknown-model probing.
- `M_INF_PORT` polling and per-port status for both command generations.
- OFE Work/Stand input routing, logic-editor input, web status and diagnostics.
- Dedicated `MODULE_JBC_USB` / `CAP_JBC_USB` identity and firmware signature.

Deliberately not enabled yet:

- Writing station settings (apart from the DLL-faithful SOLD `0x81` telemetry-stream start when `0x80` reports OFF).
- Firmware/bootloader programming of the JBC station.
- Guessing undocumented model-specific write commands.

This keeps station configuration read-only apart from mandatory handshake/UUID behavior and the non-setting SOLD continuous-telemetry start used by the original JBC API.

## 11. OFE bus separation

- `JbcBusModule` remains `MODULE_JBC_BUS` / `CAP_JBC_BUS` for JBC FAE/Robot communication.
- `JbcUsbModule` is `MODULE_JBC_USB` / `CAP_JBC_USB` for JBC station USB monitoring.
- `CAP_JBC_ACTIVITY = CAP_JBC_BUS | CAP_JBC_USB` is used wherever either module may legitimately be an OpenFume Work/Stand input.
- FAE configuration, filter/error writes and FAE station controls remain restricted to `CAP_JBC_BUS` only.

## 12. First hardware bring-up

1. Flash `JbcUsbModule` to an ESP32-S3 via a separate debug/programming path.
2. Confirm the OFE master discovers it as **JBC USB**, normally at `0x11`.
3. Verify protected 5 V USB VBUS and native D-/D+ wiring.
4. Connect a JBC station and confirm CP210x VID/PID appears in the web page.
5. With an older P01 station, verify `NAK -> SYN -> ACK -> ACK -> address -> ACK`, then firmware/model discovery.
6. With a P02 station, verify the known active handshake vector and firmware/model discovery.
7. Confirm the Web UI displays `Protocol 01 (P01)` or `Protocol 02 (P02)` correctly.
8. Confirm the exact model string and expected 1/2/4 port count.
9. Move each tool through Work/Stand/Sleep/Hibernation and compare the individual port rows and aggregate OFE trigger.

`tests/test_protocol.py` validates codec vectors and the known model/port table without Arduino hardware.
