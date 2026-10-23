$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-home-metrics-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$src = Join-Path $PSScriptRoot '../../../OpenFumeExtractorMaster/src'
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $python "$PSScriptRoot/extract_builder.py" "$build/builder.inc.h"
if ($LASTEXITCODE) { throw 'Metrics source extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$PSScriptRoot/../display_hybrid_native/stubs" "/I$src" "/I$src/../../Module/DisplayModule_800x480/src" "/I$build" "$PSScriptRoot/test_metrics.cpp" "$src/EntityPresentation.cpp" "/Fo$build\" "/Fe$build/test_metrics.exe"
if ($LASTEXITCODE) { throw 'Metrics native build failed' }
& "$build/test_metrics.exe"
if ($LASTEXITCODE) { throw 'Metrics regressions failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$PSScriptRoot/../display_hybrid_native/stubs" "/I$src" "/I$src/../../Module/DisplayModule_800x480/src" "$PSScriptRoot/test_values.cpp" "/Fo$build\test_values.obj" "/Fe$build/test_values.exe"
if ($LASTEXITCODE) { throw 'Value stream native build failed' }
& "$build/test_values.exe"
if ($LASTEXITCODE) { throw 'Value stream regressions failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot/test_mqtt_cycle.cpp" "/Fo$build\test_mqtt_cycle.obj" "/Fe$build/test_mqtt_cycle.exe"
if ($LASTEXITCODE) { throw 'MQTT cycle native build failed' }
& "$build/test_mqtt_cycle.exe"
if ($LASTEXITCODE) { throw 'MQTT cycle regressions failed' }
