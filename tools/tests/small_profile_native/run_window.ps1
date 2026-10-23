$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$python = 'C:/esp/ofe-p4-3.3.12/python/Scripts/python.exe'
foreach ($spi in @(0, 1)) {
  $name = if ($spi) { 'DisplayModule_ST7796_320x480' } else { 'DisplayModule_320x480' }
  $build = "$root/tools/tests/build-checks/small-window-$spi"
  New-Item -ItemType Directory -Force -Path $build | Out-Null
  & $python "$PSScriptRoot/extract_window.py" $name "$build/generated_small_window.inc.h"
  if ($LASTEXITCODE -ne 0) { throw "Small display window extraction failed ($name)" }
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
    "/DDISPLAY_TEST_SPI=$spi" "/I$build" "$PSScriptRoot/test_window.cpp" `
    "/Fo$build/" "/Fe$build/test_window.exe"
  if ($LASTEXITCODE -ne 0) { throw "Small display window build failed ($name)" }
  & "$build/test_window.exe"
  if ($LASTEXITCODE -ne 0) { throw "Small display window regression failed ($name)" }
}
