"""Reproduce LVGL style code generation without touching IDE caches or libraries."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
META = ROOT / 'tools/tests/build-checks/display-board-syntax/DisplayModule_800x480/compile_commands.json'
BUILD = ROOT / 'tools/tests/build-checks/lvgl-style-ice'
SOURCE = Path.home() / 'Documents/Arduino/libraries/lvgl/src/core/lv_obj_style.c'


def main():
    entry = json.loads(META.read_text(encoding='utf-8'))[0]
    arguments = entry['arguments']
    compiler = str(Path(arguments[0]).with_name('xtensa-esp32s3-elf-gcc.exe'))
    flags = []
    skip = False
    for arg in arguments[1:]:
        if skip:
            skip = False
        elif arg == '-o':
            skip = True
        elif arg not in (entry['file'], '-MMD'):
            flags.append(arg.replace('/flags/cpp_flags', '/flags/c_flags').replace(
                '\\flags\\cpp_flags', '\\flags\\c_flags'))
    BUILD.mkdir(parents=True, exist_ok=True)
    version = subprocess.run([compiler, '--version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    print(version, flush=True)
    print('Source SHA256: ' + hashlib.sha256(SOURCE.read_bytes()).hexdigest(), flush=True)
    failures = []
    for attempt in range(1, 4):
        output = BUILD / ('style-O3-' + str(attempt) + '.o')
        command = [compiler, *flags, '-O3', str(SOURCE), '-o', str(output)]
        (BUILD / ('command-' + str(attempt) + '.json')).write_text(json.dumps(command, indent=2), encoding='utf-8')
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=180)
        (BUILD / ('result-' + str(attempt) + '.txt')).write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode or not output.exists() or output.read_bytes()[:4] != b'\x7fELF':
            failures.append(attempt)
            print('FAIL: -O3 code generation, attempt ' + str(attempt) + '\n' + result.stdout + result.stderr, flush=True)
        else:
            print('PASS: actual lv_obj_style.c -O3 code generation, RGB SDK, attempt ' + str(attempt), flush=True)
    if failures:
        raise SystemExit('LVGL compiler crash reproduced; see ' + str(BUILD))
    print('PASS: isolated LVGL style compilation; no SDK, library, IDE cache or firmware settings modified')


if __name__ == '__main__':
    main()
