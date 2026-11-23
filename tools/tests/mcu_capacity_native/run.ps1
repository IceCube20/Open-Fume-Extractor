$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$source = "$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"
$build = "$root/tools/tests/build-checks/mcu-capacity-native"
python "$PSScriptRoot/extract.py"
if ($LASTEXITCODE -ne 0) {throw 'Manifest extraction failed'}
foreach ($variant in @('s3', 'p4')) {
  [string[]]$defines = @()
  if ($variant -eq 'p4') { $defines = @('/DCONFIG_IDF_TARGET_ESP32P4=1') }
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 /D_CRT_SECURE_NO_WARNINGS `
    @defines "/I$PSScriptRoot/stubs" "/I$source" "/I$build" `
    "$PSScriptRoot/test_capacity.cpp" "$source/ModuleRegistry.cpp" "/Fo$build/" "/Fe$build/test_$variant.exe"
  if ($LASTEXITCODE -ne 0) {throw "Capacity compile failed: $variant"}
  & "$build/test_$variant.exe"
  if ($LASTEXITCODE -ne 0) {throw "Capacity test failed: $variant"}
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 /D_CRT_SECURE_NO_WARNINGS `
    @defines "/I$PSScriptRoot/stubs" "/I$source" "/I$build" `
    "$PSScriptRoot/test_module_alarms.cpp" "$source/ModuleRegistry.cpp" "$source/EntityPresentation.cpp" "/Fo$build/" "/Fe$build/alarms_$variant.exe"
  if ($LASTEXITCODE -ne 0) {throw "Module alarm compile failed: $variant"}
  & "$build/alarms_$variant.exe"
  if ($LASTEXITCODE -ne 0) {throw "Module alarm test failed: $variant"}
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 /D_CRT_SECURE_NO_WARNINGS `
    @defines "/I$source" "/I$build" "$PSScriptRoot/test_multipart.cpp" "/Fo$build/" "/Fe$build/multipart_$variant.exe"
  if ($LASTEXITCODE -ne 0) {throw "Multipart compile failed: $variant"}
  & "$build/multipart_$variant.exe"
  if ($LASTEXITCODE -ne 0) {throw "Multipart test failed: $variant"}
}
