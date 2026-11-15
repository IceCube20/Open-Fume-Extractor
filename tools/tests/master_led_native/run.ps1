$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$build = "$root/tools/tests/build-checks/master-led-native"
New-Item -ItemType Directory -Force -Path $build | Out-Null
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 /DOFE_STATUS_LED_ENABLE=1 /DOFE_STATUS_LED_PIN=4 `
  "/I$PSScriptRoot/stubs" "/I$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src" "$PSScriptRoot/test_master_led.cpp" "/Fo$build/" "/Fe$build/test_master_led.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master LED compile failed' }
& "$build/test_master_led.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master LED tests failed' }
