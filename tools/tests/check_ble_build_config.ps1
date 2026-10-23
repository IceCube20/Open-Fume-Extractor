param(
  [string]$Database = ""
)
$ErrorActionPreference = 'Stop'
if (-not $Database) {
  $Database = "$PSScriptRoot/build-checks/master-ha-rc8/compile_commands.json"
  if (-not (Test-Path -LiteralPath $Database)) {
    $Database = "$PSScriptRoot/build-checks/master-current-syntax/ble-compile-commands.json"
  }
}
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$commands = Get-Content -LiteralPath $Database -Raw | ConvertFrom-Json
$entry = $commands | Where-Object { $_.file -match 'exp_nimble_mem\.c$' } | Select-Object -First 1
if (-not $entry) { throw 'NimBLE allocator compiler recipe missing' }
$sketches = @('OpenFumeExtractorMaster','Module/DisplayModule_320x480','Module/DisplayModule_800x480','Module/DisplayModule_ST7796_320x480')
foreach ($relative in $sketches) {
  $sketch = Join-Path $root $relative
  $compileArgs = [System.Collections.Generic.List[string]]::new()
  $compileArgs.Add("-I$sketch")
  $compileArgs.Add('-E')
  $compileArgs.Add('-dM')
  for ($i=1; $i -lt $entry.arguments.Count; ++$i) {
    $arg = $entry.arguments[$i]
    if ($arg -eq '-o') { ++$i; continue }
    if ($arg -eq '-c' -or $arg -eq '-MMD') { continue }
    if ($arg -match '^@.*[/\\]build_opt\.h$') { $arg = "@$sketch/build_opt.h" }
    $compileArgs.Add($arg)
  }
  $macros = (& $entry.arguments[0] @compileArgs) -join "`n"
  if ($LASTEXITCODE -ne 0) { throw "NimBLE preprocessing failed: $relative" }
  if ($macros -match '(?m)^#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL\b') {
    throw "SDK internal allocator still active: $relative"
  }
  foreach ($expected in @('CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL','CONFIG_BT_NIMBLE_MAX_CONNECTIONS','CONFIG_NIMBLE_MAX_CONNECTIONS')) {
    if ($macros -notmatch "(?m)^#define $expected 1\r?$") { throw "Wrong $expected after SDK/NimBLE headers: $relative" }
  }
  Write-Output "PASS: actual ESP32 NimBLE C allocator preprocessing: external PSRAM only, exactly one connection ($relative)"
}
