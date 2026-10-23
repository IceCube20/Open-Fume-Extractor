$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-display-backlight-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$python = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
foreach ($resolution in @('320x480', '800x480', 'ST7796_320x480')) {
  & $python "$PSScriptRoot\extract_backlight.py" $resolution "$build\generated_backlight.inc.h"
  if ($LASTEXITCODE -ne 0) { throw "Brightness extraction failed ($resolution)" }
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "/I$build" "$PSScriptRoot\test_backlight.cpp" "/Fo$build\" "/Fe$build\test_backlight.exe"
  if ($LASTEXITCODE -ne 0) { throw "Brightness native build failed ($resolution)" }
  & "$build\test_backlight.exe"
  if ($LASTEXITCODE -ne 0) { throw "Brightness regressions failed ($resolution)" }
  Write-Output "Verified display: $resolution"
}
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 "$PSScriptRoot\test_backlight_shutdown.cpp" "/Fo$build\" "/Fe$build\test_backlight_shutdown.exe"
if ($LASTEXITCODE -ne 0) { throw 'Backlight shutdown native build failed' }
& "$build\test_backlight_shutdown.exe"
if ($LASTEXITCODE -ne 0) { throw 'Backlight shutdown regressions failed' }
& $python "$PSScriptRoot\extract_backlight_settings.py" "$build\generated_backlight_settings.inc.h"
if ($LASTEXITCODE -ne 0) { throw 'Backlight settings extraction failed' }
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 "/I$build" "$PSScriptRoot\test_backlight_settings.cpp" "/Fo$build\" "/Fe$build\test_backlight_settings.exe"
if ($LASTEXITCODE -ne 0) { throw 'Backlight settings native build failed' }
& "$build\test_backlight_settings.exe"
if ($LASTEXITCODE -ne 0) { throw 'Backlight settings regressions failed' }
