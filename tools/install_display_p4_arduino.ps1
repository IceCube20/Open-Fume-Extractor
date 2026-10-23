param(
  [string]$Sketchbook = "$env:USERPROFILE\Documents\Arduino",
  [string]$Runtime = "$env:USERPROFILE\Documents\Arduino\ofe-p4-high-performance",
  [string]$Sdk = 'C:\esp\ofe-p4-3.3.12\builder\out\tools\esp32-arduino-libs\esp32p4_es',
  [string]$ArduinoData = "$env:LOCALAPPDATA\Arduino15",
  [string]$Python = 'C:\esp\ofe-p4-3.3.12\python\Scripts\python.exe',
  [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$profile = Join-Path $PSScriptRoot 'boards\p4_high_perf_sdk'
$core = Join-Path $ArduinoData 'packages\esp32\hardware\esp32\3.3.12'
$tools = Join-Path $ArduinoData 'packages\esp32\tools'
$baseline = Join-Path $tools 'esp32p4_es-libs\3.3.12'
$runtime = [IO.Path]::GetFullPath($Runtime)
$platform = Join-Path $runtime 'ide-platform'
$installedSdk = Join-Path $runtime 'sdk\esp32p4_es'
$destination = [IO.Path]::GetFullPath((Join-Path $Sketchbook 'hardware\ofep4\esp32'))
$utf8 = [Text.UTF8Encoding]::new($false)

function Assert-ChildPath([string]$ParentPath, [string]$ChildPath) {
  $parent = [IO.Path]::GetFullPath($ParentPath).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
  if (![IO.Path]::GetFullPath($ChildPath).StartsWith($parent, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Path is outside the owned runtime: $ChildPath"
  }
}

function Assert-Junction([string]$Path, [string]$Target) {
  $existing = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
  if ($existing -and ($existing.LinkType -ne 'Junction' -or
      [IO.Path]::GetFullPath([string]@($existing.Target)[0]) -ne [IO.Path]::GetFullPath($Target))) {
    throw "Existing path is not the expected OFE junction: $Path"
  }
}

foreach ($file in @((Join-Path $core 'platform.txt'), (Join-Path $profile 'boards.txt'), $Python)) {
  if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing prerequisite: $file" }
}
foreach ($directory in @($Sdk, $baseline)) {
  if (!(Test-Path -LiteralPath $directory -PathType Container)) { throw "Missing SDK: $directory" }
}
foreach ($child in @($platform, $installedSdk)) { Assert-ChildPath $runtime $child }
Assert-ChildPath ([IO.Path]::GetFullPath($Sketchbook)) $runtime
Assert-ChildPath ([IO.Path]::GetFullPath($Sketchbook)) $destination
if ($runtime.StartsWith([IO.Path]::GetFullPath($core), [StringComparison]::OrdinalIgnoreCase)) {
  throw 'The runtime must not replace an installed ESP32 platform.'
}
Assert-Junction $destination $platform

$marker = Join-Path $runtime 'ofe-p4-install.json'
if (Test-Path -LiteralPath $runtime) {
  if (!(Test-Path -LiteralPath $marker -PathType Leaf)) { throw "Unowned runtime exists: $runtime" }
  $previous = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
  if ($previous.profile -ne 'ofe-p4-cache256-prev3-3.3.12') { throw 'Wrong runtime ownership marker.' }
}
foreach ($name in @('cores', 'variants', 'libraries', 'tools')) {
  Assert-Junction (Join-Path $platform $name) (Join-Path $core $name)
}

$toolPaths = [ordered]@{
  'tools.riscv32-esp-elf-gcc.path' = (Join-Path $tools 'esp-rv32\2601')
  'tools.esptool_py.path' = (Join-Path $tools 'esptool_py\5.3.1')
}
foreach ($path in $toolPaths.Values) {
  if (!(Test-Path -LiteralPath $path -PathType Container)) { throw "Missing tool: $path" }
}
$platformText = Get-Content -LiteralPath (Join-Path $core 'platform.txt') -Raw
if ($platformText -notmatch '(?m)^version=3\.3\.12\s*$') { throw 'Arduino core 3.3.12 required.' }

& $Python (Join-Path $profile 'prepare.py') check-sdk --sdk $Sdk --baseline-sdk $baseline
if ($LASTEXITCODE -ne 0) { throw 'Source SDK validation failed; nothing installed.' }

New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$metadata = [ordered]@{ profile = 'ofe-p4-cache256-prev3-3.3.12'; sdkSource = [IO.Path]::GetFullPath($Sdk); core = $core }
[IO.File]::WriteAllText($marker, ($metadata | ConvertTo-Json), $utf8)
if ([IO.Path]::GetFullPath($Sdk) -ne [IO.Path]::GetFullPath($installedSdk)) {
  if (Test-Path -LiteralPath $installedSdk) {
    & $Python (Join-Path $profile 'prepare.py') check-sdk --sdk $installedSdk --baseline-sdk $baseline
    if ($LASTEXITCODE -ne 0) { throw 'Existing runtime SDK differs; preserved for inspection.' }
  } else {
    New-Item -ItemType Directory -Force -Path (Split-Path $installedSdk) | Out-Null
    Copy-Item -LiteralPath $Sdk -Destination $installedSdk -Recurse
  }
}
& $Python (Join-Path $profile 'prepare.py') check-sdk --sdk $installedSdk --baseline-sdk $baseline
if ($LASTEXITCODE -ne 0) { throw 'Installed SDK validation failed; board not linked.' }

New-Item -ItemType Directory -Force -Path $platform | Out-Null
# Share the pinned core resources read-only in practice; never edit their targets.
foreach ($name in @('cores', 'variants', 'libraries', 'tools')) {
  $link = Join-Path $platform $name
  if (!(Test-Path -LiteralPath $link)) {
    New-Item -ItemType Junction -Path $link -Target (Join-Path $core $name) | Out-Null
  }
}
Copy-Item -LiteralPath (Join-Path $core 'platform.txt') -Destination $platform -Force
Copy-Item -LiteralPath (Join-Path $profile 'boards.txt') -Destination $platform -Force
$local = @('name=OFE P4 High Performance', 'version=3.3.12',
  "compiler.sdk.path=$installedSdk", "tools.esp32-arduino-libs.path=$installedSdk",
  "tools.esp32-arduino-libs.path.windows=$installedSdk")
foreach ($key in $toolPaths.Keys) { $local += "$key=$($toolPaths[$key])" }
[IO.File]::WriteAllLines((Join-Path $platform 'platform.local.txt'), $local, $utf8)
if (!$PrepareOnly) {
  New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
  if (!(Test-Path -LiteralPath $destination)) {
    New-Item -ItemType Junction -Path $destination -Target $platform | Out-Null
  }
}
Write-Host "Prepared: $platform"
Write-Host 'Board: OFE P4 High Performance > OFE Display P4 800x480 (High Performance, PRE-V3)'
Write-Host 'FQBN: ofep4:esp32:ofep4. Restart Arduino IDE to refresh its board list.'
Write-Host 'Fixed: PRE-V3, PSRAM, 360 MHz CPU, 16 MB flash, 256 KiB/128 B cache, existing OTA layout.'
Write-Host 'LVGL Renderer menu: Software, PPA fills, or PPA fills + images (experimental).'
Write-Host 'No device flashed; stock ESP32/S3 platforms unchanged. Keep ESP32 core 3.3.12 installed.'
