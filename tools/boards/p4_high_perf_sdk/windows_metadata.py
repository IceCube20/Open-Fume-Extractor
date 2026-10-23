"""Read native CMake metadata for the pinned upstream Bash SDK exporter."""
import argparse
from collections import deque
import json
from pathlib import Path
import re
import shutil


def msys_paths(command):
    # Convert drive paths, not escaped quotes in -DNAME=\"value\" flags.
    return re.sub(r'([A-Za-z]):([\\/](?:[^\\\s"]|\\(?!"))*)',
                  lambda m: '/' + m[1].lower() + m[2].replace('\\', '/'), command)


def compile_command(builder, filename):
    document = json.loads((builder / 'build/compile_commands.json').read_text())
    rows = [row for row in document if Path(row['file'].replace('\\', '/')).name == filename]
    if len(rows) != 1:
        raise ValueError(f'Expected one compilation command for {filename}, got {len(rows)}')
    return msys_paths(rows[0]['command'])


def link_command(builder):
    text = (builder / 'build/build.ninja').read_text()
    result = []
    for name in ('LINK_FLAGS', 'LINK_LIBRARIES', 'LINK_PATH'):
        rows = re.findall(r'^  ' + name + r' = (.*)$', text, re.M)
        if len(rows) != 1:
            raise ValueError(f'Expected one native {name}, got {len(rows)}')
        result.append(msys_paths(rows[0]))
    return ' '.join(result)


def copy_headers(builder, source, destination, component):
    builder, source, destination, component = (Path(p).resolve() for p in
                                               (builder, source, destination, component))
    sdk = builder / 'out/tools/esp32-arduino-libs/esp32p4_es'
    if not destination.is_relative_to(sdk) or not all(p.is_relative_to(builder) for p in (source, component)):
        raise ValueError('Header copy escapes the isolated SDK workspace')
    suffixes = {'.h', '.hpp', '.inc'}
    for path in source.rglob('*'):
        if path.is_file() and path.suffix in suffixes:
            if not path.resolve().is_relative_to(builder):
                raise ValueError('Header source link escapes workspace')
            target = destination / path.relative_to(source)
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
    # Match the upstream exporter's transitive ../ include recovery, without
    # launching grep/sed/cp once per header on Windows.
    pending = deque((path, source / path.relative_to(destination)) for path in destination.rglob('*')
                    if path.is_file() and path.suffix in suffixes)
    while pending:
        target, original = pending.popleft()
        for relative in re.findall(r'#include *"([^"\r\n]*\.\./[^"\r\n]*)"',
                                   target.read_text(encoding='utf-8', errors='replace')):
            output = (target.parent / relative).resolve()
            if not output.is_relative_to(sdk) or output.exists():
                continue
            candidate = (original.parent / relative).resolve()
            if not candidate.is_file():
                candidate = component / re.sub(r'^(\.\./)*', '', relative)
            if (candidate.is_file() and candidate.suffix in suffixes and
                    candidate.resolve().is_relative_to(builder)):
                output.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(candidate, output)
                pending.append((output, candidate))


def refresh_archive_aliases(original):
    anchor = '\t\t\t\t\t\t\t\t\tif [ -f "$n_item" ]; then\n'
    if original.count(anchor) != 1:
        raise ValueError('Upstream archive alias handling changed')
    # The upstream retry branch links existing aliases but omits their export.
    # Refresh them from the current archive too, not a previous build's bytes.
    repair = (anchor + '\t\t\t\t\t\t\t\t\t\tif [[ $EXCLUDE_LIBS != *";$lname;"* ]]; then\n'
              '\t\t\t\t\t\t\t\t\t\t\tcp -f "$item" "$n_item" || exit 1\n'
              '\t\t\t\t\t\t\t\t\t\t\tif [[ $LD_LIB_FILES != *"$n_item"* ]]; then LD_LIB_FILES+="$n_item "; fi\n'
              '\t\t\t\t\t\t\t\t\t\tfi\n')
    return original.replace(anchor, repair)


def publish_memory_variant(builder):
    builder = Path(builder).resolve()
    sdk = (builder / 'out/tools/esp32-arduino-libs/esp32p4_es').resolve()
    if not sdk.is_relative_to(builder):
        raise ValueError('Memory variant output escapes workspace')
    document = json.loads((builder / 'configs/builds.json').read_text())
    for entry in document['mem_variants_files']:
        if 'esp32p4_es' not in entry['targets']:
            continue
        source = (sdk / entry['out']).resolve()
        destination = (sdk / 'qio_qspi' / entry['file']).resolve()
        if not all(path.is_relative_to(sdk) for path in (source, destination)):
            raise ValueError('Memory variant file escapes SDK')
        if source.is_file():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            source.unlink()
        elif not destination.is_file():
            raise ValueError(f'Missing common memory variant file: {source}')


def exporter_script(original):
    for filename in ('arduino-lib-builder-gcc.c', 'arduino-lib-builder-as.S',
                     'arduino-lib-builder-cpp.cpp'):
        start = f"str=`cat build/compile_commands.json | grep {filename} | grep command | cut -d':' -f2 | cut -d',' -f1`"
        old = start + '\nstr="${str:2:${#str}-1}" #remove leading space and quotes\nstr=`printf \'%b\' "$str"` #unescape the string'
        if original.count(old) != 1:
            raise ValueError(f'Upstream export command changed: {filename}')
        original = original.replace(old, f'str=$(python3 "$OFE_WINDOWS_METADATA" compile {filename}) || exit 1')
    if original.count('xfile="${item:3:${#item}-5}"') != 3:
        raise ValueError('Upstream compiler response-file handling changed')
    original = original.replace('xfile="${item:3:${#item}-5}"',
                                'xfile="${item#@}"; xfile="${xfile#\\\"}"; xfile="${xfile%\\\"}"')
    original = refresh_archive_aliases(original)
    start = original.index('if [ -f "build/CMakeFiles/arduino-lib-builder.elf.dir/link.txt" ]; then')
    end = original.index('\nif [ "$IDF_TARGET" = "esp32" ]; then\n\tLD_SCRIPTS', start)
    original = original[:start] + 'str=$(python3 "$OFE_WINDOWS_METADATA" link) || exit 1\n' + original[end:]
    start = original.index('\t\tfor f in `find "$item" -name \'*.h\'`; do')
    end = original.index('\n\tfi\ndone\necho "        join($PIOARDUINO_SDK', start)
    return (original[:start] + '\t\tpython3 "$OFE_WINDOWS_METADATA" copy-headers "$item" "$out_cpath" "$ipath" || exit 1\n'
            + original[end:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('compile', 'link', 'copy-headers', 'memory-variant'))
    parser.add_argument('arguments', nargs='*')
    args = parser.parse_args()
    builder = Path.cwd()
    if args.action == 'memory-variant':
        if args.arguments:
            parser.error('memory-variant takes no arguments')
        publish_memory_variant(builder)
    elif args.action == 'copy-headers':
        if len(args.arguments) != 3:
            parser.error('copy-headers needs source, destination and component root')
        copy_headers(builder, *args.arguments)
    else:
        if len(args.arguments) != (1 if args.action == 'compile' else 0):
            parser.error('compile needs one source filename; link takes no arguments')
        print(compile_command(builder, args.arguments[0]) if args.action == 'compile' else link_command(builder))


if __name__ == '__main__':
    main()
