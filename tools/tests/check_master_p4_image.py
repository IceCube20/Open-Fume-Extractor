"""Verify bounded P4 app/bootloader images, merged content and exported copies."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--poe', action='store_true')
    parser.add_argument('--ide-profile', action='store_true', help='Verify exports made with the separate OFE Arduino IDE board')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    name = 'OpenFumeExtractorMCU_P4_POE' if args.poe else 'OpenFumeExtractorMCU_P4'
    sketch = root/'OpenFumeExtractorMaster'/name
    build = args.build.resolve()
    ide_board = 'ofemcu_p4_poe' if args.poe else 'ofemcu_p4'
    export = sketch/('build/ofemcu.esp32.'+ide_board if args.ide_profile else 'build/esp32.esp32.esp32p4')
    minimum, maximum = (301,399) if args.poe else (1,199)
    mib = 32 if args.poe else 16
    merged = (build/(name+'.ino.merged.bin')).read_bytes()
    assert len(merged) == mib*1024*1024, 'Merged image flash size'
    for suffix, offset in (('bin',0x10000),('bootloader.bin',0x2000),('partitions.bin',0x8000)):
        filename = name+'.ino.'+suffix
        data = (build/filename).read_bytes()
        assert (export/filename).read_bytes() == data, filename+' export is stale'
        assert merged[offset:offset+len(data)] == data, filename+' merged content differs'
        if suffix != 'partitions.bin':
            assert data[0] == 0xE9 and struct.unpack_from('<H',data,12)[0] == 18, filename+' wrong chip'
            assert struct.unpack_from('<HH',data,15) == (minimum,maximum), filename+' wrong revision range'
            assert data[3]>>4 == (5 if args.poe else 4), filename+' wrong flash header'
    assert (export/(name+'.ino.merged.bin')).read_bytes() == merged
    partition = root/'tools/boards'/f'master_p4_{mib}mb.csv'
    assert (build/'partitions.csv').read_bytes() == partition.read_bytes()
    app = (build/(name+'.ino.bin')).read_bytes()
    assert len(app) <= 5*1024*1024, 'App exceeds OTA slot'
    manifest = json.loads((sketch/'p4-master-manifest.json').read_text())
    identity = re.search(rb'OFE_FW_SIG:v1;target=MASTER;version=([^;]+);', app)
    assert identity and identity[1].decode() in (manifest['firmware_version'],manifest['firmware_version']+'-Developer')
    board = 'Waveshare ESP32-P4-WIFI6-POE-ETH' if args.poe else 'Waveshare Module DEV KIT'
    assert board.encode() in app
    options = json.loads((build/'build.options.json').read_text())
    if args.ide_profile:
        assert options['fqbn'].split(':',3)[:3] == ['ofemcu','esp32',ide_board]
    else:
        assert ('ChipVariant=postv3' if args.poe else 'ChipVariant=prev3') in options['fqbn']
    report = dict(firmware=identity[1].decode(), board=board, image_bytes=len(app),
                  sha256=hashlib.sha256(app).hexdigest(), chip_id=18,
                  flash_mib=mib, revision_min=minimum, revision_max=maximum,
                  merged_and_exports_match=True, hardware_tested=False)
    (build/'image-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
