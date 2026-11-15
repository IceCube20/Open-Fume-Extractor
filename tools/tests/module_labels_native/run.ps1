$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$build = "$root/tools/tests/build-checks/module-labels-native"
python "$PSScriptRoot/extract.py"
if ($LASTEXITCODE -ne 0) { throw 'Alias extraction failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
  "/I$PSScriptRoot/../settings_store_native/stubs" "/I$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src" "/I$build" `
  "$PSScriptRoot/test_labels.cpp" "$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterSettingsStore.cpp" "/Fo$build/" "/Fe$build/test_labels.exe"
if ($LASTEXITCODE -ne 0) { throw 'Alias compile failed' }
& "$build/test_labels.exe"
if ($LASTEXITCODE -ne 0) { throw 'Alias tests failed' }
