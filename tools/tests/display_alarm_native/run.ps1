$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$build = "$root/tools/tests/build-checks/display-alarm-native"
python "$PSScriptRoot/extract.py"
if ($LASTEXITCODE -ne 0) { throw 'Alarm extraction failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
  "/I$build" "$PSScriptRoot/test_alarm.cpp" "/Fo$build/" "/Fe$build/test_alarm.exe"
if ($LASTEXITCODE -ne 0) { throw 'Alarm compile failed' }
& "$build/test_alarm.exe"
if ($LASTEXITCODE -ne 0) { throw 'Alarm tests failed' }
