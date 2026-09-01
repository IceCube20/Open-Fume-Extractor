param(
  [string]$ArduinoCli = 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
  [string]$UserLibraries = "$env:USERPROFILE\Documents\Arduino",
  [string]$Runtime = "$env:USERPROFILE\Documents\Arduino\ofe-high-performance-sdk"
)
$ErrorActionPreference = 'Stop'

$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sketch = Join-Path $project 'Module\DisplayModule_800x480'
$runtime = [IO.Path]::GetFullPath($Runtime)
$data = Join-Path $runtime 'data'
$downloads = Join-Path $runtime 'downloads'
$build = Join-Path $runtime 'build'
$sdkZip = Join-Path $downloads 'esp32-3.2.0-h.zip'
$coreZip = Join-Path $downloads 'esp32-3.2.0.zip'
$config = Join-Path $runtime 'arduino-cli.yaml'
$python = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'

New-Item -ItemType Directory -Force -Path $data,$downloads,$build | Out-Null
@"
directories:
  data: "$($data -replace '\\','/')"
  downloads: "$($downloads -replace '\\','/')"
  user: "$($UserLibraries -replace '\\','/')"
"@ | Set-Content -LiteralPath $config -Encoding utf8

if (!(Test-Path $ArduinoCli)) { throw "arduino-cli not found: $ArduinoCli" }
if (!(Test-Path $python)) { throw "Bundled Python runtime not found: $python" }

function Get-VerifiedDownload([string]$Url, [string]$Target, [string]$Sha256) {
  if ((Test-Path $Target) -and ((Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -eq $Sha256)) { return }
  Remove-Item -LiteralPath $Target -Force -ErrorAction SilentlyContinue
  & $python -c @"
import hashlib, pathlib, urllib.request
url = r'''$Url'''
target = pathlib.Path(r'''$Target''')
tmp = target.with_suffix('.download')
with urllib.request.urlopen(url, timeout=120) as source, tmp.open('wb') as out:
    digest = hashlib.sha256()
    while True:
        block = source.read(1024 * 1024)
        if not block: break
        out.write(block)
        digest.update(block)
if digest.hexdigest().upper() != r'''$Sha256''':
    tmp.unlink(missing_ok=True)
    raise SystemExit('SHA-256 mismatch')
tmp.replace(target)
"@
  if ($LASTEXITCODE) { throw "Download failed: $Url" }
}

function Remove-GeneratedDirectory([string]$Path) {
  if (!(Test-Path -LiteralPath $Path)) { return }
  $resolved = (Resolve-Path -LiteralPath $Path).Path
  $allowed = [IO.Path]::GetFullPath($runtime).TrimEnd('\') + '\'
  if (!$resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -or
      ((Get-Item -LiteralPath $resolved).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw "Refusing to remove a directory outside the generated runtime: $resolved"
  }
  Remove-Item -LiteralPath $resolved -Recurse -Force
}

Get-VerifiedDownload 'https://github.com/espressif/arduino-esp32/releases/download/3.2.0/esp32-3.2.0.zip' `
  $coreZip 'D38B16FEF6E519FC0D19BC5AF0B39CDBED7DFC2CE69214C1971DED0E61ECD911'
# Pin the downloaded official SDK archive so changed or partial content is rejected.
Get-VerifiedDownload 'https://dl.espressif.com/AE/esp-arduino-libs/esp32-3.2.0-h.zip' `
  $sdkZip '3D6A0CA36C644B1AD1B3B1C405423C7E2FCD947AE8BD2A733D54289C7D540B07'

$core = Join-Path $data 'packages\esp32\hardware\esp32\3.2.0'
$tools = Join-Path $data 'packages\esp32\tools'
$coreExpanded = Join-Path $runtime 'core-expanded'
if (!(Test-Path (Join-Path $core 'platform.txt'))) {
  Remove-GeneratedDirectory $coreExpanded
  Expand-Archive -LiteralPath $coreZip -DestinationPath $coreExpanded
  $coreSource = Get-ChildItem -LiteralPath $coreExpanded -Filter platform.txt -Recurse |
    Select-Object -First 1 -ExpandProperty DirectoryName
  if (!$coreSource) { throw 'Unexpected Arduino core archive layout' }
  New-Item -ItemType Directory -Force -Path $core | Out-Null
  Copy-Item -Path (Join-Path $coreSource '*') -Destination $core -Recurse -Force
}

# Reuse the already installed compilers. The core requests older package labels,
# but both are GCC 14.2; flashing/debug helpers are linked only for upload/debug.
$globalTools = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools'
function Link-Tool([string]$Name, [string]$Expected, [string]$Available) {
  $dst = Join-Path $tools "$Name\$Expected"
  if (Test-Path $dst) { return }
  $src = Join-Path $globalTools "$Name\$Available"
  if (!(Test-Path $src)) { throw "Required shared Arduino tool missing: $src" }
  New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
  New-Item -ItemType Junction -Path $dst -Target $src | Out-Null
}
Link-Tool 'esp-rv32' '2411' '2601'
Link-Tool 'esp-x32' '2411' '2601'
Link-Tool 'esptool_py' '4.9.dev3' '5.3.1'
Link-Tool 'mklittlefs' '3.0.0-gnu12-dc7f933' '3.0.0-gnu12-dc7f933'
Link-Tool 'mkspiffs' '0.2.3' '0.2.3'
Link-Tool 'openocd-esp32' 'v0.12.0-esp32-20241016' 'v0.12.0-esp32-20260424'
Link-Tool 'riscv32-esp-elf-gdb' '14.2_20240403' '17.1_20260402'
Link-Tool 'xtensa-esp-elf-gdb' '14.2_20240403' '17.1_20260402'

$archive = Join-Path $runtime 'sdk-expanded'
if (!(Test-Path (Join-Path $archive '.ready'))) {
  Remove-GeneratedDirectory $archive
  Expand-Archive -LiteralPath $sdkZip -DestinationPath $archive
  New-Item -ItemType File -Path (Join-Path $archive '.ready') | Out-Null
}

# The official archive replaces only matching precompiled SDK libraries. Keep
# the isolated Arduino installation and every globally installed core untouched.
$sdkRoot = Get-ChildItem -LiteralPath $archive -Directory -Recurse |
  Where-Object { Test-Path (Join-Path $_.FullName 'esp32s3') } |
  Select-Object -First 1 -ExpandProperty FullName
if (!$sdkRoot) { throw 'Unexpected high-performance SDK archive layout' }
$installedSdk = Join-Path $tools 'esp32-arduino-libs\idf-release_v5.4-2f7dcd86-v1'
New-Item -ItemType Directory -Force -Path $installedSdk | Out-Null
Copy-Item -Path (Join-Path $sdkRoot '*') -Destination $installedSdk -Recurse -Force

$sdkconfig = Join-Path $installedSdk 'esp32s3\sdkconfig'
$memoryConfig = Join-Path $installedSdk 'esp32s3\qio_opi\include\sdkconfig.h'
if (!(Test-Path $sdkconfig)) { throw 'High-performance ESP32-S3 sdkconfig not found' }
if (!(Test-Path $memoryConfig)) { throw 'High-performance ESP32-S3 QIO/OPI config not found' }
$cfg = Get-Content -LiteralPath $sdkconfig -Raw
$memoryCfg = Get-Content -LiteralPath $memoryConfig -Raw
if (!$cfg.Contains('CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y')) {
  throw 'High-performance SDK verification failed: CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y'
}
foreach ($required in @('#define CONFIG_ESP32S3_DATA_CACHE_LINE_64B 1','#define CONFIG_SPIRAM_XIP_FROM_PSRAM 1')) {
  if (!$memoryCfg.Contains($required)) { throw "High-performance QIO/OPI verification failed: $required" }
}

$fqbn = 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=default_8MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1'
& $ArduinoCli compile --config-file $config --fqbn $fqbn --build-path $build --jobs 2 --export-binaries $sketch
if ($LASTEXITCODE) { throw 'High-performance display build failed' }

$builtConfig = Join-Path $build 'sdkconfig'
$built = Get-Content -LiteralPath $builtConfig -Raw
if (!$built.Contains('CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y')) {
  throw 'Built firmware used the wrong SDK: CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y'
}
$binary = Join-Path $build 'DisplayModule_800x480.ino.bin'
if (!(Test-Path $binary)) { throw "Compiled firmware binary not found: $binary" }
$binaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($binary))
foreach ($required in @('RGB diagnostic: ESP32-S3 data cache line = 64 B', 'RGB diagnostic: PSRAM XIP enabled', 'OFE_FW_SIG:v1;target=DISPLAY_800X480;')) {
  if (!$binaryText.Contains($required)) { throw "Compiled firmware verification failed: $required" }
}
Write-Host 'OK: Display 800x480 built with 64-byte DCache and PSRAM XIP.' -ForegroundColor Green
$signature = [regex]::Match($binaryText, 'OFE_FW_SIG:v1;target=DISPLAY_800X480;version=([0-9]+\.[0-9]+\.[0-9]+[a-zA-Z0-9_-]*);')
if (!$signature.Success) { throw 'Versioned display firmware signature missing' }
$output = Join-Path $project 'firmware-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$delivery = Join-Path $output ("Display_800x480_" + $signature.Groups[1].Value + '_high-perf.bin')
Copy-Item -LiteralPath $binary -Destination $delivery -Force
Write-Host "Binary: $delivery"
Write-Host 'OFE version marker verified. Cryptographic signing is a separate step.'
