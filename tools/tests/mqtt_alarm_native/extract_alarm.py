"""Extract actual MQTT alarm serialization and shared JSON escaping."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3] / "OpenFumeExtractorMaster/src"


def function(source, name):
    start = re.search(r"^static String " + name + r"\([^\n]*\) \{", source, re.M).start()
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


mqtt = (root / "MasterMqtt.inc.h").read_text(encoding="utf-8")
web = (root / "WebStatus.inc.h").read_text(encoding="utf-8")
Path(sys.argv[1]).write_text("\n\n".join((function(web, "json_escape"), function(mqtt, "mqtt_alarm_text_json"))),
                             encoding="utf-8")
