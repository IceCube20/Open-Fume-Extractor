$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../..").Path
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-mqtt-jbc-cleanup-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
& "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe" "$PSScriptRoot/extract_cleanup.py" "$build/generated_cleanup.inc.h"
if ($LASTEXITCODE) { throw 'MQTT JBC cleanup extraction failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "/I$project/tools/tests/display_hybrid_native/stubs" "/I$project/OpenFumeExtractorMaster/src" "$PSScriptRoot/test_cleanup.cpp" "/Fo$build/" "/Fe$build/test_cleanup.exe"
if ($LASTEXITCODE) { throw 'MQTT JBC cleanup native build failed' }
& "$build/test_cleanup.exe"
if ($LASTEXITCODE) { throw 'MQTT JBC cleanup regression failed' }
