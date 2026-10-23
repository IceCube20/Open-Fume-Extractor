$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$cmakeBase = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake'
$build = Join-Path $env:TEMP 'ofe-jbc-detail-ui'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$env:PATH = "$vc/bin/Hostx64/x64;$sdk/bin/$version/x64;$env:PATH"
& "$cmakeBase/CMake/bin/cmake.exe" -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$cmakeBase/Ninja/ninja.exe" -DCMAKE_BUILD_TYPE=Release "-DLVGL_PATH=$env:USERPROFILE/Documents/Arduino/libraries/lvgl" "-DOFE_PYTHON=$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe"
if ($LASTEXITCODE) { throw 'LVGL visual test configuration failed' }
& "$cmakeBase/CMake/bin/cmake.exe" --build $build --parallel 2
if ($LASTEXITCODE) { throw 'LVGL visual test build failed' }
Push-Location $build
try {
  foreach ($name in @('jbc_detail_ui','jbc_detail_ui_st7796')) {
    & "$build/$name.exe"
    if ($LASTEXITCODE) { throw "LVGL layout regression failed: $name" }
  }
}
finally { Pop-Location }
Write-Output "Render output: $build"
