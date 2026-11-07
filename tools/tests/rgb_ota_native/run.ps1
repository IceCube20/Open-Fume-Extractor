$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$root = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$build = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$PSScriptRoot\stubs" "/I$root\Module\DisplayModule_800x480\src" "$PSScriptRoot\test_rgb_ota.cpp" "/Fo$build\" "/Fe$build\test_rgb_ota.exe"
if ($LASTEXITCODE -ne 0) { throw 'RGB OTA native build failed' }
& "$build\test_rgb_ota.exe"
if ($LASTEXITCODE -ne 0) { throw 'RGB OTA native tests failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$root\Module\DisplayModule_800x480\src" "$PSScriptRoot\test_wifi_ota_timing.cpp" "/Fo$build\" "/Fe$build\test_wifi_ota_timing.exe"
if ($LASTEXITCODE -ne 0) { throw 'WLAN OTA timing native build failed' }
& "$build\test_wifi_ota_timing.exe"
if ($LASTEXITCODE -ne 0) { throw 'WLAN OTA timing native tests failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$root\Module\DisplayModule_800x480\src" "$PSScriptRoot\test_rgb_ota_probe.cpp" "/Fo$build\" "/Fe$build\test_rgb_ota_probe.exe"
if ($LASTEXITCODE -ne 0) { throw 'RGB OTA clock probe native build failed' }
& "$build\test_rgb_ota_probe.exe"
if ($LASTEXITCODE -ne 0) { throw 'RGB OTA clock probe native tests failed' }
