"""Check current display sources with installed board flags, without rebuilding libraries."""
import json
import os
import re
import shlex
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ["ProgramFiles"]) / "Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
CACHE = Path(os.environ["LOCALAPPDATA"]) / "arduino/sketches"
SPECS = (
    ("DisplayModule_320x480", "124D6934FB87C44CAB4C109EA08F31BB"),
    ("DisplayModule_800x480", "2D5318A49D7C8999D370733F2B920513"),
    ("DisplayModule_ST7796_320x480", "1BC88B399732FF5AEC94441A29CBACB7"),
)


def board_properties(folder, fqbn):
    result = subprocess.run(
        [str(CLI), "compile", "--fqbn", fqbn, "--show-properties=expanded", "--json", str(folder)],
        capture_output=True, text=True, encoding="utf-8", timeout=90, check=True,
    )
    response = json.loads(result.stdout)
    if not response.get("success"):
        raise RuntimeError(result.stdout)
    return dict(item.split("=", 1) for item in response["builder_result"]["build_properties"])


def current_source(folder, name, cache):
    generated = (cache / "sketch" / (name + ".ino.cpp")).read_text(encoding="utf-8-sig")
    directives = list(re.finditer(r'^#line \d+ "[^"\r\n]*"\r?$', generated, re.M))
    if len(directives) < 3:
        raise RuntimeError("Missing cached Arduino prototypes: " + name)
    prototypes = generated[directives[1].start():directives[-1].start()]
    source = (folder / (name + ".ino")).read_text(encoding="utf-8-sig")
    marker = "static void ofe_keep_module_fw_signature() __attribute__((noinline));"
    if source.count(marker) != 1:
        raise RuntimeError("Unknown prototype insertion point: " + name)
    return source.replace(marker, marker + "\n" + prototypes)


def check(name, cache_id):
    folder = ROOT / "Module" / name
    cache = CACHE / cache_id
    options = json.loads((cache / "build.options.json").read_text(encoding="utf-8-sig"))
    recorded = re.sub(r"\\+", "/", options["sketchLocation"]).rstrip("/")
    if recorded.lower() != folder.as_posix().lower():
        raise RuntimeError("Cache belongs to another sketch: " + str(cache))
    props = board_properties(folder, options["fqbn"])
    build = ROOT / "tools/tests/build-checks/display-board-syntax" / name
    build.mkdir(parents=True, exist_ok=True)
    source = build / (name + ".cpp")
    source.write_text(current_source(folder, name, cache), encoding="utf-8")
    (build / "build_opt.h").write_bytes((folder / "build_opt.h").read_bytes())
    (build / "file_opts").write_text("", encoding="utf-8")
    platform = Path(props["runtime.platform.path"])
    includes = [folder, folder / "src", Path(props["build.core.path"]), Path(props["build.variant.path"])]
    includes += [platform / "libraries" / lib / "src"
                 for lib in ("Preferences", "Update", "Wire", "WiFi", "Network", "ESPmDNS", "SPI")]
    user_libraries = Path(os.environ["USERPROFILE"]) / "Documents/Arduino/libraries"
    includes += [user_libraries / lib / "src"
                 for lib in ("GFX_Library_for_Arduino", "lvgl", "NimBLE-Arduino")]
    includes += [user_libraries / "Adafruit_NeoPixel"]
    recipe = props["recipe.cpp.o.pattern"]
    recipe = recipe.replace(props["build.path"], str(build))
    recipe = recipe.replace("{includes}", " ".join('"-I' + str(p) + '"' for p in includes))
    recipe = recipe.replace("{source_file}", str(source)).replace("{object_file}", str(build / "unused.o"))
    if re.search(r"\{[^}]+\}", recipe):
        raise RuntimeError("Unexpanded board compiler property: " + recipe)
    # Arduino recipes retain literal quotes inside -D values; only remove outer
    # argument quotes. No command string is evaluated by a shell.
    arguments = [p[1:-1] if p.startswith('"') and p.endswith('"') else p
                 for p in shlex.split(recipe, posix=False)]
    if not Path(arguments[0]).exists() and Path(arguments[0] + ".exe").exists():
        arguments[0] += ".exe"
    (build / "compile_commands.json").write_text(json.dumps([{
        "arguments": arguments, "file": str(source), "directory": str(ROOT),
    }], indent=2), encoding="utf-8")
    syntax = [arguments[0], "-fsyntax-only"]
    skip = False
    for argument in arguments[1:]:
        if skip:
            skip = False
        elif argument == "-o":
            skip = True
        elif argument not in ("-MMD", "-c"):
            syntax.append(argument)
    subprocess.run(syntax, cwd=ROOT, check=True, timeout=180)
    print("PASS: current " + name + ", installed board/SDK flags and project headers", flush=True)


if __name__ == "__main__":
    for spec in SPECS:
        check(*spec)
