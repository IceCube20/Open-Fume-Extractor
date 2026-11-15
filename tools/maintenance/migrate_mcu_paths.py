"""Migrate active tooling references after the owner's MCU folder move."""
from pathlib import Path
import argparse
import os

ROOT = Path(__file__).resolve().parents[2]
S3 = 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3'
P4 = 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4'


def migrate(source):
    for before, after in (
        ('MasterP4/OpenFumeExtractorMaster_P4', P4),
        ('OpenFumeExtractorMaster/src', S3 + '/src'),
        ('OpenFumeExtractorMaster/OpenFumeExtractorMaster.ino', S3 + '/OpenFumeExtractorMCU_S3.ino'),
    ):
        source = source.replace(before, after)
        source = source.replace(before.replace('/', '\\'), after.replace('/', '\\'))
        source = source.replace(before.replace('/', '\\\\'), after.replace('/', '\\\\'))
    for quote in ('"', "'"):
        source = source.replace(quote + 'OpenFumeExtractorMaster' + quote, quote + S3 + quote)
    return source


def paths():
    for directory in ('tools', 'HomeAssistant/tests', 'HomeAssistant/tools', '.github'):
        for parent, directories, files in os.walk(ROOT / directory):
            directories[:] = [name for name in directories if name not in (
                'build-checks', '__pycache__', 'node_modules', 'DisplayRgbSramTrial',
                'vendor', 'tinycrypt')]
            for name in files:
                path = Path(parent) / name
                if path != Path(__file__).resolve() and path.suffix in (
                        '.py', '.ps1', '.cpp', '.h', '.cjs', '.yml'):
                    yield path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    count = 0
    for path in paths():
        original = path.read_bytes()
        text = original.decode('utf-8-sig')
        updated = migrate(text)
        if updated != text:
            count += 1
            print(path.relative_to(ROOT))
            if args.apply:
                path.write_bytes((b'\xef\xbb\xbf' if original.startswith(b'\xef\xbb\xbf') else b'') + updated.encode())
    print(('Updated' if args.apply else 'Would update') + f' {count} tooling files')
