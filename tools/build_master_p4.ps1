param(
  [switch]$NativeUsb,
  [ValidateRange(1, 4)][int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cli = Join-Path $env:ProgramFiles 'Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'
if (!(Test-Path -LiteralPath $cli)) { $cli = (Get-Command arduino-cli -ErrorAction Stop).Source }
$python = (Get-Command python -ErrorAction Stop).Source
$ideConfig = Join-Path $env:USERPROFILE '.arduinoIDE/arduino-cli.yaml'
$configArgs = if (Test-Path -LiteralPath $ideConfig) { @('--config-file', $ideConfig) } else { @() }
& $python (Join-Path $root 'tools/boards/prepare_master_p4.py')
if ($LASTEXITCODE -ne 0) { throw 'P4 master generation failed; review locally edited generated files.' }
$cdc = if ($NativeUsb) { 'cdc' } else { 'default' }
$fqbn = "esp32:esp32:esp32p4:ChipVariant=prev3,PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=$cdc,FlashSize=16M,PartitionScheme=custom"
$profile = if ($NativeUsb) { 'master-p4-prev3-usb' } else { 'master-p4-prev3' }
$build = Join-Path $root "tools/tests/build-checks/$profile"
& $cli compile @configArgs --fqbn $fqbn --jobs $Jobs --build-property upload.maximum_size=5242880 --build-path $build --export-binaries (Join-Path $root 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4')
if ($LASTEXITCODE -ne 0) { throw 'P4 master build failed.' }
$binary = Join-Path $build 'OpenFumeExtractorMCU_P4.ino.bin'
if (!(Test-Path -LiteralPath $binary) -or (Get-Item -LiteralPath $binary).Length -gt 5242880) {
  throw 'Firmware missing or larger than the actual OTA app slot.'
}
Write-Output "Built, NOT flashed: $binary"
