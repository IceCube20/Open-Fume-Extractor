$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$build = Join-Path $PSScriptRoot '../build-checks/render-diagnostics'
New-Item -ItemType Directory -Force -Path $build | Out-Null
foreach ($test in @('test_lvgl_profile','test_lvgl_profiler_runtime')) {
  $extra = @()
  if ($test -eq 'test_lvgl_profiler_runtime') { $extra += "$root/Module/DisplayModule_800x480/src/OfeLvProfiler.cpp" }
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$PSScriptRoot/stubs" "$PSScriptRoot/$test.cpp" @extra "/Fo$build/" "/Fe$build/$test.exe"
  if ($LASTEXITCODE -ne 0) { throw "Profiler build failed: $test" }
  & "$build/$test.exe"
  if ($LASTEXITCODE -ne 0) { throw "Profiler regression: $test" }
}
