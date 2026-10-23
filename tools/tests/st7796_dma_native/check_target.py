"""Check both ST7796 transport branches using the owned target compiler recipe."""
import json
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / "tools/tests"))
from check_display_board_syntax import current_source, CACHE

build = root / "tools/tests/build-checks/display-board-syntax/DisplayModule_ST7796_320x480"
recipe = json.loads((build / "compile_commands.json").read_text())[0]
folder = root / "Module/DisplayModule_ST7796_320x480"
Path(recipe["file"]).write_text(current_source(folder, folder.name, CACHE / "1BC88B399732FF5AEC94441A29CBACB7"), encoding="utf-8")
arguments = recipe["arguments"]
syntax = [arguments[0], "-fsyntax-only"]
skip = False
for arg in arguments[1:]:
    if skip:
        skip = False
    elif arg == "-o":
        skip = True
    elif arg not in ("-MMD", "-c"):
        syntax.append(arg)
for flags in (("DISPLAY_ST7796_ASYNC_DMA=0",),
              ("DISPLAY_ST7796_ASYNC_DMA=1", "OFE_SMALL_DRAW_LINES=16"),
              ("DISPLAY_ST7796_ASYNC_DMA=1", "OFE_SMALL_DRAW_LINES=32"),
              ("DISPLAY_ST7796_ASYNC_DMA=1", "OFE_SMALL_DRAW_LINES=48")):
    subprocess.run(syntax + ["-D" + flag for flag in flags], cwd=root, timeout=180, check=True)
    print("PASS: installed ST7796 compiler: " + ", ".join(flags), flush=True)
subprocess.run(arguments, cwd=root, timeout=240, check=True)
print("PASS: current ST7796 default target object code generation (no firmware link/upload)", flush=True)
