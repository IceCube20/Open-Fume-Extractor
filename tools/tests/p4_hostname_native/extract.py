"""Compile the generated P4 hostname method itself against a factory-MAC stub."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
wifi = (root / 'Module/DisplayModule_P4_800x480/src/OfeDisplayWifi.h').read_text()
start = wifi.index('  void prepareHostname() {')
end = wifi.index('  bool startRadio(', start)
body = wifi[start:end]
assert 'esp_efuse_mac_get_default(mac)' in body and 'esp_read_mac(' not in body
sketch = (root / 'Module/DisplayModule_P4_800x480/DisplayModule_P4_800x480.ino').read_text()
prefix = next(line for line in sketch.splitlines() if line.startswith('#define OFE_DISPLAY_HOSTNAME_PREFIX '))
Path(sys.argv[1]).write_text(prefix + '\nstruct Hostname {\nchar device_hostname_[40] = {};\n' + body + '};\n')
