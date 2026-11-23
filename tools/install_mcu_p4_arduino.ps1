param(
  [string]$Sketchbook = "$env:USERPROFILE\Documents\Arduino",
  [string]$Runtime = (Join-Path $Sketchbook 'hardware\ofemcu\esp32'),
  [string]$ArduinoData = "$env:LOCALAPPDATA\Arduino15",
  [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$runtime = [IO.Path]::GetFullPath($Runtime)
$platform = $runtime
$core = [IO.Path]::GetFullPath((Join-Path $ArduinoData 'packages\esp32\hardware\esp32\3.3.12'))
$tools = Join-Path $ArduinoData 'packages\esp32\tools'
$destination = [IO.Path]::GetFullPath((Join-Path $Sketchbook 'hardware\ofemcu\esp32'))
$utf8 = [Text.UTF8Encoding]::new($false)
$profile = 'ofe-mcu-p4-stock-3.3.12-revision-bounds-v1'

function Assert-Junction([string]$Path, [string]$Target) {
  $existing = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
  if ($existing -and ($existing.LinkType -ne 'Junction' -or
      [IO.Path]::GetFullPath([string]@($existing.Target)[0]) -ne [IO.Path]::GetFullPath($Target))) {
    throw "Existing path is not the expected OFE junction: $Path"
  }
}

if ($runtime.StartsWith($core, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'The OFE runtime must not replace the installed ESP32 platform.'
}
$platformText = Get-Content -LiteralPath (Join-Path $core 'platform.txt') -Raw
if ($platformText -notmatch '(?m)^version=3\.3\.12\s*$') { throw 'Arduino-ESP32 3.3.12 required.' }
$sdkPaths = [ordered]@{
  'ofe.sdk.prev3' = (Join-Path $tools 'esp32p4_es-libs\3.3.12')
  'ofe.sdk.v3' = (Join-Path $tools 'esp32p4-libs\3.3.12')
}
foreach ($key in $sdkPaths.Keys) {
  $cfg = Get-Content -LiteralPath (Join-Path $sdkPaths[$key] 'qio_qspi\include\sdkconfig.h') -Raw
  $minimum = [int][regex]::Match($cfg,'(?m)^#define CONFIG_ESP32P4_REV_MIN_FULL (\d+)\s*$').Groups[1].Value
  $maximum = [int][regex]::Match($cfg,'(?m)^#define CONFIG_ESP32P4_REV_MAX_FULL (\d+)\s*$').Groups[1].Value
  if (($key -eq 'ofe.sdk.prev3' -and ($minimum -ne 1 -or $maximum -ne 199)) -or
      ($key -eq 'ofe.sdk.v3' -and ($minimum -ne 301 -or $maximum -ne 399))) {
    throw "Unexpected SDK revision bounds: $key $minimum..$maximum"
  }
}
$toolPaths = [ordered]@{
  'tools.riscv32-esp-elf-gcc.path' = (Join-Path $tools 'esp-rv32\2601')
  'tools.esptool_py.path' = (Join-Path $tools 'esptool_py\5.3.1')
}
foreach ($path in $toolPaths.Values) {
  if (!(Test-Path -LiteralPath $path -PathType Container)) { throw "Missing tool: $path" }
}
$marker = Join-Path $runtime 'ofe-mcu-p4-install.json'
if (Test-Path -LiteralPath $runtime) {
  if (!(Test-Path -LiteralPath $marker -PathType Leaf)) { throw "Unowned runtime exists: $runtime" }
  $previous = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
  if ($previous.profile -ne $profile) { throw 'Wrong runtime ownership marker.' }
}
if ($destination -ne $platform) { Assert-Junction $destination $platform }
foreach ($name in @('cores','variants','libraries','tools')) {
  Assert-Junction (Join-Path $platform $name) (Join-Path $core $name)
}
New-Item -ItemType Directory -Force -Path $platform | Out-Null
foreach ($name in @('cores','variants','libraries','tools')) {
  $link = Join-Path $platform $name
  if (!(Test-Path -LiteralPath $link)) {
    New-Item -ItemType Junction -Path $link -Target (Join-Path $core $name) | Out-Null
  }
}
Copy-Item -LiteralPath (Join-Path $core 'platform.txt') -Destination $platform -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'boards\mcu_p4\boards.txt') -Destination $platform -Force
$local = @('name=OFE MCU P4','version=3.3.12',
  'compiler.sdk.path={ofe.sdk.path}',
  'tools.esp32-arduino-libs.path={ofe.sdk.path}',
  'tools.esp32-arduino-libs.path.windows={ofe.sdk.path}',
  'recipe.hooks.prebuild.4.pattern_args=--chip {build.mcu} elf2image --min-rev-full {build.min_revision} --max-rev-full {build.max_revision} --flash-mode {build.flash_mode} --flash-freq {build.img_freq} --flash-size {build.flash_size} -o',
  'recipe.objcopy.bin.pattern_args=--chip {build.mcu} elf2image --min-rev-full {build.min_revision} --max-rev-full {build.max_revision} --flash-mode "{build.flash_mode}" --flash-freq "{build.img_freq}" --flash-size "{build.flash_size}" --elf-sha256-offset 0xb0 -o "{build.path}/{build.project_name}.bin" "{build.path}/{build.project_name}.elf"')
foreach ($key in $sdkPaths.Keys) { $local += "$key=$($sdkPaths[$key])" }
foreach ($key in $toolPaths.Keys) { $local += "$key=$($toolPaths[$key])" }
[IO.File]::WriteAllLines((Join-Path $platform 'platform.local.txt'),$local,$utf8)
[IO.File]::WriteAllText($marker,([ordered]@{profile=$profile;core=$core} | ConvertTo-Json),$utf8)
if (!$PrepareOnly) {
  New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
  if ($destination -ne $platform -and !(Test-Path -LiteralPath $destination)) {
    New-Item -ItemType Junction -Path $destination -Target $platform | Out-Null
  }
}
Write-Output "Prepared platform: $platform"
Write-Output 'Board: OFE MCU P4 > OFE MCU-P4 PoE (Waveshare WIFI6-POE-ETH, V3.1+, 32 MB)'
Write-Output 'PoE FQBN: ofemcu:esp32:ofemcu_p4_poe; PRE-V3 FQBN: ofemcu:esp32:ofemcu_p4'
Write-Output 'Restart Arduino IDE. Revision, SDK, PSRAM and flash size are fixed per board.'
Write-Output 'Use the matching MCU sketch with its generated partitions.csv. No device flashed; stock core and display profiles unchanged.'
