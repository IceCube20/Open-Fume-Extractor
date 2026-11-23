param([ValidateRange(1,4)][int]$Jobs = 2)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $PSScriptRoot 'prepare_community_mcu.ps1') -ProjectRoot $root
$cli = Join-Path $env:ProgramFiles 'Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'
$config = Join-Path $env:USERPROFILE '.arduinoIDE/arduino-cli.yaml'
$configArgs = if (Test-Path -LiteralPath $config) { @('--config-file', $config) } else { @() }
$buildRoot = Join-Path $root 'tools/tests/build-checks/community-release'
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
foreach ($name in @('OpenFumeExtractorMCU_S3', 'OpenFumeExtractorMCU_P4', 'OpenFumeExtractorMCU_P4_POE')) {
  $sketch = Join-Path $root "tools/tests/build-checks/community-mcu/$name"
  $properties = @()
  if ($name -eq 'OpenFumeExtractorMCU_S3') {
    $fqbn = 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,PSRAM=opi,LoopCore=1,EventsCore=1'
    $properties += @('--build-property', 'upload.maximum_size=3342336')
  } else {
    $poe = $name -eq 'OpenFumeExtractorMCU_P4_POE'
    $variant = if ($poe) { 'postv3' } else { 'prev3' }
    $flash = if ($poe) { '32M' } else { '16M' }
    $minimum = if ($poe) { 301 } else { 1 }
    $maximum = if ($poe) { 399 } else { 199 }
    $fqbn = "esp32:esp32:esp32p4:ChipVariant=$variant,PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=default,FlashSize=$flash,PartitionScheme=custom"
    $revision = "--min-rev-full $minimum --max-rev-full $maximum"
    $boot = '--chip {build.mcu} elf2image ' + $revision + ' --flash-mode {build.flash_mode} --flash-freq {build.img_freq} --flash-size {build.flash_size} -o'
    $app = '--chip {build.mcu} elf2image ' + $revision + ' --flash-mode "{build.flash_mode}" --flash-freq "{build.img_freq}" --flash-size "{build.flash_size}" --elf-sha256-offset 0xb0 -o "{build.path}/{build.project_name}.bin" "{build.path}/{build.project_name}.elf"'
    $properties += @('--build-property', 'upload.maximum_size=5242880', '--build-property', "recipe.hooks.prebuild.4.pattern_args=$boot", '--build-property', "recipe.objcopy.bin.pattern_args=$app")
  }
  $log = Join-Path $buildRoot "$name.log"
  Write-Output "BUILD: community $name; no upload"
  & $cli compile @configArgs --fqbn $fqbn --jobs $Jobs @properties --build-path (Join-Path $buildRoot $name) --export-binaries $sketch *> $log
  if ($LASTEXITCODE) { Get-Content $log -Tail 35; throw "Community build failed: $name" }
  $binary = Join-Path $buildRoot "$name/$name.ino.bin"
  $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($binary))
  if ($text.Contains('Wrong developer password') -or $text.Contains('Falsches Entwicklerpasswort') -or $text -match 'OFE_FW_SIG:v1;target=MASTER;version=[^;]*Developer') {
    throw "Private developer functionality detected: $name"
  }
  Write-Output "PASS: community $name, exported; NOT flashed"
}
