"""Check real Xtensa output: aligned word transfers, no external memcpy/helper call."""
from pathlib import Path
import json
import re
import subprocess

root = Path(__file__).resolve().parents[2]
folder = root / "tools/tests/build-checks/display-board-syntax/DisplayModule_800x480"
entry = json.loads((folder / "compile_commands.json").read_text(encoding="utf-8"))[0]
output = root / "tools/tests/build-checks/rgb-performance"
output.mkdir(parents=True, exist_ok=True)
args = [entry["arguments"][0]]
skip = False
for arg in entry["arguments"][1:]:
    if skip:
        skip = False
    elif arg == "-o":
        skip = True
    elif arg != entry["file"] and arg != "-MMD":
        args.append(arg)
obj = output / "rgb-copy.o"
args.extend([str(root / "tools/tests/rgb_copy_codegen.cpp"), "-o", str(obj)])
subprocess.run(args, cwd=root, check=True, timeout=90)
tool = Path(args[0]).with_name("xtensa-esp32s3-elf-objdump.exe")
assembly = subprocess.run([str(tool), "-dr", str(obj)], check=True, capture_output=True, text=True).stdout
(output / "rgb-copy.asm.txt").write_text(assembly, encoding="utf-8")
assert "Disassembly of section .iram1:" in assembly
assert re.search(r"\bl32i(?:\.n)?\b", assembly), "Missing word load"
assert re.search(r"\bs32i(?:\.n)?\b", assembly), "Missing word store"
assert not re.search(r"\bcall(?:0|4|8|12|x\d+)\b", assembly), "Hot copy unexpectedly calls outside IRAM"
print("PASS: Xtensa IRAM copy emits 32-bit loads/stores without an external helper/memcpy call")
