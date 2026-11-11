$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $PSScriptRoot '../build-checks/system-automation'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /W4 /O2 "$PSScriptRoot/test_automation.cpp" "/Fo$build/" "/Fe$build/test_automation.exe"
if ($LASTEXITCODE -ne 0) { throw 'Automation native build failed' }
& "$build/test_automation.exe"
if ($LASTEXITCODE -ne 0) { throw 'Automation native tests failed' }
& python "$PSScriptRoot/extract_scheduler.py"
if ($LASTEXITCODE -ne 0) { throw 'Scheduler extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /W4 /O2 "/I$build" "$PSScriptRoot/test_system_inputs.cpp" "/Fo$build/" "/Fe$build/test_system_inputs.exe"
if ($LASTEXITCODE -ne 0) { throw 'Scheduler predicates native build failed' }
& "$build/test_system_inputs.exe"
if ($LASTEXITCODE -ne 0) { throw 'Scheduler predicate tests failed' }
