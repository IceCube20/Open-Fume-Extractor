$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-ha-api-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "$PSScriptRoot\test_master_api.cpp" "/Fo$build\" "/Fe$build\test_master_api.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master HA API native build failed' }
& "$build\test_master_api.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master HA API regressions failed' }
$project = (Resolve-Path "$PSScriptRoot\..\..\..").Path
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$project\tools\tests\display_hybrid_native\stubs" "/I$project\OpenFumeExtractorMaster\src" "$PSScriptRoot\test_jbc_counters.cpp" "/Fo$build\" "/Fe$build\test_jbc_counters.exe"
if ($LASTEXITCODE -ne 0) { throw 'JBC HA counter catalogue build failed' }
& "$build\test_jbc_counters.exe"
if ($LASTEXITCODE -ne 0) { throw 'JBC HA counter catalogue regressions failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "$PSScriptRoot\test_mqtt_performance.cpp" "/Fo$build\" "/Fe$build\test_mqtt_performance.exe"
if ($LASTEXITCODE -ne 0) { throw 'MQTT performance native build failed' }
& "$build\test_mqtt_performance.exe"
if ($LASTEXITCODE -ne 0) { throw 'MQTT performance regressions failed' }
& "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" "$project\tools\tests\display_hybrid_native\extract_refactor.py" "$build\generated_refactor.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'MQTT availability function extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot\test_mqtt_availability.cpp" "/Fo$build\" "/Fe$build\test_mqtt_availability.exe"
if ($LASTEXITCODE -ne 0) { throw 'MQTT availability native build failed' }
& "$build\test_mqtt_availability.exe"
if ($LASTEXITCODE -ne 0) { throw 'MQTT availability regressions failed' }
