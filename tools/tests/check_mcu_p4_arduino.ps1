param([ValidateRange(1,4)][int]$Jobs=4)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out=Join-Path $PSScriptRoot 'build-checks\mcu-p4-arduino'
$cli=Join-Path $env:ProgramFiles 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
$config=Join-Path $env:USERPROFILE '.arduinoIDE\arduino-cli.yaml'
foreach ($case in @(
  @{board='ofemcu_p4_poe';flash=32;minimum=301;maximum=399},
  @{board='ofemcu_p4';flash=16;minimum=1;maximum=199}
)) {
  $directory=Join-Path $out $case.board
  $sketch=Join-Path $directory 'MCUP4BoardSmoke'
  $build=Join-Path $directory 'build'
  New-Item -ItemType Directory -Force -Path $sketch | Out-Null
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fixtures\MCUP4BoardSmoke\MCUP4BoardSmoke.ino') -Destination $sketch -Force
  Copy-Item -LiteralPath (Join-Path $root "tools\boards\master_p4_$($case.flash)mb.csv") -Destination (Join-Path $sketch 'partitions.csv') -Force
  & $cli --config-file $config compile --fqbn "ofemcu:esp32:$($case.board)" --jobs $Jobs --build-path $build $sketch
  if ($LASTEXITCODE -ne 0) {throw "IDE profile build failed: $($case.board)"}
  foreach ($suffix in @('bin','bootloader.bin')) {
    $path=Join-Path $build "MCUP4BoardSmoke.ino.$suffix"
    $bytes=[IO.File]::ReadAllBytes($path)
    if ($bytes[0] -ne 0xE9 -or [BitConverter]::ToUInt16($bytes,12) -ne 18 -or
        [BitConverter]::ToUInt16($bytes,15) -ne $case.minimum -or
        [BitConverter]::ToUInt16($bytes,17) -ne $case.maximum -or
        ($bytes[3] -shr 4) -ne $(if ($case.flash -eq 32) {5} else {4})) {
      throw "Wrong image header from IDE profile: $path"
    }
  }
  Write-Output "PASS: $($case.board) actual SDK/CPU/PSRAM compilation; app and bootloader $($case.minimum)..$($case.maximum), $($case.flash) MB"
}
