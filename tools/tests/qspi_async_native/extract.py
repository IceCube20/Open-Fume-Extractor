import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
source = (root / "Module/DisplayModule_320x480/DisplayModule_320x480.ino").read_text(encoding="utf-8-sig")
start = source.index("class OfeESP32QSPI final")
end = source.index("\n};", start) + 4
Path(sys.argv[1]).write_text(source[start:end], encoding="utf-8")
functions = []
for signature in ("static bool qspi_transmit_full_frame(", "static bool qspi_idf_send_full_frame()",
                  "static void qspi_collect_full_frame(", "static void lvgl_flush_canvas_if_dirty(",
                  "static void qspi_mode_command("):
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    functions.append(source[start:end])
Path(sys.argv[2]).write_text("\n".join(functions), encoding="utf-8")
