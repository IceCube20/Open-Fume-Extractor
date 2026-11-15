"""Rename presentation strings, never identifiers, protocol keys or NVS namespaces."""
from pathlib import Path
import argparse
import json
import re
import tokenize
import io

ROOT = Path(__file__).resolve().parents[2]
TOKENS = re.compile(
    r'(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)'
    r'|(?P<raw>R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)")'
    r'|(?P<quoted>"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\')'
    r'|(?P<template>`(?:\\[\s\S]|[^`\\])*`)', re.MULTILINE)


def text(value, lower=True):
    if '://' in value:
        return value
    paths = []
    def protect(match):
        paths.append(match.group())
        return f'__OFE_PATH_KEEP_{len(paths) - 1}__'
    # C++ strings can contain whole JS statements: their nested fetch URLs are
    # machine contracts even when neighboring error messages are presentation.
    value = re.sub(r'''/[^\s'"<>\\]+''', protect, value)
    value = value.replace('Modbus RTU master', 'Modbus RTU client')
    for before, after in (
        ('Alle Master', 'Alle MCUs'), ('All masters', 'All MCUs'), ('all masters', 'all MCUs'),
        ('Masterstatus', 'MCU-Status'), ('Masterfirmware', 'MCU-Firmware'),
        ('den Master', 'die MCU'), ('der Master', 'die MCU'), ('dem Master', 'der MCU'),
        ('des Masters', 'der MCU'), ('eines Masters', 'einer MCU'),
        ('einen Master', 'eine MCU'), ('zum Master', 'zur MCU'), ('vom Master', 'von der MCU'),
        ('Der Master', 'Die MCU'), ('a master', 'an MCU'),
    ):
        value = value.replace(before, after)
    value = re.sub(r'(?<![\w/])Master(?![\w/])', 'MCU', value)
    # Single-word machine values (master), master-cfg/master-net and underscores stay unchanged.
    if lower and re.search(r'\s', value) and not any(x in value for x in ('\\"', '<', '=>', 'function(', '${')):
        value = re.sub(r'\bmasters\b', 'MCUs', value)
        value = re.sub(r'(?<![\w/])master(?![\w/])', 'MCU', value)
    for index, original in enumerate(paths):
        value = value.replace(f'__OFE_PATH_KEEP_{index}__', original)
    return value


def cpp_js(source):
    def replace(match):
        value = match.group()
        if match.lastgroup == 'comment':
            return value
        if match.group('raw'):
            delimiter = match.group('delimiter')
            start, end = len(delimiter) + 3, -(len(delimiter) + 2)
            body = value[start:end]
            protected = []
            def nested(part):
                prefix = body[max(0, part.start() - 100):part.start()]
                if part.lastgroup == 'comment' or re.search(
                        r'\b(?:class|id|data-[\w-]+|for|name|value|href|src)\s*=\s*$', prefix):
                    marker = f'__OFE_MCU_KEEP_{len(protected)}__'
                    protected.append(part.group())
                    return marker
                return text(part.group(), lower=part.lastgroup != 'template')
            body = TOKENS.sub(nested, body)
            body = re.sub(r'(?<![\w/])Master(?![\w/])', 'MCU', body)
            for index, original in enumerate(protected):
                body = body.replace(f'__OFE_MCU_KEEP_{index}__', original)
            return value[:start] + body + value[end:]
        return text(value, lower=match.lastgroup != 'template')
    return TOKENS.sub(replace, source)


def python_strings(source):
    lines = source.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    edits = []
    for token in tokenize.generate_tokens(io.StringIO(source).readline):
        if token.type in (tokenize.STRING, getattr(tokenize, 'FSTRING_MIDDLE', tokenize.STRING)):
            changed = text(token.string)
            if changed != token.string:
                edits.append((offsets[token.start[0] - 1] + token.start[1], offsets[token.end[0] - 1] + token.end[1], changed))
    for start, end, changed in reversed(edits):
        source = source[:start] + changed + source[end:]
    return source


def json_values(value):
    if isinstance(value, str):
        return text(value)
    if isinstance(value, list):
        return [json_values(item) for item in value]
    if isinstance(value, dict):
        return {key: json_values(item) for key, item in value.items()}
    return value


def paths():
    for base in (ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src', ROOT / 'Module', ROOT / 'HomeAssistant/custom_components/open_fume_extractor'):
        for path in base.rglob('*'):
            if path.suffix not in ('.h', '.cpp', '.ino', '.py', '.js', '.json'):
                continue
            if any(part in ('build', 'tests', 'p4', 'tinycrypt', 'usb_msc', '__pycache__') for part in path.parts):
                continue
            if 'privateconfig' in path.name.lower() or path.name in ('MasterBoardLocalConfig.h', 'ofe_lv_conf.h', 'lv_conf.h'):
                continue
            yield path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    changed = 0
    for path in sorted(paths()):
        original = path.read_bytes()
        bom = original.startswith(b'\xef\xbb\xbf')
        source = original.decode('utf-8-sig')
        if path.suffix == '.json':
            value = json.loads(source)
            updated = json_values(value)
            replacement = source if updated == value else json.dumps(updated, ensure_ascii=source.isascii(), indent=2) + '\n'
            if updated != value and '\r\n' in source:
                replacement = replacement.replace('\n', '\r\n')
        else:
            replacement = python_strings(source) if path.suffix == '.py' else cpp_js(source)
        if replacement == source:
            continue
        changed += 1
        print(path.relative_to(ROOT))
        if args.apply:
            path.write_bytes((b'\xef\xbb\xbf' if bom else b'') + replacement.encode())
    print(('Updated' if args.apply else 'Would update') + f' {changed} source files')


if __name__ == '__main__':
    main()
