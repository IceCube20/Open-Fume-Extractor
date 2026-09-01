param(
  [string]$ProjectRoot = (Join-Path $PSScriptRoot '..')
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$targets = @(
  @{ Name='Master'; Path='OpenFumeExtractorMaster\src\MasterBuildConfig.h'; Prefix='MASTER_FW'; Target='MASTER'; Address='0x01'; Board='ESP32-S3, 16 MB Flash, 8 MB OPI-PSRAM' },
  @{ Name='JBC FAE Bus'; Path='Module\JbcBusModule\JbcBusModule.ino'; Prefix='OFE_MODULE_FW'; Target='JBC_BUS'; Address='0x10'; Board='ESP32 DevKit V1' },
  @{ Name='JBC USB'; Path='Module\JbcUsbModule\JbcUsbModule.ino'; Prefix='OFE_MODULE_FW'; Target='JBC_USB'; Address='0x11'; Board='ESP32-S3, USB Host' },
  @{ Name='Fan/IO'; Path='Module\FanIoModule\FanIoModule.ino'; Prefix='OFE_MODULE_FW'; Target='FAN_IO'; Address='0x20'; Board='ESP32 DevKit V1' },
  @{ Name='Fan/IO Pro'; Path='Module\FanIoProModule\FanIoProModule.ino'; Prefix='OFE_MODULE_FW'; Target='FAN_IO_PRO'; Address='0x20'; Board='ESP32 DevKit V1' },
  @{ Name='Weller Zero Smog'; Path='Module\WellerZeroSmogModule\WellerZeroSmogModule.ino'; Prefix='OFE_MODULE_FW'; Target='WELLER_ZERO_SMOG'; Address='0x30'; Board='ESP32 DevKit V1' },
  @{ Name='Display 320x480'; Path='Module\DisplayModule_320x480\DisplayModule_320x480.ino'; Prefix='OFE_MODULE_FW'; Target='DISPLAY_320X480'; Address='0x40'; Board='ESP32-S3 / JC3248W535C_I_Y' },
  @{ Name='Display 800x480'; Path='Module\DisplayModule_800x480\DisplayModule_800x480.ino'; Prefix='OFE_MODULE_FW'; Target='DISPLAY_800X480'; Address='0x40'; Board='ESP32-S3 / Guition JC8048W550' },
  @{ Name='Universal RS232'; Path='Module\UniversalRs232Module\UniversalRs232Module.ino'; Prefix='OFE_MODULE_FW'; Target='UNIVERSAL_RS232'; Address='0x50'; Board='ESP32 DevKit V1' },
  @{ Name='Modbus RTU'; Path='Module\ModbusRtuModule\ModbusRtuModule.ino'; Prefix='OFE_MODULE_FW'; Target='MODBUS_RTU'; Address='0x60'; Board='ESP32 DevKit V1' }
)

function Read-Macro([string]$Text, [string]$Name) {
  $match = [regex]::Match($Text, '(?m)^\s*#define\s+' + [regex]::Escape($Name) + '\s+"?([^"\s/]+)"?')
  if (!$match.Success) { throw "Macro $Name not found" }
  return $match.Groups[1].Value
}

$rows = foreach ($target in $targets) {
  $path = Join-Path $root $target.Path
  $text = Get-Content -LiteralPath $path -Raw
  $major = Read-Macro $text ($target.Prefix + '_MAJOR')
  $minor = Read-Macro $text ($target.Prefix + '_MINOR')
  $patch = Read-Macro $text ($target.Prefix + '_PATCH')
  $suffix = Read-Macro $text ($target.Prefix + '_SUFFIX')
  $signaturePrefix = "OFE_FW_SIG:v1;target=$($target.Target);"
  if (!$text.Contains($signaturePrefix)) {
    throw "Firmware signature target $($target.Target) not found in $($target.Path)"
  }
  [pscustomobject]@{
    Name = $target.Name
    Version = "$major.$minor.$patch$suffix"
    Target = $target.Target
    Address = $target.Address
    Board = $target.Board
    Source = ($target.Path -replace '\\','/')
  }
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('# Firmwarestände')
$lines.Add('')
$lines.Add('Diese Datei wird aus den Versionsmakros der Sketches erzeugt. Manuelle Änderungen')
$lines.Add('an der Tabelle werden beim nächsten Lauf von `tools/update_firmware_docs.ps1` überschrieben.')
$lines.Add('')
$lines.Add("Stand: **$(Get-Date -Format 'yyyy-MM-dd')**")
$lines.Add('')
$lines.Add('| Firmware | Version | Signaturziel | Werksadresse | Hardware |')
$lines.Add('|---|---:|---|---:|---|')
foreach ($row in $rows) {
  $lines.Add("| [$($row.Name)](../$($row.Source)) | ``$($row.Version)`` | ``$($row.Target)`` | ``$($row.Address)`` | $($row.Board) |")
}
$lines.Add('')
$lines.Add('## Hinweise')
$lines.Add('')
$lines.Add('- Die Adresse ist die Werkseinstellung. Weitere Module derselben Familie erhalten fortlaufende Adressen innerhalb ihres Familienbereichs.')
$lines.Add('- Beide Displays beginnen bei `0x40`; sie sind durch Capability und Firmware-Signatur eindeutig unterscheidbar.')
$lines.Add('- Hardwareversion `0x0100` entspricht Revision 1.0 der derzeitigen Moduldefinitionen.')
$lines.Add('- Vor einem Update muss das erkannte Signaturziel zum ausgewählten Zielgerät passen. Nur der Entwicklermodus darf diese Prüfung bewusst übergehen.')
$lines.Add('')
$lines.Add('## Aktualisieren')
$lines.Add('')
$lines.Add('```powershell')
$lines.Add('.\tools\update_firmware_docs.ps1')
$lines.Add('```')

$output = Join-Path $root 'docs\Firmwarestaende.md'
[IO.File]::WriteAllLines($output, $lines, [Text.UTF8Encoding]::new($false))
Write-Host "Updated $output"
