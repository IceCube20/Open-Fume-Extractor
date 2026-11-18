$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$build = "$root/tools/tests/build-checks/usb-storage-native"
New-Item -ItemType Directory -Force -Path $build | Out-Null
python "$PSScriptRoot/extract_transfer.py"
if ($LASTEXITCODE -ne 0) { throw 'USB transfer source extraction failed' }
foreach ($config in @(@(1, 2, 0), @(1, 2, 1), @(0, 2, 0), @(1, 1, 0))) {
  $suffix = $config -join '_'
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /TC /std:c11 /utf-8 /W4 /O2 `
    "/DTEST_LABEL_ENABLED=$($config[0])" "/DTEST_LABEL_UNICODE=$($config[1])" "/DTEST_LABEL_BDL=$($config[2])" `
    "/I$build" "$PSScriptRoot/test_volume_label.c" "/Fo$build/" "/Fe$build/test_volume_label_$suffix.exe"
  if ($LASTEXITCODE -ne 0) { throw 'USB volume label accessor compile failed' }
  & "$build/test_volume_label_$suffix.exe"
  if ($LASTEXITCODE -ne 0) { throw 'USB volume label accessor tests failed' }
}
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
  "/I$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src" "$PSScriptRoot/test_policy.cpp" "/Fo$build/" "/Fe$build/test_policy.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB policy compile failed' }
& "$build/test_policy.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB policy tests failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
  "/I$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src" "/I$build" "$PSScriptRoot/test_transfer.cpp" "/Fo$build/" "/Fe$build/test_transfer.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB transfer compile failed' }
& "$build/test_transfer.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB transfer tests failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
  "/I$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src" "/I$build" "$PSScriptRoot/test_multi_drive.cpp" "/Fo$build/" "/Fe$build/test_multi_drive.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB multi-drive compile failed' }
& "$build/test_multi_drive.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB multi-drive tests failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
  "/I$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src" "/I$build" "$PSScriptRoot/test_file_manager.cpp" "/Fo$build/" "/Fe$build/test_file_manager.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB file manager compile failed' }
& "$build/test_file_manager.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB file manager tests failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
  "/I$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src" "$PSScriptRoot/test_diagnostics.cpp" "/Fo$build/" "/Fe$build/test_diagnostics.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB diagnostics compile failed' }
& "$build/test_diagnostics.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB diagnostics tests failed' }
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
  "/I$root/OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src" "/I$build" "$PSScriptRoot/test_census.cpp" "/Fo$build/" "/Fe$build/test_census.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB census compile failed' }
& "$build/test_census.exe"
if ($LASTEXITCODE -ne 0) { throw 'USB census tests failed' }
foreach ($enabled in @(0, 1)) {
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/DTEST_ENUM_FILTER=$enabled" `
    "/I$build" "$PSScriptRoot/test_enumeration.cpp" "/Fo$build/" "/Fe$build/test_enumeration_$enabled.exe"
  if ($LASTEXITCODE -ne 0) { throw 'USB enumeration filter compile failed' }
  & "$build/test_enumeration_$enabled.exe"
  if ($LASTEXITCODE -ne 0) { throw 'USB enumeration filter tests failed' }
}
