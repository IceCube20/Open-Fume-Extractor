$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
foreach ($enabled in @(0, 1)) {
  $build = "$root/tools/tests/build-checks/display-gui-$enabled"
  New-Item -ItemType Directory -Force -Path $build | Out-Null
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 `
    "/DDISPLAY_GUI_TASK_ENABLED=$enabled" "/I$PSScriptRoot/stubs" `
    "/I$root/Module/DisplayModule_800x480/src" "$PSScriptRoot/test_gui_task.cpp" `
    "/Fo$build/" "/Fe$build/test_gui_task.exe"
  if ($LASTEXITCODE -ne 0) { throw "GUI task build failed ($enabled)" }
  & "$build/test_gui_task.exe"
  if ($LASTEXITCODE -ne 0) { throw "GUI task test failed ($enabled)" }
}
