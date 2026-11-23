param(
  [switch]$NativeUsb,
  [switch]$Poe,
  [ValidateRange(1, 4)][int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cli = Join-Path $env:ProgramFiles 'Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'
if (!(Test-Path -LiteralPath $cli)) { $cli = (Get-Command arduino-cli -ErrorAction Stop).Source }
$python = (Get-Command python -ErrorAction Stop).Source
$ideConfig = Join-Path $env:USERPROFILE '.arduinoIDE/arduino-cli.yaml'
$configArgs = if (Test-Path -LiteralPath $ideConfig) { @('--config-file', $ideConfig) } else { @() }
$prepareArgs = @()
if ($Poe) { $prepareArgs += '--poe' }
& $python (Join-Path $root 'tools/boards/prepare_master_p4.py') @prepareArgs
if ($LASTEXITCODE -ne 0) { throw 'P4 master generation failed; review locally edited generated files.' }
$cdc = if ($NativeUsb) { 'cdc' } else { 'default' }
$variant = if ($Poe) { 'postv3' } else { 'prev3' }
$flash = if ($Poe) { '32M' } else { '16M' }
$fqbn = "esp32:esp32:esp32p4:ChipVariant=$variant,PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=$cdc,FlashSize=$flash,PartitionScheme=custom"
$profile = if ($Poe) { 'master-p4-poe-v3' } else { 'master-p4-prev3' }
if ($NativeUsb) { $profile += '-usb' }
$sketchName = if ($Poe) { 'OpenFumeExtractorMCU_P4_POE' } else { 'OpenFumeExtractorMCU_P4' }
$build = Join-Path $root "tools/tests/build-checks/$profile"
# Stock Arduino leaves image revisions unbounded despite the selected SDK family.
$minimum = if ($Poe) { 301 } else { 1 }
$maximum = if ($Poe) { 399 } else { 199 }
$revisionArgs = "--min-rev-full $minimum --max-rev-full $maximum"
$bootRecipe = '--chip {build.mcu} elf2image ' + $revisionArgs + ' --flash-mode {build.flash_mode} --flash-freq {build.img_freq} --flash-size {build.flash_size} -o'
$appRecipe = '--chip {build.mcu} elf2image ' + $revisionArgs + ' --flash-mode "{build.flash_mode}" --flash-freq "{build.img_freq}" --flash-size "{build.flash_size}" --elf-sha256-offset 0xb0 -o "{build.path}/{build.project_name}.bin" "{build.path}/{build.project_name}.elf"'
& $cli compile @configArgs --fqbn $fqbn --jobs $Jobs --build-property upload.maximum_size=5242880 `
  --build-property "recipe.hooks.prebuild.4.pattern_args=$bootRecipe" `
  --build-property "recipe.objcopy.bin.pattern_args=$appRecipe" `
  --build-path $build --export-binaries (Join-Path $root "OpenFumeExtractorMaster/$sketchName")
if ($LASTEXITCODE -ne 0) { throw 'P4 master build failed.' }
$binary = Join-Path $build "$sketchName.ino.bin"
if (!(Test-Path -LiteralPath $binary) -or (Get-Item -LiteralPath $binary).Length -gt 5242880) {
  throw 'Firmware missing or larger than the actual OTA app slot.'
}
foreach ($path in @($binary, (Join-Path $build "$sketchName.ino.bootloader.bin"))) {
  $bytes = [IO.File]::ReadAllBytes($path)
  if ($bytes.Length -lt 24 -or $bytes[0] -ne 0xE9 -or
      [BitConverter]::ToUInt16($bytes,12) -ne 18 -or
      [BitConverter]::ToUInt16($bytes,15) -ne $minimum -or
      [BitConverter]::ToUInt16($bytes,17) -ne $maximum) {
    throw "Missing or incorrect P4 image revision limits: $path"
  }
}
Write-Output "App and bootloader revisions verified: $minimum..$maximum"
Write-Output "Built, NOT flashed: $binary"
