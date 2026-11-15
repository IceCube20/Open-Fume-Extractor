"""Replace reviewed inventory limits, never protocol offsets or RGB/UI geometry."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
versions = {'DisplayModule_320x480': (36, 37), 'DisplayModule_800x480': (52, 53),
            'DisplayModule_ST7796_320x480': (46, 47)}
for name, (old, new) in versions.items():
    folder = ROOT / 'Module' / name
    for relative in (name + '.ino', 'src/OfeDisplayHomeUi.inc.h',
                     'src/OfeDisplayBridgeHomeUi.inc.h', 'src/OfeDisplayHomeActivityUi.h'):
        path = folder / relative
        raw = path.read_bytes()
        source = raw.decode('utf-8-sig').replace('\r\n', '\n')
        result = source
        if relative.endswith('.ino'):
            if '#include "src/OfeDisplayCapacity.h"' not in result:
                result = result.replace('#include <Arduino.h>', '#include <Arduino.h>\n#include "src/OfeDisplayCapacity.h"', 1)
            result = result.replace('DISPLAY_UNIVERSAL_MODULE_CACHE_MAX = 16;', 'DISPLAY_UNIVERSAL_MODULE_CACHE_MAX = 32;')
            result = result.replace(f'#define OFE_MODULE_FW_PATCH {old}\n', f'#define OFE_MODULE_FW_PATCH {new}\n')
            for array in ('module_summaries', 'expected_modules', 'ui_module_rows',
                          'ui_module_row_status_stripes', 'ui_module_row_names', 'ui_module_row_meta'):
                result = result.replace(array + '[17]', array + '[DISPLAY_MODULE_CAPACITY]')
            result = result.replace('sizeof(DisplayModuleSummary), 17,', 'sizeof(DisplayModuleSummary), DISPLAY_MODULE_CAPACITY,')
            result = result.replace('sizeof(DisplayModuleSummary) * 17', 'sizeof(DisplayModuleSummary) * DISPLAY_MODULE_CAPACITY')
            result = result.replace('uint8_t slot = 17;', 'uint8_t slot = DISPLAY_MODULE_CAPACITY;')
            result = result.replace('module_total < 17 ? module_total : 17', 'module_total < DISPLAY_MODULE_CAPACITY ? module_total : DISPLAY_MODULE_CAPACITY')
            result = result.replace('raw_total > 17 ? 17 : raw_total', 'raw_total > DISPLAY_MODULE_CAPACITY ? DISPLAY_MODULE_CAPACITY : raw_total')
            result = result.replace('home_input_values[64]', 'home_input_values[DISPLAY_INPUT_OPTION_CAPACITY]')
            result = result.replace('uint16_t values[64]', 'uint16_t values[DISPLAY_INPUT_OPTION_CAPACITY]')
            result = result.replace('home_output_addrs[18]', 'home_output_addrs[DISPLAY_OUTPUT_OPTION_CAPACITY]')
            result = result.replace('uint8_t addrs[18]', 'uint8_t addrs[DISPLAY_OUTPUT_OPTION_CAPACITY]')
        else:
            result = result.replace('cards[17]', 'cards[DISPLAY_MODULE_CAPACITY]')
            result = result.replace('sorted[17]', 'sorted[DISPLAY_MODULE_CAPACITY]')
            result = result.replace('Fan fans[22]; // Extractor, legacy tiles and up to 17 addressed module cards.',
                                    'Fan fans[5 + DISPLAY_MODULE_CAPACITY]; // Extractor, legacy tiles and addressed cards.')
        result = re.sub(r'(<|>=) 17\b', r'\1 DISPLAY_MODULE_CAPACITY', result)
        if result != source:
            # Keep the file's BOM and newline convention; no formatting churn.
            if b'\r\n' in raw:
                result = result.replace('\r\n', '\n').replace('\n', '\r\n')
            path.write_bytes((b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'') + result.encode())
            print(path.relative_to(ROOT))
