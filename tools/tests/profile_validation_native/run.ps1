$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-profile-validation-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$src = Join-Path $root 'OpenFumeExtractorMaster/src'
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $python "$PSScriptRoot/extract_modbus.py" "$build/module_functions.h"
if ($LASTEXITCODE -ne 0) { throw 'Modbus decoder extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$src" "/I$build" "$PSScriptRoot/test_profile.cpp" "/Fo$build\" "/Fe$build/test_profile.exe"
if ($LASTEXITCODE -ne 0) { throw 'Profile validation native build failed' }
& "$build/test_profile.exe" "$root/examples/Weller_Zero_Smog_Shield_Pro_v6.ofeprofile" "$root/examples/Modbus_RTU_Generic_IO_v1.ofeprofile" "$root/examples/Modbus_Generic_Transform_TextMap_v1.ofeprofile"
if ($LASTEXITCODE -ne 0) { throw 'Profile validation regressions failed' }
