"""Fetch a pinned Espressif MSC component into the sketch, not the global SDK."""
import hashlib
import argparse
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
COMMIT = '7d9431146f9078ebe4f0ac94e9a69e88cee64deb'
PREFIX = 'host/class/msc/usb_host_msc/'


def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'OFE-USB-vendor'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',action='store_true')
    args=parser.parse_args()
    tree = json.loads(fetch(f'https://api.github.com/repos/espressif/esp-usb/git/trees/{COMMIT}?recursive=1'))
    if tree.get('truncated'):
        raise RuntimeError('Incomplete upstream file list')
    dest = ROOT / 'tools/tests/build-checks/usb-msc-upstream'
    records = {}
    for item in tree['tree']:
        name = item['path']
        if item['type'] != 'blob' or not name.startswith(PREFIX):
            continue
        relative = name[len(PREFIX):]
        if not (relative.startswith(('src/', 'include/', 'private_include/')) or relative in
                ('LICENSE', 'README.md', 'idf_component.yml', 'CMakeLists.txt')):
            continue
        target = (dest / relative).resolve()
        if not target.is_relative_to(dest.resolve()):
            raise RuntimeError('Unsafe upstream path')
        data = fetch(f'https://raw.githubusercontent.com/espressif/esp-usb/{COMMIT}/{name}')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        records[relative] = hashlib.sha256(data).hexdigest()
        print(relative)
    license_data=fetch('https://www.apache.org/licenses/LICENSE-2.0.txt')
    (dest/'LICENSE').write_bytes(license_data)
    manifest={'repository':'https://github.com/espressif/esp-usb','commit':COMMIT,'version':'1.3.0',
              'license':'Apache-2.0','license_sha256':hashlib.sha256(license_data).hexdigest(),'files':records}
    (dest/'UPSTREAM.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if not args.stage:return
    sketch=ROOT/'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3'
    staged={}
    for name,digest in records.items():
        source=dest/name
        data=source.read_bytes()
        if hashlib.sha256(data).hexdigest()!=digest:raise RuntimeError('Modified upstream source: '+name)
        if name.startswith('include/usb/'):
            relative='usb/'+Path(name).name
        elif name.startswith(('private_include/','include/esp_private/')):
            relative='src/usb_msc/'+Path(name).name
        elif name in ('src/msc_host.c','src/msc_host_vfs.c','src/msc_scsi_bot.c','src/diskio_usb.c'):
            relative='src/usb_msc/'+Path(name).name
            data=b'#include "sdkconfig.h"\n#if defined(CONFIG_IDF_TARGET_ESP32P4)\n'+data+b'\n#endif\n'
        else:continue
        target=sketch/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        staged[relative]={'upstream':name,'sha256':hashlib.sha256(data).hexdigest()}
    folder=sketch/'src/usb_msc';folder.mkdir(parents=True,exist_ok=True)
    (folder/'LICENSE').write_bytes(license_data)
    manifest['staged']=staged
    (folder/'UPSTREAM.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Staged pinned MSC source; only P4 guards added to C files. No global SDK changes.')


if __name__ == '__main__':
    main()
