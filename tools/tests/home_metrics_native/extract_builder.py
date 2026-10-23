"""Extract the actual Master metrics builder for native wire/cache regressions."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
source = (root / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text(encoding='utf-8')
start = source.index('static ofe_home_metrics::State display_home_metrics(')
end = source.index('\n}\n', start) + 3
Path(sys.argv[1]).write_text(source[start:end], encoding='utf-8')
start = source.index('static void sync_fanio_gpio_entities(')
end = source.index('\n}\n', start) + 3
Path(sys.argv[1]).with_name('fanio_live.inc.h').write_text(source[start:end], encoding='utf-8')
start = source.index('bool MasterScheduler::readIoStatus(')
end = source.index('\n}\n', start) + 3
Path(sys.argv[1]).with_name('fanio_readback.inc.h').write_text(
    source[start:end].replace('bool MasterScheduler::readIoStatus(', 'static bool readIoStatus('), encoding='utf-8')
display = (root / 'Module/DisplayModule_800x480/DisplayModule_800x480.ino').read_text(encoding='utf-8')
start = display.index('static uint8_t parse_display_live_io(')
end = display.index('\n}\n', start) + 3
Path(sys.argv[1]).with_name('live_parser.inc.h').write_text(display[start:end], encoding='utf-8')

# Execute both real response dispatch loops, including the JBC branch before
# the Home capability. Presence-only assertions missed that marker collision.
def block(text, start):
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

parts = []
for name, anchor, binding in (
    ('async', 'void MasterScheduler::processDisplayStatusResponse(', 'auto& display = record;'),
    ('sync', 'bool MasterScheduler::sendDisplayStatus(', 'auto* display = &record;'),
):
    start = source.index('while (rp < resp.len)', source.index(anchor))
    loop = block(source, start)
    parts.append('static void negotiate_' + name + '(ModuleRecord& record, const Frame& resp) {\n' + binding + '''
uint8_t rp = 6, requested_universal_addr = 0, requested_universal_start = 0;
uint8_t requested_module_list_start = 0, requested_module_detail_addr = 0;
bool display_requested_universal_page = false, display_requested_module_list = false;
bool display_requested_module_detail = false;
''' + loop + '\n}\n')
for name in ('DisplayModule_320x480', 'DisplayModule_800x480', 'DisplayModule_ST7796_320x480'):
    sketch = (root / 'Module' / name / (name + '.ino')).read_text(encoding='utf-8')
    start = sketch.index('  // Advertise the compact per-module Home record')
    end = sketch.index('  bus.send(resp);', start)
    parts.append('static void advertise_' + name + '(Frame& resp) {\n' + sketch[start:end] + '\n}\n')
Path(sys.argv[1]).with_name('negotiation.inc.h').write_text('\n'.join(parts), encoding='utf-8')

mqtt = (root / 'OpenFumeExtractorMaster/src/MasterMqtt.inc.h').read_text(encoding='utf-8')
start = mqtt.index('  if (!mqtt_state_module_cycle_pending) {', mqtt.index('static void mqtt_publish_state('))
begin = 'static void begin_cycle() {\n' + block(mqtt, start) + '\n}\n'
tick = block(mqtt, mqtt.index('static void mqtt_publish_module_cycle_tick()'))
Path(sys.argv[1]).with_name('mqtt_cycle.inc.h').write_text(begin + tick, encoding='utf-8')
