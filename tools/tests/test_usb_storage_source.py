from pathlib import Path
import hashlib
import importlib.util
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
        self.assertIn('O_RDWR | O_CREAT | O_EXCL', code)
        self.assertIn('generation != state->generation', code)
        self.assertIn('uint32_t(millis() - state->touched)', code)
        self.assertIn('token != state->token', code)
        self.assertIn('memcmp(state->buffer, state->buffer + TRANSFER_CHUNK_BYTES, count)', code)

    def test_streaming_has_fixed_buffers_and_vfs_not_ram_size_limit(self):
        code = (SRC/'MasterUsbStorage.cpp').read_text()
        policy = (SRC/'UsbStoragePolicy.h').read_text()
        self.assertIn('MAX_FILE_BYTES = INT32_MAX', policy)
        self.assertIn('TRANSFER_CHUNK_BYTES = 8192', policy)
        self.assertNotIn('6 * 1024 * 1024', policy)
        self.assertEqual(code.count('heap_caps_malloc(2 * TRANSFER_CHUNK_BYTES,'), 2)
        self.assertEqual(code.count('heap_caps_malloc(TRANSFER_CHUNK_BYTES,'), 1)
        self.assertNotIn('heap_caps_malloc(bytes', code)
        self.assertNotIn('heap_caps_malloc(entry.size', code)
        self.assertNotIn('heap_caps_malloc(state->size', code)
        receive = code[code.index('bool receiveChunk('):code.index('bool receiveCommit(')]
        self.assertIn('bytes > TRANSFER_CHUNK_BYTES', receive)
        self.assertIn('!range(state->size, offset, bytes)', receive)
        self.assertIn('bytes > state->received - offset', receive)
        send = code[code.index('bool sendFile(WebServer&'):]
        self.assertIn('client.write(chunk, count) == count', send)
        self.assertIn('client.stop()', send)
        self.assertIn('state->sending = false; state->cancel = true', send)

    def test_native_harness_extracts_worker_and_streaming_implementations(self):
        path = ROOT/'tools/tests/usb_storage_native/extract_transfer.py'
        spec = importlib.util.spec_from_file_location('usb_transfer_extraction', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        source = (SRC/'MasterUsbStorage.cpp').read_text()
        headers = module.extract(source, (SRC/'MasterUsbMonitor.cpp').read_text())
        for name in ('transfer.h', 'transfer_helpers.h', 'transfer_fs.h', 'send_file.h',
                     'transfer_unmount.h', 'manager_state.h', 'drive_globals.h',
                     'device_event.h', 'drive_registry.h', 'drive_api.h', 'status_json.h', 'quoted.h'):
            with self.subTest(header=name):
                self.assertTrue(headers[name].strip())
                self.assertIn(headers[name], source)
        worker_body = headers['worker_step.h'].removeprefix('void workerStep() {\n').removesuffix('\n}\n')
        self.assertIn(worker_body, source)
        self.assertIn('uint32_t(millis() - state->touched) > BUFFER_LEASE_MS', worker_body)
        self.assertIn('transferChunk(true)', worker_body)
        self.assertIn('transferChunk(false)', worker_body)
        self.assertLess(worker_body.index('processDevices();'), worker_body.index('char action[12]'))
        self.assertIn('activateDrive(state->requestedDrive, state->requestedInstance)', worker_body)
        # A backend edit must be extracted verbatim, not replaced with a harness implementation.
        changed = source.replace('bool waitTransfer(uint32_t token, Phase phase) {',
                                 'bool waitTransfer(uint32_t token, Phase phase) { /* extraction probe */', 1)
        self.assertIn('/* extraction probe */', module.extract(changed, (SRC/'MasterUsbMonitor.cpp').read_text())['transfer.h'])
        for signature, header in (
            ('void event(', 'device_event.h'),
            ('void unmount()', 'transfer_unmount.h'),
            ('void cacheVolumeLabel(', 'drive_registry.h'),
            ('void probeVolumeLabel(', 'drive_registry.h'),
            ('void activateDrive(', 'drive_registry.h'),
            ('void disconnectDrive(', 'drive_registry.h'),
            ('void mount(', 'drive_registry.h'),
            ('void processDevices()', 'drive_registry.h'),
            ('bool matchesDrive(', 'drive_api.h'),
            ('bool selectDrive(', 'drive_api.h'),
            ('String statusJson(', 'status_json.h'),
        ):
            with self.subTest(signature=signature):
                start = source.index('{', source.index(signature)) + 1
                changed = source[:start] + ' /* extraction probe */' + source[start:]
                self.assertIn('/* extraction probe */', module.extract(changed, (SRC/'MasterUsbMonitor.cpp').read_text())[header])
        self.assertNotIn('void cacheVolumeLabel(', headers['manager_fs.h'])
        self.assertNotIn('void probeVolumeLabel(', headers['manager_fs.h'])
        adapter = (SRC/'usb_msc/msc_host_vfs.c').read_text()
        label = module.extract_volume_label(adapter)
        self.assertIn(label, adapter)
        self.assertIn('f_getlabel(drive, label, NULL)', label)
        changed = adapter.replace('TCHAR label[129] = {0};',
                                  'TCHAR label[129] = {0}; /* extraction probe */', 1)
        self.assertIn('/* extraction probe */', module.extract_volume_label(changed))

    def test_registry_owns_four_handles_with_selected_vfs_and_transient_probe(self):
        code = (SRC/'MasterUsbStorage.cpp').read_text()
        policy = (SRC/'UsbStoragePolicy.h').read_text()
        self.assertIn('MAX_DRIVES = 4', policy)
        self.assertIn('NO_DRIVE = UINT32_MAX', policy)
        self.assertIn('Drive drives[MAX_DRIVES]', code)
        self.assertIn('std::atomic<unsigned> selectedDrive{NO_DRIVE}', code)
        self.assertIn('std::atomic<uint32_t> pendingAddresses[4]', code)
        self.assertIn('installDisconnects[MAX_DRIVES + 1]', code)
        event = code[code.index('void event('):code.index('void hostTask(')]
        self.assertIn('fetch_or(', event)
        self.assertIn('drives[i].removed.store(true)', event)
        self.assertIn('selectedDrive.load() == i', event)
        self.assertIn('installInProgress && evt->device.handle', event)
        self.assertIn('installDisconnectOverflow = true', event)
        for call in ('msc_host_install_device(', 'msc_host_uninstall_device(', 'msc_host_vfs_register(', 'unmount('):
            self.assertNotIn(call, event)
        unmount = code[code.index('void unmount()'):code.index('void listDirectory(')]
        self.assertIn('discardTransfer()', unmount)
        self.assertIn('msc_host_vfs_unregister(vfs)', unmount)
        self.assertNotIn('msc_host_uninstall_device(', unmount)
        disconnect = code[code.index('void disconnectDrive('):code.index('void descriptorText(')]
        self.assertIn('selectedDrive.load() == index', disconnect)
        self.assertIn('msc_host_uninstall_device(handle)', disconnect)
        process = code[code.index('void processDevices()'):code.index('void readSelected()')]
        gate = 'if (state->buffer || (state->phase == Phase::Queued && !strcmp(state->operation, "drive"))) return'
        self.assertLess(process.index('disconnectDrive(i)'), process.index(gate))
        self.assertLess(process.index(gate), process.index('mount(word * 32 + bit)'))
        self.assertIn('pendingAddresses[word].fetch_and(', process)
        self.assertEqual(code.count('msc_host_vfs_register('), 2)
        self.assertIn('msc_host_vfs_register(device, "/usb", &config, &vfs)', code)
        probe = code[code.index('void probeVolumeLabel('):code.index('void activateDrive(')]
        self.assertIn('config.format_if_mount_failed = false', probe)
        self.assertIn('config.max_files = 1', probe)
        self.assertIn('msc_host_vfs_register(handle, "/usb-label", &config, &probe)', probe)
        self.assertIn('cacheVolumeLabel(index, handle, probe)', probe)
        self.assertIn('msc_host_vfs_unregister(probe)', probe)
        for mutation in ('unmount()', 'refresh(', 'state->', 'selectedDrive.store(', 'activeDevice.store('):
            self.assertNotIn(mutation, probe)
        cache = code[code.index('void cacheVolumeLabel('):code.index('void probeVolumeLabel(')]
        self.assertIn('ofe_usb_volume_label(mounted, label, sizeof(label))', cache)
        self.assertIn('drives[index].connected.load()', cache)
        self.assertIn('drives[index].handle.load() == handle', cache)
        self.assertIn('strlcpy(drives[index].volumeName, label, sizeof(drives[index].volumeName))', cache)
        activate = code[code.index('void activateDrive('):code.index('void disconnectDrive(')]
        self.assertIn('cacheVolumeLabel(index, device, vfs)', activate)
        self.assertLess(activate.index('cacheVolumeLabel('), activate.index('ready.store(true)'))
        mount = code[code.index('void mount('):code.index('void processDevices()')]
        self.assertLess(mount.index('probeVolumeLabel(slot)'), mount.index('activateDrive(slot, instance)'))
        self.assertIn('drives[index].volumeName[0]', disconnect)
        self.assertIn('volumeName[64]', code)
        self.assertIn('quoted(json, drive.volumeName)', code)
        self.assertIn('\\"volume_name\\"', code)
        adapter = (SRC/'usb_msc/msc_host_vfs.c').read_text()
        label = adapter[adapter.index('esp_err_t ofe_usb_volume_label('):]
        self.assertIn('#if FF_USE_LABEL && FF_LFN_UNICODE == 2', label)
        self.assertIn('vfs->drive', label)
        self.assertIn('ff_diskio_get_pdrv_bdl(vfs->bdl)', label)
        self.assertNotIn('ff_diskio_get_pdrv_disk(', label)
        for write in ('f_setlabel(', 'f_write(', 'f_mkfs(', 'msc_host_vfs_format('):
            self.assertNotIn(write, label)
        header = (SRC/'UsbVolumeLabel.h').read_text()
        self.assertIn('esp_err_t ofe_usb_volume_label(msc_host_vfs_handle_t mount, char* output, size_t capacity)', header)

    def test_selection_and_upload_begin_require_current_generation(self):
        code = (SRC/'MasterUsbStorage.cpp').read_text()
        for start, end in (
            ('bool selectDrive(', 'String statusJson('),
            ('bool command(', 'uint32_t manage('),
            ('uint32_t receiveBegin(', 'bool waitTransfer('),
        ):
            body = code[code.index(start):code.index(end, code.index(start))]
            with self.subTest(api=start):
                self.assertIn('generation != state->generation', body)
                self.assertIn('busy()', body)
        begin = code[code.index('uint32_t receiveBegin('):code.index('bool waitTransfer(')]
        self.assertLess(begin.index('generation != state->generation'), begin.index('directory != DEFAULT_DIRECTORY'))
        selection = code[code.index('bool selectDrive('):code.index('String statusJson(')]
        self.assertIn('drives[index].instance != instance', selection)
        self.assertIn('state->requestedInstance = instance', selection)
        activate = code[code.index('void activateDrive('):code.index('void disconnectDrive(')]
        self.assertLess(activate.index('drives[index].instance != instance'), activate.index('unmount()'))

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
        self.assertIn("e.href='/usb'", code)
        self.assertIn("usbHeader(s)", code)
        self.assertNotIn("fetch('/usb/status'", code)

    def test_dedicated_browser_and_contextual_picker_replace_inline_panels(self):
        routes = (SRC/'WebUsbStorage.inc.h').read_text()
        shell = (SRC/'WebShell.inc.h').read_text()
        ui = (SRC/'WebUsbStorageUi.h').read_text()
        self.assertIn('web.on("/usb", HTTP_GET', routes)
        self.assertIn("data-usb-purpose='browser'", routes)
        self.assertIn("<section class='panel' id='usb-storage'", routes)
        self.assertIn("<script defer src='/usb/ui.js'></script>", shell)
        for name in ('WebConfig.inc.h', 'WebUpdate.inc.h', 'WebDiagnostics.inc.h'):
            self.assertNotIn('web_append_usb_panel', (SRC/name).read_text())
        self.assertIn("picker.id='usb-picker'", ui)
        self.assertIn('window.ofeUsbPick=', ui)
        self.assertIn("picker.addEventListener('cancel'", ui)
        self.assertIn('pending.invalid', ui)
        self.assertIn('input.isConnected||input.disabled', ui)
        self.assertIn("['master_fw_file','module_fw_file','c6_file']", ui)
        self.assertIn('window.ofeRs485QualityExport(true)', ui)
        profiles = (SRC/'WebStatus.inc.h').read_text()
        for function in ('universalExport', 'universalProtocolBuilderExport', 'modbusBuilderExport'):
            self.assertIn('function '+function+'(addr,usb=false)', profiles)

    def test_file_manager_keeps_identity_and_transfer_safety(self):
        ui = (SRC/'WebUsbStorageUi.h').read_text()
        self.assertIn('JSON.stringify([filePath(f),f.size])', ui)
        self.assertIn("if(selection&&!chosen())selection=''", ui)
        self.assertIn('if(signature!==listSignature)', ui)
        self.assertIn('document.createTextNode(f.name)', ui)
        self.assertIn('({drive:s?.drive,drive_instance:s?.drive_instance,generation:s?.generation})', ui)
        self.assertIn('sameDrive(s,c)&&s?.generation===c.generation', ui)
        self.assertIn('requireContext(current,expected)', ui)
        self.assertIn('params={...params,...expected}', ui)
        self.assertIn("['command','manage','write/begin'].includes(path)", ui)
        self.assertIn("api('command',{action:'read',index:f.index},expected)", ui)
        self.assertIn("await wait('ejected'", ui)
        self.assertIn("if(token)await api('release',{token})", ui)
        self.assertIn("setInterval(poll,3000)", ui)
        self.assertIn('data-no-dirty="1"', ui)

    def test_http_file_actions_require_explicit_drive_identity(self):
        code = (SRC/'WebUsbStorage.inc.h').read_text()
        self.assertIn('ofe_usb::matchesDrive(drive, instance)', code)
        self.assertIn('ofe_usb::selectDrive(generation, index, instance)', code)
        for start, end in (
            ('static void web_usb_command()', 'static void web_usb_begin()'),
            ('static void web_usb_begin()', 'static void web_usb_manage()'),
            ('static void web_usb_manage()', 'static void web_usb_upload()'),
        ):
            body = code[code.index(start):code.index(end)]
            with self.subTest(route=start):
                self.assertIn('web_usb_drive_matches()', body)
                self.assertIn('web_c6_number("generation", UINT32_MAX, generation)', body)

    def test_clients_are_ready_before_first_enumeration(self):
        code = (SRC/'MasterUsbStorage.cpp').read_text()
        worker = code[code.index('void worker(void*)'):code.index('void quoted(')]
        self.assertLess(worker.index('msc_host_install(&config)'), worker.index('xTaskCreatePinnedToCore(hostTask'))
        self.assertLess(worker.index('monitor::begin()'), worker.index('xTaskCreatePinnedToCore(hostTask'))
        self.assertIn('host.peripheral_map = 1U;', worker)

    def test_directory_actions_are_worker_owned_and_bounded(self):
        code = (SRC/'MasterUsbStorage.cpp').read_text()
        self.assertIn('Directory directories[MAX_DIRECTORIES]', code)
        self.assertIn('MAX_SCAN_ENTRIES', code)
        self.assertIn('SCAN_TIMEOUT_MS', code)
        self.assertIn('pathDepth(relative) > MAX_DEPTH', code)
        self.assertIn('if (finish) state->phase = Phase::Reading', code)
        self.assertIn('manageSelected(action)', code)
        self.assertIn('state->completedOperation = operation', code)
        self.assertIn('!stat(target, &st) || errno != ENOENT', code)
        self.assertIn('rmdir(source)', code)
        self.assertIn('strcmp(item->d_name, ".") && strcmp(item->d_name, "..")', code)
        self.assertNotIn('remove_all', code)
        self.assertNotIn('system(', code)

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
