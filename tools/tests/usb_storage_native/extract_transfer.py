from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT/'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterUsbStorage.cpp').read_text()
out = ROOT/'tools/tests/build-checks/usb-storage-native'
out.mkdir(parents=True, exist_ok=True)
(out/'transfer.h').write_text(source[source.index('bool command(const String&'):source.index('bool sendFile(WebServer&')])
(out/'enumeration.h').write_text(source[source.index('#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK'):source.index('void worker(void*)')])
worker_start = source.index('void worker(void*) {') + len('void worker(void*) {')
(out/'host_init.h').write_text('void installHostForTest() {' + source[worker_start:source.index('  monitor::hostResult(err);', worker_start)] + '\n}\n')
monitor = (ROOT/'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterUsbMonitor.cpp').read_text()
(out/'census.h').write_text(monitor[monitor.index('esp_err_t describe('):monitor.index('void task(void*)')])
