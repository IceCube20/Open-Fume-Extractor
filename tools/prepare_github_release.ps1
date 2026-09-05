param(
  [string]$ProjectRoot = (Join-Path $PSScriptRoot '..'),
  [string]$Python = "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$release = Join-Path $root 'GitHub-Release'
$firmwareRoot = Join-Path $release 'firmware'
$docsRoot = Join-Path $release 'docs'
$flasherRoot = Join-Path $release 'flasher'
$manifestRoot = Join-Path $flasherRoot 'manifests'
$signer = Join-Path $root 'tools\ofe_firmware_sign.py'
$privateKey = Join-Path $root 'Open Fume Extractor Signing Key\ofe_ed25519_private.pem'
$publicKey = Join-Path $root 'Open Fume Extractor Signing Key\ofe_ed25519_public.pem'
$logo = Join-Path $root 'assets\IceCube20_96.png'

foreach ($required in @($Python, $signer, $privateKey, $publicKey, $docsRoot, $logo)) {
  if (!(Test-Path -LiteralPath $required)) { throw "Required release input missing: $required" }
}

$targets = @(
  @{ Name='OpenFumeExtractor-Master'; Label='Open Fume Extractor Master'; Target='MASTER'; Chip='ESP32-S3'; Flash='16 MB'; App='OpenFumeExtractorMaster\build\esp32.esp32.esp32s3\OpenFumeExtractorMaster.ino.bin'; Merged='OpenFumeExtractorMaster\build\esp32.esp32.esp32s3\OpenFumeExtractorMaster.ino.merged.bin' },
  @{ Name='JBC-FAE-Bus'; Label='JBC FAE Bus'; Target='JBC_BUS'; Chip='ESP32'; Flash='4 MB'; App='Module\JbcBusModule\build\esp32.esp32.esp32\JbcBusModule.ino.bin'; Merged='Module\JbcBusModule\build\esp32.esp32.esp32\JbcBusModule.ino.merged.bin' },
  @{ Name='JBC-USB'; Label='JBC USB'; Target='JBC_USB'; Chip='ESP32-S3'; Flash='16 MB'; App='Module\JbcUsbModule\build\esp32.esp32.esp32s3\JbcUsbModule.ino.bin'; Merged='Module\JbcUsbModule\build\esp32.esp32.esp32s3\JbcUsbModule.ino.merged.bin' },
  @{ Name='Fan-IO'; Label='Fan/IO'; Target='FAN_IO'; Chip='ESP32'; Flash='4 MB'; App='Module\FanIoModule\build\esp32.esp32.esp32\FanIoModule.ino.bin'; Merged='Module\FanIoModule\build\esp32.esp32.esp32\FanIoModule.ino.merged.bin' },
  @{ Name='Fan-IO-Pro'; Label='Fan/IO Pro'; Target='FAN_IO_PRO'; Chip='ESP32'; Flash='4 MB'; App='Module\FanIoProModule\build\esp32.esp32.esp32\FanIoProModule.ino.bin'; Merged='Module\FanIoProModule\build\esp32.esp32.esp32\FanIoProModule.ino.merged.bin' },
  @{ Name='Weller-Zero-Smog'; Label='Weller Zero Smog'; Target='WELLER_ZERO_SMOG'; Chip='ESP32'; Flash='4 MB'; App='Module\WellerZeroSmogModule\build\esp32.esp32.esp32\WellerZeroSmogModule.ino.bin'; Merged='Module\WellerZeroSmogModule\build\esp32.esp32.esp32\WellerZeroSmogModule.ino.merged.bin' },
  @{ Name='Display-320x480'; Label='Display 320x480'; Target='DISPLAY_320X480'; Chip='ESP32-S3'; Flash='16 MB'; App='Module\DisplayModule_320x480\build\esp32.esp32.esp32s3\DisplayModule_320x480.ino.bin'; Merged='Module\DisplayModule_320x480\build\esp32.esp32.esp32s3\DisplayModule_320x480.ino.merged.bin' },
  @{ Name='Display-800x480'; Label='Display 800x480'; Target='DISPLAY_800X480'; Chip='ESP32-S3'; Flash='16 MB'; App='Module\DisplayModule_800x480\build\ofe.esp32.ofe800\DisplayModule_800x480.ino.bin'; Merged='Module\DisplayModule_800x480\build\ofe.esp32.ofe800\DisplayModule_800x480.ino.merged.bin' },
  @{ Name='Universal-RS232'; Label='Universal RS232'; Target='UNIVERSAL_RS232'; Chip='ESP32'; Flash='4 MB'; App='Module\UniversalRs232Module\build\esp32.esp32.esp32\UniversalRs232Module.ino.bin'; Merged='Module\UniversalRs232Module\build\esp32.esp32.esp32\UniversalRs232Module.ino.merged.bin' },
  @{ Name='Modbus-RTU'; Label='Modbus RTU'; Target='MODBUS_RTU'; Chip='ESP32'; Flash='4 MB'; App='Module\ModbusRtuModule\build\esp32.esp32.esp32\ModbusRtuModule.ino.bin'; Merged='Module\ModbusRtuModule\build\esp32.esp32.esp32\ModbusRtuModule.ino.merged.bin' }
)

function Get-FirmwareInfo([string]$Path) {
  $bytes = [IO.File]::ReadAllBytes($Path)
  $text = [Text.Encoding]::ASCII.GetString($bytes)
  $match = [regex]::Match($text, 'OFE_FW_SIG:v1;target=([A-Z0-9_]+);version=([^;\x00-\x1F]+);')
  if (!$match.Success) { throw "OFE target/version marker missing: $Path" }
  return [pscustomobject]@{ Target=$match.Groups[1].Value; Version=$match.Groups[2].Value }
}

function Assert-MergedContainsApp([string]$AppPath, [string]$MergedPath) {
  $appBytes = [IO.File]::ReadAllBytes($AppPath)
  $mergedInfo = Get-Item -LiteralPath $MergedPath
  $appOffset = 0x10000
  if ($mergedInfo.Length -lt ($appOffset + $appBytes.Length)) {
    throw "Merged image is too small for app image: $MergedPath"
  }
  $embedded = New-Object byte[] $appBytes.Length
  $stream = [IO.File]::OpenRead($MergedPath)
  try {
    $stream.Position = $appOffset
    $read = 0
    while ($read -lt $embedded.Length) {
      $count = $stream.Read($embedded, $read, $embedded.Length - $read)
      if ($count -le 0) { throw "Unexpected end of merged image: $MergedPath" }
      $read += $count
    }
  } finally {
    $stream.Dispose()
  }
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    # Keep the release helper compatible with Windows PowerShell 5.1.
    $appHash = ([BitConverter]::ToString($sha.ComputeHash($appBytes))).Replace('-', '')
    $embeddedHash = ([BitConverter]::ToString($sha.ComputeHash($embedded))).Replace('-', '')
  } finally {
    $sha.Dispose()
  }
  if ($appHash -ne $embeddedHash) {
    throw "Merged image does not contain the selected app at 0x10000: $MergedPath"
  }
}

if (Test-Path -LiteralPath $firmwareRoot) {
  $resolvedRelease = [IO.Path]::GetFullPath($release).TrimEnd('\') + '\'
  $resolvedFirmware = [IO.Path]::GetFullPath($firmwareRoot)
  if (!$resolvedFirmware.StartsWith($resolvedRelease, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clear firmware outside release tree: $resolvedFirmware"
  }
  Remove-Item -LiteralPath $resolvedFirmware -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $firmwareRoot | Out-Null
New-Item -ItemType Directory -Force -Path $manifestRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $flasherRoot 'assets') | Out-Null
Copy-Item -LiteralPath $logo -Destination (Join-Path $flasherRoot 'assets\IceCube20_96.png') -Force

$rows = @()
foreach ($target in $targets) {
  $app = Join-Path $root $target.App
  $merged = Join-Path $root $target.Merged
  foreach ($source in @($app, $merged)) {
    if (!(Test-Path -LiteralPath $source)) { throw "Firmware build missing: $source" }
  }
  $appInfo = Get-FirmwareInfo $app
  $mergedInfo = Get-FirmwareInfo $merged
  if ($appInfo.Target -ne $target.Target -or $mergedInfo.Target -ne $target.Target) {
    throw "Target mismatch for $($target.Name): app=$($appInfo.Target), merged=$($mergedInfo.Target)"
  }
  if ($appInfo.Version -ne $mergedInfo.Version) {
    throw "Version mismatch for $($target.Name): app=$($appInfo.Version), merged=$($mergedInfo.Version)"
  }
  Assert-MergedContainsApp $app $merged

  $folder = Join-Path $firmwareRoot $target.Name
  New-Item -ItemType Directory -Force -Path $folder | Out-Null
  $otaName = "$($target.Name)-$($appInfo.Version).bin"
  $mergedName = "$($target.Name)-$($appInfo.Version)-merged.bin"
  $ota = Join-Path $folder $otaName
  $mergedOut = Join-Path $folder $mergedName

  & $Python $signer sign --key $privateKey --input $app --output $ota
  if ($LASTEXITCODE) { throw "Signing failed for $($target.Name)" }
  & $Python $signer verify --key $publicKey --input $ota
  if ($LASTEXITCODE) { throw "Signature verification failed for $($target.Name)" }
  Copy-Item -LiteralPath $merged -Destination $mergedOut -Force

  $rows += [pscustomobject]@{
    Name=$target.Name
    Label=$target.Label
    Version=$appInfo.Version
    Target=$target.Target
    Chip=$target.Chip
    Flash=$target.Flash
    Ota="../firmware/$($target.Name)/$otaName"
    OtaHash=(Get-FileHash -LiteralPath $ota -Algorithm SHA256).Hash.ToLowerInvariant()
    Merged="../firmware/$($target.Name)/$mergedName"
    MergedHash=(Get-FileHash -LiteralPath $mergedOut -Algorithm SHA256).Hash.ToLowerInvariant()
  }
}

$catalog = @()
foreach ($row in $rows) {
  $manifestName = "$($row.Name).json"
  $manifest = [ordered]@{
    name = "Open Fume Extractor - $($row.Label)"
    version = $row.Version
    new_install_prompt_erase = $true
    new_install_improv_wait_time = 0
    builds = @(
      [ordered]@{
        chipFamily = $row.Chip
        improv = $false
        parts = @(
          [ordered]@{ path = "../$($row.Merged)"; offset = 0 }
        )
      }
    )
  }
  [IO.File]::WriteAllText(
    (Join-Path $manifestRoot $manifestName),
    ($manifest | ConvertTo-Json -Depth 8),
    [Text.UTF8Encoding]::new($false)
  )
  $catalog += [ordered]@{
    id = $row.Name
    label = $row.Label
    version = $row.Version
    signatureTarget = $row.Target
    chipFamily = $row.Chip
    flashSize = $row.Flash
    manifest = "flasher/manifests/$manifestName"
    merged = $row.Merged.Replace('../', '')
    ota = $row.Ota.Replace('../', '')
    mergedSha256 = $row.MergedHash
  }
}
[IO.File]::WriteAllText(
  (Join-Path $flasherRoot 'firmware-catalog.json'),
  ($catalog | ConvertTo-Json -Depth 6),
  [Text.UTF8Encoding]::new($false)
)

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('# Firmware Files')
$lines.Add('')
$lines.Add('Install a complete merged image with the [GitHub Pages web flasher](https://icecube20.github.io/Open-Fume-Extractor/).')
$lines.Add('')
$lines.Add("Release tree generated: **$(Get-Date -Format 'yyyy-MM-dd')**")
$lines.Add('')
$lines.Add('The normal `.bin` files are Ed25519-signed OTA packages for the master web updater.')
$lines.Add('The `*-merged.bin` files are complete images for an initial USB flash at address `0x0`.')
$lines.Add('')
$lines.Add('| Target | Version | Signature target | OTA file | Merged file |')
$lines.Add('|---|---:|---|---|---|')
foreach ($row in $rows) {
  $lines.Add("| $($row.Name) | ``$($row.Version)`` | ``$($row.Target)`` | [download]($($row.Ota)) | [download]($($row.Merged)) |")
}
$lines.Add('')
$lines.Add('## SHA-256 checksums')
$lines.Add('')
foreach ($row in $rows) {
  $lines.Add("- ``$($row.OtaHash)``  ``$(Split-Path $row.Ota -Leaf)``")
  $lines.Add("- ``$($row.MergedHash)``  ``$(Split-Path $row.Merged -Leaf)``")
}
$lines.Add('')
$lines.Add('## Update rules')
$lines.Add('')
$lines.Add('- Never use a merged image in the web updater.')
$lines.Add('- Never flash an OTA package at address `0x0`.')
$lines.Add('- Display 320x480 and Display 800x480 are different firmware targets.')
$lines.Add('- The web updater rejects a file whose target or Ed25519 signature does not match the selected device.')

[IO.File]::WriteAllLines((Join-Path $docsRoot 'FIRMWARE.md'), $lines, [Text.UTF8Encoding]::new($false))
Write-Host "GitHub release tree prepared: $release" -ForegroundColor Green
