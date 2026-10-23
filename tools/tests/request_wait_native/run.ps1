$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-request-wait-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "$PSScriptRoot\test_request_wait.cpp" "/Fo$build\" "/Fe$build\test_request_wait.exe"
if ($LASTEXITCODE -ne 0) { throw 'Request-wait native build failed' }
& "$build\test_request_wait.exe"
if ($LASTEXITCODE -ne 0) { throw 'Request-wait regressions failed' }
$root = (Resolve-Path "$PSScriptRoot\..\..\..").Path
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$PSScriptRoot\..\display_hybrid_native\stubs" "/I$root\OpenFumeExtractorMaster\src\bus" "$PSScriptRoot\test_parser_length.cpp" "$root\OpenFumeExtractorMaster\src\bus\Rs485PeripheralBus.cpp" "/Fo$build\" "/Fe$build\test_parser_length.exe"
if ($LASTEXITCODE -ne 0) { throw 'RS485 parser native build failed' }
& "$build\test_parser_length.exe"
if ($LASTEXITCODE -ne 0) { throw 'RS485 parser regressions failed' }
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $python "$PSScriptRoot\extract_maintenance.py" "$build\generated_maintenance.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'Maintenance extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "/I$PSScriptRoot\..\display_hybrid_native\stubs" "$PSScriptRoot\test_maintenance.cpp" "/Fo$build\" "/Fe$build\test_maintenance.exe"
if ($LASTEXITCODE -ne 0) { throw 'Maintenance native build failed' }
& "$build\test_maintenance.exe"
if ($LASTEXITCODE -ne 0) { throw 'Maintenance regressions failed' }
& $python "$PSScriptRoot\extract_discovery_guard.py" "$build\generated_discovery_guard.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'Discovery guard extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot\test_discovery_guard.cpp" "/Fo$build\" "/Fe$build\test_discovery_guard.exe"
if ($LASTEXITCODE -ne 0) { throw 'Discovery guard native build failed' }
& "$build\test_discovery_guard.exe"
if ($LASTEXITCODE -ne 0) { throw 'Discovery guard regressions failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "/I$PSScriptRoot\..\display_hybrid_native\stubs" "/I$root\OpenFumeExtractorMaster\src\bus" "$PSScriptRoot\test_display_reply.cpp" "/Fo$build\" "/Fe$build\test_display_reply.exe"
if ($LASTEXITCODE -ne 0) { throw 'Display reply dispatcher build failed' }
& "$build\test_display_reply.exe"
if ($LASTEXITCODE -ne 0) { throw 'Display reply dispatcher regressions failed' }
