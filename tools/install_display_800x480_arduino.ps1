param(
  [string]$Sketchbook = "$env:USERPROFILE\Documents\Arduino",
  [string]$Runtime = "$env:USERPROFILE\Documents\Arduino\ofe-high-performance-sdk",
  [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$runtime = [IO.Path]::GetFullPath($Runtime)
$core = Join-Path $runtime 'data\packages\esp32\hardware\esp32\3.2.0'
$tools = Join-Path $runtime 'data\packages\esp32\tools'
$sdk = Join-Path $tools 'esp32-arduino-libs\idf-release_v5.4-2f7dcd86-v1'
$platform = Join-Path $runtime 'ide-platform'
if (!(Test-Path -LiteralPath (Join-Path $core 'platform.txt'))) {
  throw 'Run build_display_800x480_high_perf.ps1 once to prepare the verified SDK/core.'
}
$cfg = Get-Content -LiteralPath (Join-Path $sdk 'esp32s3\qio_opi\include\sdkconfig.h') -Raw
foreach ($required in @('#define CONFIG_ESP32S3_DATA_CACHE_LINE_64B 1', '#define CONFIG_SPIRAM_XIP_FROM_PSRAM 1')) {
  if (!$cfg.Contains($required)) { throw "Wrong SDK configuration: $required" }
}

# Copy only core/platform resources. The large SDK stays in the isolated runtime.
New-Item -ItemType Directory -Force -Path $platform | Out-Null
foreach ($name in @('cores', 'variants', 'libraries', 'tools')) {
  Copy-Item -LiteralPath (Join-Path $core $name) -Destination $platform -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $core 'platform.txt') -Destination $platform -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'arduino_high_perf\boards.txt') -Destination $platform -Force

$toolPaths = [ordered]@{
  'tools.esp32-arduino-libs.path' = $sdk
  'tools.xtensa-esp-elf-gcc.path' = (Join-Path $tools 'esp-x32\2411')
  'tools.esptool_py.path' = (Join-Path $tools 'esptool_py\4.9.dev3')
}
$local = @('name=OFE RGB High Performance', 'version=3.2.0')
foreach ($key in $toolPaths.Keys) {
  if (!(Test-Path -LiteralPath $toolPaths[$key])) { throw "Tool not found: $($toolPaths[$key])" }
  # The core's Windows COPY hooks require native separators, unlike gcc.
  $local += "$key=$($toolPaths[$key])"
}
[IO.File]::WriteAllLines((Join-Path $platform 'platform.local.txt'), $local, [Text.UTF8Encoding]::new($false))
Write-Host "Prepared platform: $platform"
if ($PrepareOnly) { return }

# Only create our own board link. Never replace an existing unrelated platform.
$destination = [IO.Path]::GetFullPath((Join-Path $Sketchbook 'hardware\ofe\esp32'))
$existing = Get-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
if ($existing) {
  if ($existing.LinkType -ne 'Junction' -or
      [IO.Path]::GetFullPath([string]@($existing.Target)[0]) -ne [IO.Path]::GetFullPath($platform)) {
    throw "Destination already exists and is not our platform link: $destination"
  }
} else {
  New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
  New-Item -ItemType Junction -Path $destination -Target $platform | Out-Null
}
Write-Host 'Installed: OFE RGB High Performance > OFE Display 800x480 (High Performance)'
Write-Host "Restart Arduino IDE. FQBN: ofe:esp32:ofe800. Runtime: $runtime"
