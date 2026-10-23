$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$build = Join-Path $PSScriptRoot 'build-checks/master-diagnostics-storage'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$commands = Get-Content -LiteralPath "$PSScriptRoot/build-checks/master-ha-rc8/compile_commands.json" -Raw | ConvertFrom-Json
$entry = $commands | Where-Object { [IO.Path]::GetFileName($_.file) -eq 'MasterScheduler.cpp' } | Select-Object -First 1
if (-not $entry) { throw 'Missing ESP32 compiler command' }
$args = [System.Collections.Generic.List[string]]::new()
$args.Add("-I$root/OpenFumeExtractorMaster/src")
for ($i=1; $i -lt $entry.arguments.Count; ++$i) {
  $arg=$entry.arguments[$i]
  if ($arg -eq '-o') { ++$i; continue }
  if ($arg -eq '-MMD' -or $arg -eq $entry.file) { continue }
  $args.Add($arg)
}
$args.Add("$PSScriptRoot/master_diagnostics_size.cpp")
$args.Add('-o'); $args.Add("$build/esp32-size.o")
& $entry.arguments[0] @args
if ($LASTEXITCODE -ne 0) { throw 'ESP32 diagnostics ABI size compilation failed' }
$nm = Join-Path ([IO.Path]::GetDirectoryName($entry.arguments[0])) 'xtensa-esp32s3-elf-nm.exe'
$symbols = & $nm -S --size-sort "$build/esp32-size.o"
if ($LASTEXITCODE -ne 0) { throw 'Cannot read ESP32 size symbols' }
foreach ($name in @('ofe_diag_entry_size','ofe_diag_table_size')) {
  $line = $symbols | Where-Object { $_ -match "\b$name$" }
  if ($line -notmatch '^\S+\s+(\S+)\s+\S+\s+') { throw "Missing size symbol: $name" }
  Write-Output ("{0}: {1} bytes (ESP32 compiler)" -f $name,[Convert]::ToUInt32($Matches[1],16))
}
