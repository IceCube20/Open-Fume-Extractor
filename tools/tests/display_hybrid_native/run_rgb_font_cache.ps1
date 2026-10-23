$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$display = "$root/Module/DisplayModule_800x480"
$lvgl = "$env:USERPROFILE/Documents/Arduino/libraries/lvgl"
$build = "$root/tools/tests/build-checks/rgb-font-cache"
New-Item -ItemType Directory -Force -Path $build | Out-Null
$flags = @('/nologo', '/utf-8', '/O2', '/Gy', '/Gw', '/W3', '/D_CRT_SECURE_NO_WARNINGS', '/DLV_CONF_PATH=<rgb_font_test_config.h>',
  "/I$PSScriptRoot/font_cache_stubs", "/I$display", "/I$lvgl")
$sources = @('font/fmt_txt/lv_font_fmt_txt.c', 'font/lv_font.c', 'draw/lv_draw_buf.c', 'misc/lv_utils.c',
  'misc/lv_area.c', 'misc/lv_color.c', 'misc/lv_color_op.c', 'misc/lv_math.c', 'stdlib/clib/lv_string_clib.c')
$objects = @()
foreach ($source in $sources) {
  $obj = "$build/$([IO.Path]::GetFileNameWithoutExtension($source)).obj"
  & "$vc/bin/Hostx64/x64/cl.exe" @flags /TC /std:c11 /c "$lvgl/src/$source" "/Fo$obj"
  if ($LASTEXITCODE -ne 0) { throw "LVGL source build failed: $source" }
  $objects += $obj
}
foreach ($font in @("$display/openfume_montserrat_14_de.c", "$lvgl/src/font/lv_font_montserrat_14.c")) {
  $obj = "$build/$([IO.Path]::GetFileNameWithoutExtension($font)).obj"
  & "$vc/bin/Hostx64/x64/cl.exe" @flags /TC /std:c11 /c $font "/Fo$obj"
  if ($LASTEXITCODE -ne 0) { throw "Font build failed: $font" }
  $objects += $obj
}
foreach ($enabled in @(1, 0, 'default')) {
  $exe = "$build/test_rgb_font_cache_$enabled.exe"
  $mode = @()
  if ($enabled -ne 'default') { $mode += "/DOFE_RGB_FONT_CACHE=$enabled" }
  & "$vc/bin/Hostx64/x64/cl.exe" @flags /EHsc /std:c++17 @mode "$PSScriptRoot/test_rgb_font_cache.cpp" @objects "/Fo$build/" "/Fe$exe" /link /OPT:REF /INCREMENTAL:NO
  if ($LASTEXITCODE -ne 0) { throw 'RGB font cache build failed' }
  & $exe
  if ($LASTEXITCODE -ne 0) { throw 'RGB font cache regression failed' }
}
