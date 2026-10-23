$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
foreach ($lines in @(16, 32, 48)) {
  $build = "$root/tools/tests/build-checks/small-profile-$lines"
  New-Item -ItemType Directory -Force -Path $build | Out-Null
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
    "/DOFE_SMALL_DRAW_LINES=$lines" "/I$PSScriptRoot/stubs" `
    "/I$root/Module/DisplayModule_320x480/src" "$PSScriptRoot/test_profile.cpp" `
    "/Fo$build/" "/Fe$build/test_profile.exe"
  if ($LASTEXITCODE -ne 0) { throw "Small display profile build failed (B$lines)" }
  & "$build/test_profile.exe"
  if ($LASTEXITCODE -ne 0) { throw "Small display profile test failed (B$lines)" }
}
