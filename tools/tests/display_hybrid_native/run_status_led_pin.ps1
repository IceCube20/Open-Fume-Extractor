param(
  [string]$HeaderDirectory = "$PSScriptRoot/../../../Module/DisplayModule_P4_800x480/src",
  [switch]$CheckOldBug
)
$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$cases = @(@{name='missing'; enabled=1; pin=-1; count=2})
if (!$CheckOldBug) {
  $cases += @{name='disabled'; enabled=0; pin=4; count=2}
  $cases += @{name='empty'; enabled=1; pin=4; count=0}
  $cases += @{name='configured'; enabled=1; pin=4; count=2}
}
foreach ($case in $cases) {
  $name = $case.name + $(if ($CheckOldBug) {'-baseline'} else {''})
  $build = "$root/tools/tests/build-checks/status-led-$name"
  New-Item -ItemType Directory -Force -Path $build | Out-Null
  $defines = @("/DOFE_STATUS_LED_ENABLE=$($case.enabled)", "/DOFE_STATUS_LED_PIN=$($case.pin)",
               "/DOFE_STATUS_LED_COUNT=$($case.count)")
  if ($CheckOldBug) { $defines += '/DEXPECT_OLD_LED_BUG=1' }
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 @defines `
    "/I$PSScriptRoot/status_led_stubs" "/I$HeaderDirectory" "$PSScriptRoot/test_status_led_pin.cpp" `
    "/Fo$build/" "/Fe$build/test_status_led_pin.exe"
  if ($LASTEXITCODE -ne 0) { throw "Status LED build failed ($name)" }
  & "$build/test_status_led_pin.exe"
  if ($LASTEXITCODE -ne 0) { throw "Status LED test failed ($name)" }
}
