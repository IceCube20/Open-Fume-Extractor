from pathlib import Path

root = Path(__file__).resolve().parents[3]
out = root / 'tools/tests/build-checks/display-alarm-native'
out.mkdir(parents=True, exist_ok=True)
blocks = []
for index, path in enumerate(sorted((root / 'Module').glob('DisplayModule_*/*.ino'))):
    source = path.read_text(encoding='utf-8-sig')
    start = source.index('static void lv_update_alarm_header_ui() {')
    end = source.index('\n}\n', start) + 3
    blocks.append(f'namespace display{index} {{\n' + source[start:end] + '\n}\n')
if len(blocks) != 4:
    raise RuntimeError('Expected all four production displays')
(out / 'alarm_headers.h').write_text(''.join(blocks), encoding='utf-8')
