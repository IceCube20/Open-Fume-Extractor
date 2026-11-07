$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$build = "$root/tools/tests/build-checks/p4-hostname"
New-Item -ItemType Directory -Force -Path $build | Out-Null
& 'C:/esp/ofe-p4-3.3.12/python/Scripts/python.exe' "$PSScriptRoot/extract.py" "$build/hostname.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'P4 hostname extraction failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot/test_hostname.cpp" "/Fo$build/" "/Fe$build/test_hostname.exe"
if ($LASTEXITCODE -ne 0) { throw 'P4 hostname test build failed' }
& "$build/test_hostname.exe"
if ($LASTEXITCODE -ne 0) { throw 'P4 hostname tests failed' }
