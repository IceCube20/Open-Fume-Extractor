$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$masterSrc = Join-Path (Join-Path $root "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3") "src"
$moduleRoot = Join-Path $root "Module"

$checks = @(
  @{
    Name = "Rs485PeripheralBus.h"
    Canonical = Join-Path (Join-Path $masterSrc "bus") "Rs485PeripheralBus.h"
    Relative = Join-Path "src" "Rs485PeripheralBus.h"
  },
  @{
    Name = "Rs485PeripheralBus.cpp"
    Canonical = Join-Path (Join-Path $masterSrc "bus") "Rs485PeripheralBus.cpp"
    Relative = Join-Path "src" "Rs485PeripheralBus.cpp"
  },
  @{
    Name = "OfeStatusLed.h"
    # MCU LEDs support a driver override; module LEDs keep the direct driver.
    Canonical = Join-Path $moduleRoot "FanIoModule/src/OfeStatusLed.h"
    Relative = Join-Path "src" "OfeStatusLed.h"
  }
)

$failed = $false

foreach ($check in $checks) {
  if (!(Test-Path -LiteralPath $check.Canonical -PathType Leaf)) {
    throw "Canonical file not found: $($check.Canonical)"
  }

  $canonicalHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $check.Canonical).Hash
  Write-Host ("{0}: canonical {1}" -f $check.Name, $canonicalHash.Substring(0, 12))

  Get-ChildItem -LiteralPath $moduleRoot -Directory | Sort-Object Name | ForEach-Object {
    $copy = Join-Path $_.FullName $check.Relative
    if (!(Test-Path -LiteralPath $copy -PathType Leaf)) {
      Write-Host ("  MISSING {0}" -f $copy) -ForegroundColor Red
      $script:failed = $true
      return
    }

    $copyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $copy).Hash
    if ($copyHash -ne $canonicalHash) {
      Write-Host ("  DRIFT   {0} {1}" -f $_.Name, $copyHash.Substring(0, 12)) -ForegroundColor Red
      $script:failed = $true
    } else {
      Write-Host ("  OK      {0}" -f $_.Name)
    }
  }
}

$mcuLed = Join-Path $masterSrc 'OfeStatusLed.h'
$p4Led = Join-Path $root 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4/src/OfeStatusLed.h'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $mcuLed).Hash -ne
    (Get-FileHash -Algorithm SHA256 -LiteralPath $p4Led).Hash) {
  throw 'MCU-S3/P4 LED driver drift detected.'
}

if ($failed) {
  throw "Shared source drift detected. Copy the canonical files intentionally, then rerun this check."
}

Write-Host "Shared source check passed."
