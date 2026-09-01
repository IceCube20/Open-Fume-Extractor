from pathlib import Path

src = (Path(__file__).resolve().parents[1] / "JbcUsbModule.ino").read_text(encoding="utf-8")

assert "#define OFE_MODULE_FW_PATCH 75" in src

# EncodeFrame.cs: Protocol 01 = 7 control bytes, Protocol 02 = 8, one-byte
# LENGTH allows 0..255 data bytes.
assert "JBC_MAX_FRAME_DATA = 255U" in src
assert "JBC_MAX_LOGICAL_FRAME = 8U + JBC_MAX_FRAME_DATA" in src
assert "uint8_t data[JBC_MAX_FRAME_DATA]" in src
assert "uint8_t rx_logical[JBC_MAX_LOGICAL_FRAME]" in src
assert "len > 192" not in src

# Exact DLL wire semantics: response toggles source bit7, target is preserved,
# DLE stuffing is applied to the logical body, BCC XOR includes ETX.
wire_decl = src.index("static bool jbc_write_frame_wire")
wire_start = src.index("static bool jbc_write_frame_wire", wire_decl + 1)
wire = src[wire_start:src.index("static bool jbc_retry_timed_out_request", wire_start)]
assert "if (response) source_device ^= 0x80U;" in wire
assert "logical[n++] = target;" in wire
assert "uint8_t bcc = JBC_ETX;" in wire
assert "if (logical[i] == JBC_DLE) wire[w++] = JBC_DLE;" in wire
assert "wire[w++] = JBC_DLE;" in wire and "wire[w++] = JBC_ETX;" in wire

# Station_Com runtime FID allocator is exactly 1..239; FD/ED are fixed
# discovery FIDs and ED is not excluded from the normal 1..239 range.
assert "JBC_MAX_RUNTIME_FID = 239" in src
fid = src[src.index("static uint8_t next_fid()"):src.index("static bool jbc_send_raw_byte", src.index("static uint8_t next_fid()"))]
assert "if (jbc_next_fid >= JBC_MAX_RUNTIME_FID) jbc_next_fid = 0;" in fid
assert "return ++jbc_next_fid;" in fid
assert "JBC_HS_FID" not in fid and "JBC_FW_FID" not in fid

# DecodeFrame state machine and length checks mirror EncodeFrame.CheckFrame.
assert "(size_t)rx_logical[4] + 7U == rx_logical_len" in src
assert "(size_t)rx_logical[5] + 8U == rx_logical_len" in src
assert "for (size_t i = 0; i < rx_logical_len; ++i) x ^= rx_logical[i];" in src
assert "else if (b == JBC_STX)" in src
assert "else if (b == JBC_ETX)" in src

# QueueMessages/Station_Com fidelity around frames: 500 ms, initial+4 retries,
# connection restart after exhaustion, and >3 NACKs/s connection error.
assert "JBC_SINGLE_FLIGHT_TIMEOUT_MS = 500UL" in src
assert "JBC_MESSAGE_RETRY_COUNT = 4" in src
assert "restart_jbc_discovery(true, true);" in src
assert "return kept > 3;" in src
assert "f.len == 5 && f.data[0] != 1" in src

# P01 raw discovery and P02 FD handshake are both present.
for token in ("JBC_NAK", "JBC_SYN", "JBC_ACK", "JBC_HS_FID = 0xFD", "JBC_FW_FID = 0xED"):
    assert token in src
assert "JBC_LINK_P01_WAIT_ACK" in src and "JBC_LINK_P01_WAIT_ADDR" in src
assert "f.fid == JBC_HS_FID" in src

print("JBC P01/P02 full frame-protocol fidelity source audit: OK")
