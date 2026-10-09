$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-weller-filter-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$source = Get-Content (Join-Path $PSScriptRoot '../../../Module/WellerZeroSmogModule/WellerZeroSmogModule.ino') -Raw
$functions = [regex]::Matches($source, '(?ms)^static void (publish_weller_frame|update_weller_faults|handle_telemetry)\([^\r\n]*\) \{\r?\n.*?^}')
$constant = [regex]::Match($source, '(?m)^static const uint32_t WELLER_FILTER_MAX_AGE_MS = [^;]+;')
if ($functions.Count -ne 3 -or !$constant.Success) { throw 'Weller production-function extraction failed' }
$generated = $constant.Value + "`n" + (($functions | ForEach-Object { $_.Value }) -join "`n`n")
[IO.File]::WriteAllText((Join-Path $build 'generated_filter.inc.h'), $generated)
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot/test_filter.cpp" "/Fo$build\" "/Fe$build/test_filter.exe"
if ($LASTEXITCODE -ne 0) { throw 'Weller filter native build failed' }
& "$build/test_filter.exe"
if ($LASTEXITCODE -ne 0) { throw 'Weller filter regression failed' }
