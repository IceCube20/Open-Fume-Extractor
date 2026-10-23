"""Compile the current generated P4 UI with an existing P4 build's real flags.

Only compiler input in our test folder is written. The cached firmware and the
owner's Downloads sketch are never modified. This is not a full link or upload.
"""
import argparse
import json
import subprocess
from pathlib import Path

from check_display_board_syntax import ROOT, current_source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    name = "DisplayModule_P4_800x480"
    folder = ROOT / "Module" / name
    database = json.loads((args.cache / "compile_commands.json").read_text())
    entry = next(item for item in database if Path(item["file"]).name == name + ".ino.cpp")
    build = ROOT / "tools/tests/build-checks/display-gui-p4-syntax"
    build.mkdir(parents=True, exist_ok=True)
    source = build / (name + ".cpp")
    source.write_text(current_source(folder, name, args.cache), encoding="utf-8")
    command = [entry["arguments"][0], "-fsyntax-only", "-I" + str(folder), "-I" + str(folder / "src")]
    skip = False
    for argument in entry["arguments"][1:]:
        if skip:
            skip = False
        elif argument == "-o":
            skip = True
        elif argument not in ("-c", "-MMD", entry["file"]):
            command.append(argument)
    command.append(str(source))
    subprocess.run(command, check=True, cwd=ROOT, timeout=180)
    print("PASS: generated P4 sketch, GUI task and project headers with installed P4 SDK/PPA compiler flags")


if __name__ == "__main__":
    main()
