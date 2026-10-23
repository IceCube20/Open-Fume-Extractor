$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$build = Join-Path $PSScriptRoot '../build-checks/master-diagnostics-storage'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $python "$PSScriptRoot/extract_diagnostics_storage.py" "$build/generated_diagnostics_storage.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'Diagnostics extraction failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 "/I$build" "/I$PSScriptRoot/../display_hybrid_native/stubs" "$PSScriptRoot/test_diagnostics_storage.cpp" "/Fo$build/" "/Fe$build/diagnostics.exe"
if ($LASTEXITCODE -ne 0) { throw 'Diagnostics storage build failed' }
& "$build/diagnostics.exe"
if ($LASTEXITCODE -ne 0) { throw 'Diagnostics storage regression' }
