"""Extract production USB implementations, without maintaining test-side copies."""

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'
DEFAULT_OUTPUT = ROOT / 'tools/tests/build-checks/usb-storage-native'


def section(source, start, end):
    first = source.index(start)
    return source[first:source.index(end, first + len(start))]


def extract_volume_label(source):
    return section(source, 'esp_err_t ofe_usb_volume_label(', '\n}\n') + '\n}\n'


def extract(source, monitor):
    worker_start = source.index('void worker(void*) {') + len('void worker(void*) {')
    return {
        'transfer.h': section(source, 'bool command(const String&', 'bool sendFile(WebServer&'),
        'transfer_helpers.h': section(source, 'void message(', 'void event('),
        'device_event.h': section(source, 'void event(', 'void hostTask('),
        'drive_globals.h': section(source, 'Drive drives[MAX_DRIVES]', 'struct Guard'),
        'drive_registry.h': section(source, 'void cacheVolumeLabel(', 'void readSelected()'),
        'drive_api.h': section(source, 'bool detected()', 'String statusJson('),
        'status_json.h': section(source, 'String statusJson(', 'bool command(const String&'),
        'quoted.h': section(source, 'void quoted(', '}  // namespace\n'),
        'transfer_fs.h': section(source, 'void readSelected()', 'void manageSelected('),
        'send_file.h': section(source, 'bool sendFile(WebServer&', '}  // namespace ofe_usb'),
        'transfer_unmount.h': section(source, 'void unmount()', 'void listDirectory('),
        'worker_step.h': (
            'void workerStep() {\n'
            + section(source, '    processDevices();', '    bool transferActive;')
            + '\n}\n'
        ),
        'manager_state.h': section(source, 'enum class Phase', 'State* state = nullptr;'),
        'manager_queue.h': section(source, 'uint32_t manage(', 'uint32_t receiveBegin('),
        'manager_fs.h': (
            section(source, 'void listDirectory(', 'void cacheVolumeLabel(')
            + section(source, 'void manageSelected(', '#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK')
        ),
        'enumeration.h': section(source, '#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK', 'void worker(void*)'),
        'host_init.h': (
            'void installHostForTest() {'
            + source[worker_start:source.index('  monitor::hostResult(err);', worker_start)]
            + '\n}\n'
        ),
        'census.h': section(monitor, 'esp_err_t describe(', 'void task(void*)'),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--check', action='store_true', help='Validate extraction without writing files or compiling.')
    args = parser.parse_args()
    headers = extract(
        (SRC / 'MasterUsbStorage.cpp').read_text(encoding='utf-8'),
        (SRC / 'MasterUsbMonitor.cpp').read_text(encoding='utf-8'),
    )
    headers['volume_label.h'] = extract_volume_label(
        (SRC / 'usb_msc/msc_host_vfs.c').read_text(encoding='utf-8'))
    for name, code in headers.items():
        if not code.strip():
            raise ValueError(f'Empty production extraction: {name}')
    if args.check:
        print(f'PASS: extracted {len(headers)} production sections without writing files')
        return
    args.output.mkdir(parents=True, exist_ok=True)
    for name, code in headers.items():
        (args.output / name).write_text(code, encoding='utf-8')


if __name__ == '__main__':
    main()
