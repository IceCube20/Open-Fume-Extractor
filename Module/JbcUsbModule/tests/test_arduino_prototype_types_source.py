from pathlib import Path

src = Path(__file__).resolve().parents[1] / "JbcUsbModule.ino"
text = src.read_text(encoding="utf-8")

# Arduino's .ino preprocessor emits prototypes ahead of the sketch body. Any
# local types used by generated prototypes must therefore be visible before the
# first include / prototype insertion point.
first_include = text.index("#include <Arduino.h>")
for decl in (
    "enum JbcProtocol : unsigned char;",
    "enum JbcStationKind : unsigned char;",
    "enum JbcLinkState : unsigned char;",
    "enum JbcUidProvisionState : unsigned char;",
    "enum RxState : unsigned char;",
    "enum UsbSerialOpenResult : unsigned char;",
    "struct JbcFrame;",
    "struct JbcModelInfo;",
):
    assert decl in text
    assert text.index(decl) < first_include

# Definitions retain their fixed uint8_t ABI later in the sketch.
assert "enum JbcProtocol : uint8_t {" in text
assert "enum JbcStationKind : uint8_t {" in text
assert "enum JbcLinkState : uint8_t {" in text
assert "enum JbcUidProvisionState : uint8_t {" in text
assert "enum RxState : uint8_t {" in text
assert "enum UsbSerialOpenResult : uint8_t {" in text

# The send/single-flight helpers still use the same typed API.
assert "static bool jbc_single_flight_ready(JbcProtocol frame_protocol" in text
assert "static bool jbc_send_frame(JbcProtocol frame_protocol" in text
assert "#define OFE_MODULE_FW_PATCH 75" in text
