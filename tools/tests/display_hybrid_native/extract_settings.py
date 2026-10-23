"""Extract the unchanged sketch callback dependencies for layout host tests."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[3]
source = (root / "Module/DisplayModule_320x480/DisplayModule_320x480.ino").read_text(encoding="utf-8")
match = re.search(r"static void lv_create_backlight_controls\([^;]*?\) \{", source)
end, depth = match.end(), 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
pathlib.Path(sys.argv[1]).write_text(source[match.start():end], encoding="utf-8")
