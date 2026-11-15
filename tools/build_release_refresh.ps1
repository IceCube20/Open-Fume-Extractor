param([ValidateRange(1,4)][int]$Jobs = 2)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$cli = Join-Path $env:ProgramFiles 'Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'
if (!(Test-Path -LiteralPath $cli)) { $cli = (Get-Command arduino-cli -ErrorAction Stop).Source }
$config = Join-Path $env:USERPROFILE '.arduinoIDE/arduino-cli.yaml'
$configArgs = if (Test-Path -LiteralPath $config) { @('--config-file', $config) } else { @() }
$targets = @(
  @{Name='Community-MCU-S3'; Sketch='tools/tests/build-checks/community-mcu/OpenFumeExtractorMCU_S3'; Board='esp32.esp32.esp32s3'},
  @{Name='Fan-IO'; Sketch='Module/FanIoModule'; Board='esp32.esp32.esp32'},
  @{Name='Fan-IO-Pro'; Sketch='Module/FanIoProModule'; Board='esp32.esp32.esp32'},
  @{Name='JBC-FAE'; Sketch='Module/JbcBusModule'; Board='esp32.esp32.esp32'},
  @{Name='JBC-USB'; Sketch='Module/JbcUsbModule'; Board='esp32.esp32.esp32s3'},
  @{Name='Weller'; Sketch='Module/WellerZeroSmogModule'; Board='esp32.esp32.esp32'},
  @{Name='Universal'; Sketch='Module/UniversalRs232Module'; Board='esp32.esp32.esp32'},
  @{Name='Modbus'; Sketch='Module/ModbusRtuModule'; Board='esp32.esp32.esp32'}
)
$logRoot = Join-Path $root 'tools/tests/build-checks/release-refresh-219'
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
foreach ($target in $targets) {
  $sketch = Join-Path $root $target.Sketch
  $options = Get-Content -LiteralPath (Join-Path $sketch "build/$($target.Board)/build.options.json") -Raw | ConvertFrom-Json
  if (!$options.fqbn -or !$options.fqbn.StartsWith('esp32:esp32:')) { throw "Unexpected build profile: $($target.Name)" }
  $build = Join-Path $logRoot $target.Name
  $log = Join-Path $logRoot ($target.Name + '.log')
  Write-Output "BUILD: $($target.Name)"
  & $cli compile @configArgs --fqbn $options.fqbn --jobs $Jobs --build-path $build --export-binaries $sketch *> $log
  if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $log -Tail 35
    throw "Release build failed: $($target.Name); log: $log"
  }
  Write-Output "PASS: $($target.Name), exported; NOT flashed"
}
Write-Output 'All requested release refresh builds completed. No device was flashed.'
