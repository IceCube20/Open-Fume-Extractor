# FAE Identity Commands

The JBC-facing identity uses the one-workplace F1 variant exposed by the
reconstructed FAE2 firmware builder. Work and Stand remain available.
It is separate from the OFE module firmware version and does not restrict the
number of JBC stations that the OFE master can combine for one extractor.

| Command | Response |
| --- | --- |
| `0x21` | `02:F2:8886791:0023324:F1_DIS_02`, exactly 31 ASCII bytes, no NUL terminator |
| `0x1C` | Eight binary bytes derived from this board's eFuse identity, most-significant byte first |
| `0x1E` | The currently stored device ID, using its actual byte length |
| `0x1F` | Store a new text ID; successful reply uses command `0x1F` and payload `06` |

The hardware ID is the emulator board's own identity, not a claimed original JBC
hardware-register value. FID, source/destination addressing, BCC and DLE escaping
remain part of the surrounding P02 frame.

## Stored Device ID

Each module has one emulated JBC UART bus and one persistent ID in its existing
`jbc-bus/devid` NVS entry. Changing the OFE module address does not change that ID.
Existing stored IDs are retained, including legacy payloads up to 64 bytes.
A failed read does not intentionally replace the stored entry.

Only a module without a stored ID receives a newly generated 32-character
uppercase hexadecimal text ID. Its prefix includes the board identity and its
suffix uses random values. It is saved once, is not a security token, and is
transmitted as ASCII rather than decoded into 16 binary bytes.

New writes accept 1-32 printable ASCII bytes, without a terminator or embedded
NUL. RAM is updated and an ACK is sent only after NVS reports a successful write.
Invalid data or failed persistence receives a NACK, preserving the active ID.
An initial storage failure is reported on the serial console; a generated ID
cannot be guaranteed to survive a restart until it has been stored successfully.

## Firmware Fields

`02:F2:8886791:0023324:F1_DIS_02` is
`protocol:family:software:hardware:model`.
The final field separates into model `F1`, model type `DIS`, model version `2`.
The family field stays `F2`, as it does in the reconstructed builder's F1 branch.
This is not a claim that a physical FAE1 uses the same software/hardware codes.
Selecting this model does not change the stored device ID or hardware ID.
The expansion of `DIS` is not confirmed. JBC Connect uses `DIS` with model version
at least 2 to enable `SwitchablePorts`; this does not establish an identical
feature table in every DME firmware.

This format follows the supplied static analysis of FAE2 firmware 8886791 and
JBC Connect, reviewed on 2026-11-07. It is not a new physical-FAE capture or proof
of complete DME compatibility. The existing `0x1D` discovery behavior is left
unchanged because its exact response contract was not established by that work.
