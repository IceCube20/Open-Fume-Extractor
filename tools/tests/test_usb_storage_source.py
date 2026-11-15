from pathlib import Path
import hashlib
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT/'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class UsbStorageSourceTests(unittest.TestCase):
    def test_upstream_is_pinned_and_staged_without_unreviewed_edits(self):
        manifest = json.loads((SRC/'usb_msc/UPSTREAM.json').read_text())
        self.assertEqual(manifest['version'], '1.3.0')
        for name, record in manifest['staged'].items():
            self.assertEqual(hashlib.sha256((ROOT/'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3'/name).read_bytes()).hexdigest(), record['sha256'])

    def test_usb_is_p4_only_and_never_formats_media(self):
        code = (SRC/'MasterUsbStorage.cpp').read_text()
        self.assertIn('#if defined(CONFIG_IDF_TARGET_ESP32P4)', code)
        self.assertIn('config.format_if_mount_failed = false', code)
        self.assertNotIn('msc_host_vfs_format(', code)
        self.assertNotIn('ESP_ERROR_CHECK(', code)
        self.assertIn('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT', code)
        self.assertIn('O_WRONLY | O_CREAT | O_EXCL', code)
        self.assertIn('generation != state->generation', code)
        self.assertIn('uint32_t(millis() - state->touched)', code)
        self.assertIn('token != state->token', code)
        self.assertIn('memcmp(state->buffer + offset', code)

    def test_routes_require_auth_and_do_not_install_firmware(self):
        code = (SRC/'WebUsbStorage.inc.h').read_text()
        self.assertEqual(code.count('web.on(') + 1, code.count('if (web_require_auth())'))  # Multipart callback is authenticated too.
        self.assertNotIn('Update.begin', code)
        self.assertNotIn('esp_ota_set_boot_partition', code)
        ui = (SRC/'WebUsbStorageUi.h').read_text()
        self.assertIn("input.dispatchEvent(new Event('change'", ui)
        self.assertNotIn('sendMaster(', ui)
        self.assertNotIn('sendModule(', ui)
        self.assertIn("await loadBackupFile(file)", ui)
        self.assertNotIn("fetch('/config/import'", ui)

    def test_header_uses_existing_poll_and_hides_without_device(self):
        code = (SRC/'WebTransportIcons.h').read_text()
        self.assertIn('e.hidden=!s.usb_detected', code)
        self.assertIn("e.href='/config#usb-storage'", code)
        self.assertIn("usbHeader(s)", code)
        self.assertNotIn("fetch('/usb/status'", code)

    def test_clients_are_ready_before_first_enumeration(self):
        code = (SRC/'MasterUsbStorage.cpp').read_text()
        worker = code[code.index('void worker(void*)'):code.index('void quoted(')]
        self.assertLess(worker.index('msc_host_install(&config)'), worker.index('xTaskCreatePinnedToCore(hostTask'))
        self.assertLess(worker.index('monitor::begin()'), worker.index('xTaskCreatePinnedToCore(hostTask'))
        self.assertIn('host.peripheral_map = 1U;', worker)

    def test_observer_does_not_claim_interfaces_or_change_power(self):
        code = (SRC/'MasterUsbMonitor.cpp').read_text()
        self.assertNotIn('usb_host_interface_claim(', code)
        self.assertNotIn('usb_host_transfer_submit(', code)
        self.assertNotIn('usb_host_lib_set_root_port_power(', code)
        self.assertNotIn('gpio_set_level(', code)
        self.assertIn('usb_parse_next_descriptor_of_type(', code)
        self.assertIn('usb_host_device_close(client, device)', code)

    def test_sdk_enumeration_filter_is_explicitly_registered(self):
        code = (SRC/'MasterUsbStorage.cpp').read_text()
        worker = code[code.index('void worker(void*)'):code.index('void quoted(')]
        self.assertIn('#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK', worker)
        self.assertIn('host.enum_filter_cb = allowEnumeration;', worker)
        self.assertLess(worker.index('host.enum_filter_cb ='), worker.index('usb_host_install(&host)'))

    def test_usb_diagnostics_require_server_developer_mode(self):
        self.assertIn('statusJson(bool developerMode = false)', (SRC/'MasterUsbStorage.h').read_text())
        self.assertIn('if (developerMode) monitor::appendJson(json);', (SRC/'MasterUsbStorage.cpp').read_text())
        self.assertIn('ofe_usb::statusJson(developer_mode_enabled)', (SRC/'WebUsbStorage.inc.h').read_text())
        ui = (SRC/'WebUsbStorageUi.h').read_text()
        self.assertIn('hostDiagnostics(s.developer_mode===true?s.diagnostics:null)', ui)
        self.assertIn('info.hidden=list.hidden=!d', ui)

    def test_census_is_not_dependent_on_hub_new_device_events(self):
        code = (SRC/'MasterUsbMonitor.cpp').read_text()
        self.assertIn('usb_host_device_addr_list_fill(', code)
        self.assertIn('void callback(const usb_host_client_event_msg_t*, void*) {}', code)
        self.assertIn('report.configured = count', code)
        self.assertNotIn('OpenDevice handles[', code)
        self.assertIn('copy.allocated', code)


if __name__ == '__main__':
    unittest.main()
