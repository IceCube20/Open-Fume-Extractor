$ErrorActionPreference = 'Stop'
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$build = Join-Path $env:TEMP 'ofe-weller-filter-native'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:INCLUDE = "$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um"
$env:LIB = "$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$source = Get-Content (Join-Path $PSScriptRoot '../../../Module/WellerZeroSmogModule/WellerZeroSmogModule.ino') -Raw
$functions = [regex]::Matches($source, '(?ms)^static void (publish_weller_frame|update_weller_faults|handle_telemetry)\([^\r\n]*\) \{\r?\n.*?^}')
$constant = [regex]::Match($source, '(?m)^static const uint32_t WELLER_FILTER_MAX_AGE_MS = [^;]+;')
if ($functions.Count -ne 3 -or !$constant.Success) { throw 'Weller production-function extraction failed' }
$generated = $constant.Value + "`n" + (($functions | ForEach-Object { $_.Value }) -join "`n`n")
[IO.File]::WriteAllText((Join-Path $build 'generated_filter.inc.h'), $generated)
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot/test_filter.cpp" "/Fo$build\" "/Fe$build/test_filter.exe"
if ($LASTEXITCODE -ne 0) { throw 'Weller filter native build failed' }
& "$build/test_filter.exe"
if ($LASTEXITCODE -ne 0) { throw 'Weller filter regression failed' }
$scheduler = Get-Content (Join-Path $PSScriptRoot '../../../OpenFumeExtractorMaster/src/MasterScheduler.cpp') -Raw
$policy = [regex]::Matches($scheduler, '(?ms)^(?:static bool module_filter_fault_current|uint16_t MasterScheduler::systemJbc(?:Error|FilterLife|FilterSaturation))\([^\r\n]*\)(?: const)? \{\r?\n.*?^}')
if ($policy.Count -ne 4) { throw 'Master Weller fault-policy extraction failed' }
[IO.File]::WriteAllText((Join-Path $build 'generated_master_filter.inc.h'), (($policy | ForEach-Object { $_.Value }) -join "`n`n"))
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot/test_master_filter.cpp" "/Fo$build\" "/Fe$build/test_master_filter.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master Weller fault-policy native build failed' }
& "$build/test_master_filter.exe"
if ($LASTEXITCODE -ne 0) { throw 'Master Weller fault-policy regressions failed' }
$commands = [regex]::Matches($source, '(?ms)^static void (weller_send_checked4|weller_set_filter_runtime|weller_reset_filter_runtime)\([^\r\n]*\) \{\r?\n.*?^}')
if ($commands.Count -ne 3) { throw 'Weller command extraction failed' }
[IO.File]::WriteAllText((Join-Path $build 'generated_commands.inc.h'), (($commands | ForEach-Object { $_.Value }) -join "`n`n"))
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot/test_commands.cpp" "/Fo$build\" "/Fe$build/test_commands.exe"
if ($LASTEXITCODE -ne 0) { throw 'Weller command native build failed' }
& "$build/test_commands.exe"
if ($LASTEXITCODE -ne 0) { throw 'Weller command regression failed' }
$presets = ''
foreach ($name in @('DisplayModule_320x480', 'DisplayModule_800x480', 'DisplayModule_ST7796_320x480')) {
  $display = Get-Content (Join-Path $PSScriptRoot "../../../Module/$name/$name.ino") -Raw
  $helpers = [regex]::Matches($display, '(?ms)^static (?:const char\* (?:tr|weller_filter_options)|uint8_t weller_filter_preset_index|int16_t targeted_event_value)\([^\r\n]*\) \{\r?\n.*?^}')
  $event = [regex]::Match($display, '(?ms)^static void lv_detail_weller_filter_event\([^\r\n]*\) \{\r?\n.*?^}')
  if ($helpers.Count -ne 4 -or !$event.Success) { throw 'Display filter preset extraction failed' }
  $presets += "namespace $name {`nstatic uint8_t display_language = 0;`n" + (($helpers | ForEach-Object { $_.Value }) -join "`n") + "`n" + $event.Value + "`n}`n"
}
[IO.File]::WriteAllText((Join-Path $build 'generated_presets.inc.h'), $presets)
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /utf-8 /W4 /O2 "/I$build" "$PSScriptRoot/test_presets.cpp" "/Fo$build\" "/Fe$build/test_presets.exe"
if ($LASTEXITCODE -ne 0) { throw 'Display filter preset native build failed' }
& "$build/test_presets.exe"
if ($LASTEXITCODE -ne 0) { throw 'Display filter preset regression failed' }
