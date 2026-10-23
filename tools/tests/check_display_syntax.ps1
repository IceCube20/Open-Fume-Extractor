param(
  [string]$SmallBuild = "$env:LOCALAPPDATA/arduino/sketches/124D6934FB87C44CAB4C109EA08F31BB",
  [string]$LargeBuild = "$env:LOCALAPPDATA/arduino/sketches/2D5318A49D7C8999D370733F2B920513",
  [string[]]$DisplayNames = @('DisplayModule_320x480', 'DisplayModule_800x480', 'DisplayModule_ST7796_320x480')
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$build = Join-Path $PSScriptRoot 'build-checks/display-settings-syntax'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$specs = @(
  @('DisplayModule_320x480', $SmallBuild),
  @('DisplayModule_800x480', $LargeBuild),
  @('DisplayModule_ST7796_320x480', "$PSScriptRoot/build-checks/ST7796")
)
foreach ($spec in $specs) {
  $name = $spec[0]
  if ($name -notin $DisplayNames) { continue }
  $display = Join-Path $root "Module/$name"
  $commands = Get-Content -LiteralPath "$($spec[1])/compile_commands.json" -Raw | ConvertFrom-Json
  $entry = $commands | Where-Object { [IO.Path]::GetFileName($_.file) -eq "$name.ino.cpp" } | Select-Object -First 1
  if (-not $entry) { throw "Missing compiler recipe for $name" }
  $generated = Get-Content -LiteralPath $entry.file -Raw
  $directives = [regex]::Matches($generated, '(?m)^#line \d+ "[^"\r\n]*"\r?$')
  if ($directives.Count -lt 3) { throw 'Missing Arduino auto-prototype block' }
  $prototypeStart = $directives[1].Index
  $prototypeEnd = $directives[$directives.Count-1].Index
  $prototypes = $generated.Substring($prototypeStart, $prototypeEnd-$prototypeStart)
  $current = Get-Content -LiteralPath "$display/$name.ino" -Raw
  $marker = 'static void ofe_keep_module_fw_signature() __attribute__((noinline));'
  if (-not $current.Contains($marker)) { throw 'Unknown prototype insertion marker' }
  # Only generated compiler input is written; production source and IDE caches stay untouched.
  $source = Join-Path $build "$name.cpp"
  [IO.File]::WriteAllText($source, $current.Replace($marker, $marker+"`n"+$prototypes), [Text.UTF8Encoding]::new($false))
  $compileArgs = [System.Collections.Generic.List[string]]::new()
  $compileArgs.Add("-I$display")
  $compileArgs.Add("-I$env:USERPROFILE/Documents/Arduino/libraries/NimBLE-Arduino/src")
  $compileArgs.Add('-include')
  $compileArgs.Add("$display/OfeBleBuildConfig.h")
  $compileArgs.Add('-fsyntax-only')
  for ($i = 1; $i -lt $entry.arguments.Count; ++$i) {
    $arg = $entry.arguments[$i]
    if ($arg -eq '-o') { ++$i; continue }
    if ($arg -eq '-c' -or $arg -eq '-MMD' -or $arg -eq $entry.file) { continue }
    $compileArgs.Add($arg)
  }
  $compileArgs.Add($source)
  & $entry.arguments[0] @compileArgs
  if ($LASTEXITCODE -ne 0) { throw "Current display syntax check failed: $name" }
  Write-Output "PASS: ESP32 compiler, current sketch and settings header: $name"
}
