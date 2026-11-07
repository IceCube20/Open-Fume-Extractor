$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-fae-identity-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
python "$PSScriptRoot/extract.py" "$build/generated_identity.inc.h"
if ($LASTEXITCODE) { throw 'FAE identity extraction failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "/I$root/Module/JbcBusModule/src" "$PSScriptRoot/test_identity.cpp" "/Fo$build/" "/Fe$build/test_identity.exe"
if ($LASTEXITCODE) { throw 'FAE identity native build failed' }
& "$build/test_identity.exe"
if ($LASTEXITCODE) { throw 'FAE identity regression failed' }
