$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$esp = "$env:LOCALAPPDATA/Arduino15/packages/esp32"
$sdk = "$esp/tools/esp32p4_es-libs/3.3.12"
$core = "$esp/hardware/esp32/3.3.12"
$tool = "$esp/tools/esp-rv32/2601/bin/riscv32-esp-elf-"
$source = "$root/OpenFumeExtractorMaster"
$common = @('-fsyntax-only', '-DF_CPU=360000000L', '-DARDUINO=10607', '-DESP32=1', '-DARDUINO_ARCH_ESP32',
  '-DARDUINO_USB_MODE=1', '-DARDUINO_USB_CDC_ON_BOOT=0', '-DBOARD_HAS_PSRAM',
  "@$sdk/flags/defines", '-iprefix', "$sdk/include/", "@$sdk/flags/includes", "-I$sdk/qio_qspi/include",
  "-I$core/cores/esp32", "-I$core/variants/esp32p4", "-I$source", "-I$source/src",
  "-I$core/libraries/Network/src", "-I$core/libraries/FS/src", "-I$core/libraries/WebServer/src")
& "${tool}g++.exe" "@$sdk/flags/cpp_flags" @common "$source/src/MasterUsbStorage.cpp"
if ($LASTEXITCODE -ne 0) { throw 'USB storage C++ SDK check failed' }
& "${tool}g++.exe" "@$sdk/flags/cpp_flags" @common "$source/src/MasterUsbMonitor.cpp"
if ($LASTEXITCODE -ne 0) { throw 'USB observer C++ SDK check failed' }
foreach ($name in @('msc_host.c', 'msc_host_vfs.c', 'msc_scsi_bot.c', 'diskio_usb.c')) {
  & "${tool}gcc.exe" "@$sdk/flags/c_flags" @common "$source/src/usb_msc/$name"
  if ($LASTEXITCODE -ne 0) { throw "USB MSC SDK check failed: $name" }
}
Write-Output 'PASS: actual Arduino 3.3.12 / IDF 5.5.5 P4 USB backend and all four upstream MSC C sources'
