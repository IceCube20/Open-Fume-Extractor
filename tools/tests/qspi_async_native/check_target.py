"""Compile both QSPI scheduling branches using the owned Arduino target recipe."""
import json
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / "tools/tests"))
from check_display_board_syntax import current_source, CACHE

build = root / "tools/tests/build-checks/display-board-syntax/DisplayModule_320x480"
recipe = json.loads((build / "compile_commands.json").read_text())[0]
folder = root / "Module/DisplayModule_320x480"
Path(recipe["file"]).write_text(current_source(folder, folder.name, CACHE / "124D6934FB87C44CAB4C109EA08F31BB"), encoding="utf-8")
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
for flag in ("DISPLAY_QSPI_ASYNC_FULL_FRAME=0", "DISPLAY_QSPI_ASYNC_FULL_FRAME=1"):
    subprocess.run(syntax + ["-D" + flag], cwd=root, timeout=180, check=True)
    print("PASS: QSPI installed target compiler: " + flag, flush=True)
subprocess.run(arguments, cwd=root, timeout=240, check=True)
print("PASS: current QSPI default target object code generation (no firmware link/upload)", flush=True)
