$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$root = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$build = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$PSScriptRoot\stubs" "/I$root\OpenFumeExtractorMaster\src\bus" "$PSScriptRoot\test_tunnel.cpp" "$root\OpenFumeExtractorMaster\src\bus\Rs485PeripheralBus.cpp" "/Fo$build\" "/Fe$build\test_tunnel.exe"
if ($LASTEXITCODE -ne 0) { throw 'Native test build failed' }
& "$build\test_tunnel.exe"
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed' }

# Exercise the installed allocator with exactly the large display's LVGL config.
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "$PSScriptRoot\test_lvgl_profile.cpp" "/Fo$build\" "/Fe$build\test_lvgl_profile.exe"
if ($LASTEXITCODE -ne 0) { throw 'LVGL profile test build failed' }
& "$build\test_lvgl_profile.exe"
if ($LASTEXITCODE -ne 0) { throw 'LVGL profile tests failed' }

$lvgl = Join-Path $env:USERPROFILE 'Documents\Arduino\libraries\lvgl'
$display = Join-Path $root 'Module\DisplayModule_800x480'
$configFlag = '/DLV_CONF_PATH="ofe_lv_conf.h"'
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /TC /std:c11 /utf-8 /W3 $configFlag "/I$display" "/I$lvgl" /c "$lvgl\src\stdlib\builtin\lv_tlsf.c" "/Fo$build\lv_tlsf.obj"
if ($LASTEXITCODE -ne 0) { throw 'LVGL allocator build failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 $configFlag "/I$display" "/I$lvgl" "$PSScriptRoot\test_lvgl_pool.cpp" "$build\lv_tlsf.obj" "/Fo$build\" "/Fe$build\test_lvgl_pool.exe"
if ($LASTEXITCODE -ne 0) { throw 'LVGL pool test build failed' }
& "$build\test_lvgl_pool.exe"
if ($LASTEXITCODE -ne 0) { throw 'LVGL pool tests failed' }

$python = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
foreach ($resolution in @('320x480', '800x480')) {
  & $python "$PSScriptRoot\extract_ota.py" $resolution "$build\generated_display_ota.inc.h"
  if ($LASTEXITCODE -ne 0) { throw 'OTA handler extraction failed' }
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$PSScriptRoot\stubs" "/I$root\OpenFumeExtractorMaster\src\bus" "/I$build" "$PSScriptRoot\test_display_ota.cpp" "$build\Rs485PeripheralBus.obj" "/Fo$build\" "/Fe$build\test_display_ota.exe"
  if ($LASTEXITCODE -ne 0) { throw "OTA test build failed ($resolution)" }
  & "$build\test_display_ota.exe"
  if ($LASTEXITCODE -ne 0) { throw "OTA tests failed ($resolution)" }
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$PSScriptRoot\stubs" "/I$root\OpenFumeExtractorMaster\src\bus" "/I$build" "$PSScriptRoot\test_display_status.cpp" "/Fo$build\" "/Fe$build\test_display_status.exe"
  if ($LASTEXITCODE -ne 0) { throw "Display status test build failed ($resolution)" }
  & "$build\test_display_status.exe"
  if ($LASTEXITCODE -ne 0) { throw "Display status tests failed ($resolution)" }
}
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$PSScriptRoot\stubs" "/I$root\OpenFumeExtractorMaster\src\bus" "/I$build" "$PSScriptRoot\test_master_route.cpp" "/Fo$build\" "/Fe$build\test_master_route.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master routing test build failed' }
& "$build\test_master_route.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master routing tests failed' }

& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$build" "$PSScriptRoot\test_display_interaction.cpp" "/Fo$build\" "/Fe$build\test_display_interaction.exe"
if ($LASTEXITCODE -ne 0) { throw 'Display interaction test build failed' }
& "$build\test_display_interaction.exe"
if ($LASTEXITCODE -ne 0) { throw 'Display interaction tests failed' }

& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$build" "$PSScriptRoot\test_rgb_tiles.cpp" "/Fo$build\" "/Fe$build\test_rgb_tiles.exe"
if ($LASTEXITCODE -ne 0) { throw 'RGB tile test build failed' }
& "$build\test_rgb_tiles.exe"
if ($LASTEXITCODE -ne 0) { throw 'RGB tile tests failed' }

& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 /DDISPLAY_LVGL_LARGE_TILES=0 "/I$build" "$PSScriptRoot\test_rgb_tiles.cpp" "/Fo$build\" "/Fe$build\test_rgb_tiles_default.exe"
if ($LASTEXITCODE -ne 0) { throw 'RGB SRAM default test build failed' }
& "$build\test_rgb_tiles_default.exe"
if ($LASTEXITCODE -ne 0) { throw 'RGB SRAM default tests failed' }
