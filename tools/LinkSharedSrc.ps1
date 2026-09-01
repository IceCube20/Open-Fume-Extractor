$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$sharedSrc = Join-Path (Join-Path $root "src") "bus"
$moduleRoot = Join-Path $root "Module"

if (!(Test-Path -LiteralPath $sharedSrc -PathType Container)) {
  throw "Shared src folder not found: $sharedSrc"
}

Get-ChildItem -LiteralPath $moduleRoot -Directory | ForEach-Object {
  $link = Join-Path $_.FullName "src"

  if (Test-Path -LiteralPath $link) {
    $item = Get-Item -LiteralPath $link -Force
    if ($item.LinkType -eq "Junction" -and (($item.Target -join ";") -eq $sharedSrc)) {
      return
    }
    throw "Refusing to replace existing non-matching src folder: $link"
  }

  New-Item -ItemType Junction -Path $link -Target $sharedSrc | Out-Null
  Write-Host "Linked $link -> $sharedSrc"
}

