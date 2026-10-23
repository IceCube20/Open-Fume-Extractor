$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-jbc-detail-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
& "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe" "$PSScriptRoot/extract_jbc_detail.py" "$build/generated_jbc_detail.inc.h"
if ($LASTEXITCODE) { throw 'JBC detail extraction failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 /O2 "/I$build" "/I$PSScriptRoot/stubs" "/I$root/OpenFumeExtractorMaster/src" "/I$root/Module/DisplayModule_800x480/src" "$PSScriptRoot/test_jbc_detail.cpp" "$root/OpenFumeExtractorMaster/src/bus/Rs485PeripheralBus.cpp" "/Fo$build/" "/Fe$build/test_jbc_detail.exe"
if ($LASTEXITCODE) { throw 'JBC detail native build failed' }
& "$build/test_jbc_detail.exe"
if ($LASTEXITCODE) { throw 'JBC detail regression failed' }
