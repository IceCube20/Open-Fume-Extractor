$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-settings-store-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$src = Join-Path $PSScriptRoot '../../../OpenFumeExtractorMaster/src'
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$PSScriptRoot/stubs" "/I$src" "$PSScriptRoot/test_settings.cpp" "$src/MasterSettingsStore.cpp" "/Fo$build\" "/Fe$build/test_settings.exe"
if ($LASTEXITCODE -ne 0) { throw 'Settings store native build failed' }
& "$build/test_settings.exe"
if ($LASTEXITCODE -ne 0) { throw 'Settings store regressions failed' }
