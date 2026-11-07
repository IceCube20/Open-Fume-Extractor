import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
source = (root / "Module/JbcBusModule/JbcBusModule.ino").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"^static [^\n]*\b" + name + r"\([^;]*?\)\s*\{", source, re.M)
    if not match:
        raise RuntimeError("Missing production function: " + name)
    depth = 0
    for i in range(source.index("{", match.start()), len(source)):
        depth += (source[i] == "{") - (source[i] == "}")
        if depth == 0:
            return source[match.start():i + 1]
    raise RuntimeError("Unclosed production function: " + name)


parts = [re.search(r"namespace " + name + r" \{[\s\S]*?\n\}", source).group()
         for name in ("jbc_base", "jbc_fe")]
parts += [re.search(r"^static const (?:uint8_t|size_t) " + name + r" = [^;]+;", source, re.M).group()
          for name in ("DLE", "STX", "ETX", "P02_MAX")]
parts += [function(name) for name in ("bcc_calc_xor", "stuff_bytes", "send_p02", "send_firmware",
                                     "send_device_id", "send_read_reply", "handle_identity_command")]
assert "if (n < 5 || n != size_t(5 + f[4])) return;" in function("handle_p02_frame")
assert "handle_identity_command(reply_src_addr, reply_to, fid, ctrl, data, len)" in function("handle_p02_frame")
Path(sys.argv[1]).write_text("\n\n".join(parts), encoding="utf-8")
