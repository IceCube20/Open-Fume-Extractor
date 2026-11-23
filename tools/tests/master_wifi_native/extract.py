"""Extract the actual Master WiFi lifecycle, without HTTP route registration."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
source = (root / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebServer.inc.h").read_text(encoding="utf-8-sig")


def function(name):
    start = source.index(f"static void {name}(")
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


Path(sys.argv[1]).write_text("\n\n".join(function(name) for name in (
    "master_time_service_tick", "wifi_start_setup_ap", "wifi_start_sta_connect", "wifi_pause_connection_for_scan", "wifi_on_sta_connected", "wifi_service_tick")), encoding="utf-8")
