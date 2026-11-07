$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
& 'C:/esp/ofe-p4-3.3.12/python/Scripts/python.exe' "$PSScriptRoot\extract.py" "$build\generated_wifi.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'Master WiFi source extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$build" "$PSScriptRoot\test_wifi.cpp" "/Fo$build\" "/Fe$build\test_wifi.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master WiFi native build failed' }
& "$build\test_wifi.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master WiFi native tests failed' }
