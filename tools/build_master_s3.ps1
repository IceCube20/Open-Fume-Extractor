param([ValidateRange(1,4)][int]$Jobs = 2)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cli = Join-Path $env:ProgramFiles 'Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'
if (!(Test-Path -LiteralPath $cli)) { $cli = (Get-Command arduino-cli -ErrorAction Stop).Source }
$config = Join-Path $env:USERPROFILE '.arduinoIDE/arduino-cli.yaml'
$configArgs = if (Test-Path -LiteralPath $config) { @('--config-file', $config) } else { @() }
$sketch = Join-Path $root 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3'
$profile = Join-Path $sketch 'build/esp32.esp32.esp32s3/build.options.json'
$fqbn = 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,PSRAM=opi,LoopCore=1,EventsCore=1'
if (Test-Path -LiteralPath $profile) {
  $options = Get-Content -LiteralPath $profile -Raw | ConvertFrom-Json
  if (!$options.fqbn -or !$options.fqbn.StartsWith('esp32:esp32:esp32s3:')) { throw 'Unexpected S3 export profile.' }
  $fqbn = $options.fqbn
}
$build = Join-Path $root 'tools/tests/build-checks/master-s3-current'
& $cli compile @configArgs --fqbn $fqbn --jobs $Jobs --build-property upload.maximum_size=3342336 --build-path $build --export-binaries $sketch
if ($LASTEXITCODE -ne 0) { throw 'S3 MCU build failed.' }
$binary = Join-Path $build 'OpenFumeExtractorMCU_S3.ino.bin'
if (!(Test-Path -LiteralPath $binary) -or (Get-Item -LiteralPath $binary).Length -gt 3342336) {
  throw 'Firmware missing or larger than the S3 OTA app slot.'
}
Write-Output "Built, NOT flashed: $binary"
