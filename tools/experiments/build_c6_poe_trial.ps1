param([switch]$NativeUsb, [ValidateRange(1, 4)][int]$Jobs = 2)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$cli = Join-Path $env:ProgramFiles 'Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'
if (!(Test-Path -LiteralPath $cli)) { $cli = (Get-Command arduino-cli -ErrorAction Stop).Source }
$config = Join-Path $env:USERPROFILE '.arduinoIDE/arduino-cli.yaml'
$configArgs = if (Test-Path -LiteralPath $config) { @('--config-file', $config) } else { @() }
$cdc = if ($NativeUsb) { 'cdc' } else { 'default' }
$fqbn = "esp32:esp32:esp32p4:ChipVariant=postv3,PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=$cdc,FlashSize=32M,PartitionScheme=custom"
$build = Join-Path $root "tools/tests/build-checks/c6-poe-2131-trial-$cdc"
$name = 'C6_PoE_2131_Trial'
$sketch = Join-Path $PSScriptRoot $name
$emptySketchbook = Join-Path $root 'tools/tests/build-checks/c6-trial-sketchbook'
New-Item -ItemType Directory -Force -Path $emptySketchbook | Out-Null
$boot = '--chip {build.mcu} elf2image --min-rev-full 301 --max-rev-full 399 --flash-mode {build.flash_mode} --flash-freq {build.img_freq} --flash-size {build.flash_size} -o'
$app = '--chip {build.mcu} elf2image --min-rev-full 301 --max-rev-full 399 --flash-mode "{build.flash_mode}" --flash-freq "{build.img_freq}" --flash-size "{build.flash_size}" --elf-sha256-offset 0xb0 -o "{build.path}/{build.project_name}.bin" "{build.path}/{build.project_name}.elf"'
$previousSketchbook = $env:ARDUINO_DIRECTORIES_USER
try {
  # This sketch needs only stock platform libraries, never local SDK profiles.
  $env:ARDUINO_DIRECTORIES_USER = $emptySketchbook
  & $cli compile @configArgs --fqbn $fqbn --jobs $Jobs --build-path $build `
    --build-property upload.maximum_size=5242880 `
    --build-property "recipe.hooks.prebuild.4.pattern_args=$boot" `
    --build-property "recipe.objcopy.bin.pattern_args=$app" $sketch
  if ($LASTEXITCODE -ne 0) { throw 'C6 PoE trial build failed.' }
} finally { $env:ARDUINO_DIRECTORIES_USER = $previousSketchbook }
foreach ($file in @("$name.ino.bin", "$name.ino.bootloader.bin")) {
  $path = Join-Path $build $file
  $bytes = [IO.File]::ReadAllBytes($path)
  if ($bytes.Length -lt 24 -or $bytes[0] -ne 0xE9 -or [BitConverter]::ToUInt16($bytes,12) -ne 18 -or
      [BitConverter]::ToUInt16($bytes,15) -ne 301 -or [BitConverter]::ToUInt16($bytes,17) -ne 399) {
    throw "Wrong trial image chip/revision bounds: $file"
  }
}
Write-Output "Compiled and revision-checked; NOT flashed. Host SDK unchanged. Trial app: $build/$name.ino.bin"
