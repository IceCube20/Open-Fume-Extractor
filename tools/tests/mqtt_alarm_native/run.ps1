$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-mqtt-alarm-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $python "$PSScriptRoot\extract_alarm.py" "$build\generated_alarm.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'MQTT alarm extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot\test_alarm.cpp" "/Fo$build\" "/Fe$build\test_alarm.exe"
if ($LASTEXITCODE -ne 0) { throw 'MQTT alarm native build failed' }
& "$build\test_alarm.exe"
if ($LASTEXITCODE -ne 0) { throw 'MQTT alarm regressions failed' }
