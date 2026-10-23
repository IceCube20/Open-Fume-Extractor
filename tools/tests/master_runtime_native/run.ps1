$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-master-runtime-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$python = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $python "$PSScriptRoot\extract_runtime.py" "$build\generated_runtime.inc.h" "$build\generated_cpu.inc.h" "$build\generated_profile.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'Runtime extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 /O2 "/I$build" "$PSScriptRoot\test_runtime.cpp" "/Fo$build\" "/Fe$build\test_runtime.exe"
if ($LASTEXITCODE -ne 0) { throw 'Runtime native build failed' }
& "$build\test_runtime.exe"
if ($LASTEXITCODE -ne 0) { throw 'Runtime regressions failed' }
foreach ($variant in @('dual32', 'dual64', 'single32', 'disabled')) {
  $defines = @()
  if ($variant -eq 'dual64') { $defines += '/DTEST_COUNTER_64=1' }
  if ($variant -eq 'single32') { $defines += '/DconfigNUMBER_OF_CORES=1' }
  if ($variant -eq 'disabled') { $defines += '/DconfigGENERATE_RUN_TIME_STATS=0' }
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 /O2 @defines "/I$build" "$PSScriptRoot\test_cpu.cpp" "/Fo$build\" "/Fe$build\test_cpu_$variant.exe"
  if ($LASTEXITCODE -ne 0) { throw "CPU $variant native build failed" }
  & "$build\test_cpu_$variant.exe"
  if ($LASTEXITCODE -ne 0) { throw "CPU $variant regressions failed" }
}
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 /O2 "/I$build" "$PSScriptRoot\test_profile.cpp" "/Fo$build\" "/Fe$build\test_profile.exe"
if ($LASTEXITCODE -ne 0) { throw 'Task/phase profiling native build failed' }
& "$build\test_profile.exe"
if ($LASTEXITCODE -ne 0) { throw 'Task/phase profiling regressions failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 /O2 "/I$PSScriptRoot\buffer_stubs" "$PSScriptRoot\test_text_buffer.cpp" "/Fo$build\" "/Fe$build\test_text_buffer.exe"
if ($LASTEXITCODE -ne 0) { throw 'JSON text-buffer native build failed' }
& "$build\test_text_buffer.exe"
if ($LASTEXITCODE -ne 0) { throw 'JSON text-buffer regressions failed' }
