$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$build = Join-Path $PSScriptRoot '../build-checks/display-settings'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $python "$PSScriptRoot/extract_settings.py" "$build/generated_settings_controls.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'Settings function extraction failed' }
foreach ($variant in @('standard', 'st7796')) {
  $extra = @()
  if ($variant -eq 'st7796') { $extra += '/DST7796_PERCEPTUAL_BACKLIGHT=1' }
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 @extra "/I$build" "$PSScriptRoot/test_settings_layout.cpp" "/Fo$build/" "/Fe$build/settings-$variant.exe"
  if ($LASTEXITCODE -ne 0) { throw "Settings layout compile failed: $variant" }
  foreach ($width in @(456,776)) { foreach ($lang in @(0,1)) {
    $json = & "$build/settings-$variant.exe" $width $lang
    if ($LASTEXITCODE -ne 0) { throw "Settings bounds/overlap regression: $variant/$width/$lang" }
    $json | Set-Content -LiteralPath "$build/$variant-$width-$lang.json" -Encoding utf8
  } }
}
Write-Output 'PASS: settings in DE/EN, both widths and LCD profile; widget bounds, no overlaps, 44px targets, selections and event handlers'
