param(
  [string]$Database = "$PSScriptRoot/build-checks/master-ha-rc8/compile_commands.json"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$master = Join-Path $root 'OpenFumeExtractorMaster'
$build = Join-Path $PSScriptRoot 'build-checks/master-current-syntax'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$commands = Get-Content -LiteralPath $Database -Raw | ConvertFrom-Json
foreach ($name in @('OpenFumeExtractorMaster.ino.cpp', 'MasterScheduler.cpp', 'MasterDisplayWifi.cpp')) {
  $entry = $commands | Where-Object { [IO.Path]::GetFileName($_.file) -eq $name } | Select-Object -First 1
  if (-not $entry) { throw "No ESP32 compiler command for $name" }
  if ($name -eq 'OpenFumeExtractorMaster.ino.cpp') {
    $source = Join-Path $master 'OpenFumeExtractorMaster.ino'
  } else {
    $source = Join-Path $master "src/$name"
  }
  $args = [System.Collections.Generic.List[string]]::new()
  $args.Add("-I$master")
  $args.Add("-I$master/src")
  $args.Add("-I$env:USERPROFILE/Documents/Arduino/libraries/NimBLE-Arduino/src")
  $args.Add('-include')
  $args.Add("$master/OfeBleBuildConfig.h")
  $args.Add('-fsyntax-only')
  $args.Add('-x')
  $args.Add('c++')
  for ($i = 1; $i -lt $entry.arguments.Count; ++$i) {
    $arg = $entry.arguments[$i]
    if ($arg -eq '-o') { ++$i; continue }
    if ($arg -eq '-c' -or $arg -eq '-MMD' -or $arg -eq $entry.file) { continue }
    $args.Add($arg)
  }
  $args.Add($source)
  & $entry.arguments[0] @args
  if ($LASTEXITCODE -ne 0) { throw "ESP32 syntax check failed: $name" }
  Write-Output "PASS: ESP32 compiler, current $name and current project headers"
}
