$ErrorActionPreference = 'Stop'
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$build = Join-Path $PSScriptRoot '../build-checks/display-pairing'
New-Item -ItemType Directory -Force -Path $build | Out-Null
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W3 "$PSScriptRoot/test_pairing_layout.cpp" "/Fo$build/" "/Fe$build/pairing.exe"
if ($LASTEXITCODE -ne 0) { throw 'Pairing layout compile failed' }
foreach ($width in @(480,800)) { foreach ($lang in @(0,1)) {
  $json = & "$build/pairing.exe" $width $lang
  if ($LASTEXITCODE -ne 0) { throw "Pairing regression: $width/$lang" }
  $json | Set-Content -LiteralPath "$build/$width-$lang.json" -Encoding utf8
} }
Write-Output 'PASS: actual pairing UI, both languages/sizes, bounds, keyboard, dialogs, dirty fields, refresh, save, forget and cancel'
