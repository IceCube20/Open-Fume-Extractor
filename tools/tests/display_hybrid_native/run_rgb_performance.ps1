$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$build = Join-Path $PSScriptRoot '../build-checks/rgb-performance'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $python "$PSScriptRoot/extract_ota.py" '800x480' "$build/generated_display_ota.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'RGB/OTA extraction failed' }
& $python "$PSScriptRoot/extract_rgb_performance.py" "$build/generated_rgb_performance.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'RGB scheduling extraction failed' }
foreach ($test in @('test_rgb_tiles','test_rgb_performance','test_display_ota')) {
  $extra = @()
  if ($test -eq 'test_display_ota') { $extra += "$root/OpenFumeExtractorMaster/src/bus/Rs485PeripheralBus.cpp" }
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 /O2 "/I$build" "/I$PSScriptRoot/stubs" "/I$root/OpenFumeExtractorMaster/src/bus" "$PSScriptRoot/$test.cpp" @extra "/Fo$build/" "/Fe$build/$test.exe"
  if ($LASTEXITCODE -ne 0) { throw "Native RGB build failed: $test" }
  & "$build/$test.exe"
  if ($LASTEXITCODE -ne 0) { throw "Native RGB regression: $test" }
}
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 /O2 /DDISPLAY_LVGL_LARGE_TILES=0 "/I$build" "$PSScriptRoot/test_rgb_tiles.cpp" "/Fo$build/" "/Fe$build/test_rgb_tiles_default.exe"
if ($LASTEXITCODE -ne 0) { throw 'Default RGB copy build failed' }
& "$build/test_rgb_tiles_default.exe"
if ($LASTEXITCODE -ne 0) { throw 'Default RGB copy regression' }
