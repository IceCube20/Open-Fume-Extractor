param(
  [string]$Sketchbook = "$env:USERPROFILE\Documents\Arduino",
  [string]$BaseRuntime = "$env:USERPROFILE\Documents\Arduino\ofe-high-performance-sdk",
  [string]$CandidateWorkspace = 'C:\esp\ofe-s3-3.2.0-wifi-sram'
)
$ErrorActionPreference = 'Stop'

$candidateSdk = Join-Path $CandidateWorkspace 'builder\out\tools\esp32-arduino-libs'
$candidatePlatform = Join-Path $CandidateWorkspace 'arduino-candidate-r5\ide-platform'
$basePlatform = Join-Path $BaseRuntime 'ide-platform'
$baseTools = Join-Path $BaseRuntime 'data\packages\esp32\tools'
$boardLink = Join-Path $Sketchbook 'hardware\ofe_sram\esp32'

if (!(Test-Path (Join-Path $candidateSdk 'esp32s3\qio_opi\include\sdkconfig.h'))) {
  throw "Candidate SDK export is missing: $candidateSdk"
}
if (!(Test-Path (Join-Path $basePlatform 'platform.txt'))) {
  throw "Base high-performance Arduino platform is missing: $basePlatform"
}
$config = Get-Content (Join-Path $candidateSdk 'esp32s3\qio_opi\include\sdkconfig.h') -Raw
if ($config -match '(?m)^#define CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP 1$') {
  throw 'The candidate qio_opi configuration does not contain the expected WiFi/lwIP SRAM preference.'
}

$existingLink = Get-Item -LiteralPath $boardLink -Force -ErrorAction SilentlyContinue
if ($existingLink -and ($existingLink.LinkType -ne 'Junction' -or
    [IO.Path]::GetFullPath([string]@($existingLink.Target)[0]) -ne [IO.Path]::GetFullPath($candidatePlatform))) {
  throw "Preserving existing board path: $boardLink"
}
if (!(Test-Path -LiteralPath $candidatePlatform)) {
  New-Item -ItemType Directory -Path $candidatePlatform -Force | Out-Null
  Copy-Item -Path (Join-Path $basePlatform '*') -Destination $candidatePlatform -Recurse -Force
}

$boardText = Get-Content (Join-Path $PSScriptRoot '..\..\arduino_high_perf\boards.txt') -Raw
$boardText = $boardText.Replace('ofe800.', 'ofe800sram.')
$boardText = $boardText.Replace('OFE Display 800x480 (High Performance)', 'OFE Display 800x480 (WiFi SRAM Test)')
[IO.File]::WriteAllText((Join-Path $candidatePlatform 'boards.txt'), $boardText, [Text.UTF8Encoding]::new($false))

$toolchain = Join-Path $baseTools 'esp-x32\2411'
$esptool = Join-Path $baseTools 'esptool_py\4.9.dev3'
foreach ($tool in @($toolchain, $esptool)) {
  if (!(Test-Path -LiteralPath $tool)) { throw "Required Arduino build tool is missing: $tool" }
}
$local = @(
  'name=OFE RGB WiFi SRAM Candidate',
  'version=3.2.0-sram-test',
  "tools.esp32-arduino-libs.path=$candidateSdk",
  "tools.xtensa-esp-elf-gcc.path=$toolchain",
  "tools.esptool_py.path=$esptool"
)
[IO.File]::WriteAllLines((Join-Path $candidatePlatform 'platform.local.txt'), $local, [Text.UTF8Encoding]::new($false))

if (!$existingLink) {
  New-Item -ItemType Directory -Force -Path (Split-Path $boardLink) | Out-Null
  New-Item -ItemType Junction -Path $boardLink -Target $candidatePlatform | Out-Null
}
Write-Host 'Registered board: OFE RGB WiFi SRAM Candidate > OFE Display 800x480 (WiFi SRAM Test)'
Write-Host 'FQBN: ofe_sram:esp32:ofe800sram'
Write-Host "Candidate platform: $candidatePlatform"
