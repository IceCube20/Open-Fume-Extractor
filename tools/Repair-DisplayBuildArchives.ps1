param(
  [Parameter(Mandatory = $true)][string[]]$BuildPaths,
  [switch]$Repair
)
$ErrorActionPreference = 'Stop'
$cacheRoot = [IO.Path]::GetFullPath("$env:LOCALAPPDATA/arduino/sketches").TrimEnd('\')
$moduleRoot = [IO.Path]::GetFullPath("$PSScriptRoot/../Module").Replace('\', '/').TrimEnd('/')
$modules = @('DisplayModule_320x480', 'DisplayModule_800x480', 'DisplayModule_ST7796_320x480')
$bin = "$env:LOCALAPPDATA/Arduino15/packages/esp32/tools/esp-x32/2601/bin"
$ar = Join-Path $bin 'xtensa-esp-elf-gcc-ar.exe'
$objdump = Join-Path $bin 'xtensa-esp-elf-objdump.exe'
$backupRoot = Join-Path $PSScriptRoot ("tests/build-checks/archive-diagnosis/backups/" + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
if (!(Test-Path -LiteralPath $ar) -or !(Test-Path -LiteralPath $objdump)) { throw 'ESP32 archive tools not found' }

foreach ($requested in $BuildPaths) {
  $build = (Resolve-Path -LiteralPath $requested).Path.TrimEnd('\')
  if (![string]::Equals((Split-Path -Parent $build), $cacheRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Not an immediate Arduino sketch cache directory: $build"
  }
  $options = Get-Content -LiteralPath (Join-Path $build 'build.options.json') -Raw | ConvertFrom-Json
  $sketch = ($options.sketchLocation -replace '\\+', '/').TrimEnd('/')
  $name = ($sketch -split '/')[-1]
  if ($name -notin $modules -or ![string]::Equals($sketch, "$moduleRoot/$name", [StringComparison]::OrdinalIgnoreCase)) {
    throw "Cache does not belong to a display in this project: $build"
  }
  $lvgl = (Resolve-Path -LiteralPath (Join-Path $build 'libraries/lvgl')).Path
  if ($Repair) {
    foreach ($temporary in (Get-ChildItem -LiteralPath $lvgl -Filter 'objs.ofe-repair-*.a' -File -Recurse)) {
      if ($temporary.Name -match '^objs\.ofe-repair-[a-f0-9]{32}\.a$' -and
          $temporary.FullName.StartsWith($lvgl + '\', [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $temporary.FullName
      }
    }
  }
  $objectsByDirectory = @{}
  $commandsPath = Join-Path $build 'compile_commands.json'
  if (Test-Path -LiteralPath $commandsPath) {
    $commands = Get-Content -LiteralPath $commandsPath -Raw | ConvertFrom-Json
    foreach ($command in $commands) {
      $argsList = @($command.arguments)
      $outputIndex = [Array]::IndexOf($argsList, '-o')
      if ($outputIndex -lt 0 -or $outputIndex + 1 -ge $argsList.Count) { continue }
      $objectPath = [IO.Path]::GetFullPath($argsList[$outputIndex + 1])
      if (!$objectPath.StartsWith($lvgl + '\', [StringComparison]::OrdinalIgnoreCase) -or
          [IO.Path]::GetExtension($objectPath) -ne '.o') { continue }
      $directory = [IO.Path]::GetDirectoryName($objectPath)
      if (!$objectsByDirectory.ContainsKey($directory)) { $objectsByDirectory[$directory] = @() }
      $objectsByDirectory[$directory] += [IO.Path]::GetFileName($objectPath)
    }
  }
  $archives = @(Get-ChildItem -LiteralPath $lvgl -Filter 'objs.a' -File -Recurse)
  $bad = @()
  foreach ($archive in $archives) {
    $diagnostic = & $objdump -f $archive.FullName 2>&1
    if ($LASTEXITCODE -eq 0) { continue }
    $bad += $archive.FullName
    if (!$Repair) { continue }
    # A damaged long-name table can make even "ar t" fail. The current compiler
    # database supplies authoritative members without including stale object files.
    if ($objectsByDirectory.ContainsKey($archive.DirectoryName)) {
      $members = @($objectsByDirectory[$archive.DirectoryName])
    } else {
      # IDE builds may omit compile_commands.json. Use the installed LVGL source
      # list, never an unfiltered cache glob that might contain obsolete objects.
      $sourceRoot = "$env:USERPROFILE/Documents/Arduino/libraries/lvgl/src"
      $sourceDirectory = Join-Path $sourceRoot ([IO.Path]::GetRelativePath($lvgl, $archive.DirectoryName))
      if (Test-Path -LiteralPath $sourceDirectory -PathType Container) {
        $members = @(Get-ChildItem -LiteralPath $sourceDirectory -File |
          Where-Object { $_.Extension -in @('.c', '.cpp', '.cc', '.cxx', '.s') } |
          ForEach-Object { $_.Name + '.o' })
      } else {
        $members = @(& $ar t $archive.FullName)
        if ($LASTEXITCODE -ne 0) { throw "Cannot read archive members: $archive" }
      }
      if (!$members.Count) { throw "No authoritative source/object list for: $archive" }
    }
    if (@($members | Select-Object -Unique).Count -ne $members.Count) { throw "Duplicate archive members: $archive" }
    $objectPaths = foreach ($member in $members) {
      if ([IO.Path]::GetFileName($member) -ne $member -or $member -notmatch '\.o$') {
        throw "Unsafe or unexpected archive member: $member"
      }
      $object = Join-Path $archive.DirectoryName $member
      if (!(Test-Path -LiteralPath $object -PathType Leaf)) { throw "Missing original object: $object" }
      $object
    }
    $objectPaths = @($objectPaths)
    # Bound the Windows command length while validating objects in batches.
    for ($i = 0; $i -lt $objectPaths.Count; $i += 32) {
      $chunk = @($objectPaths[$i..([Math]::Min($i + 31, $objectPaths.Count - 1))])
      $diagnostic = & $objdump -f @chunk 2>&1
      if ($LASTEXITCODE -ne 0) { throw "Original objects are invalid; rebuild the library instead: $archive" }
    }
    $temporary = Join-Path $archive.DirectoryName ('objs.ofe-repair-' + [Guid]::NewGuid().ToString('N') + '.a')
    if (!$temporary.StartsWith($lvgl + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe temporary archive path' }
    # One archiver invocation with relative object names avoids repeated in-place updates.
    Push-Location -LiteralPath $archive.DirectoryName
    try {
      & $ar crs ([IO.Path]::GetFileName($temporary)) @members
      if ($LASTEXITCODE -ne 0) { throw "Archive rebuild failed: $archive" }
    } finally { Pop-Location }
    $diagnostic = & $objdump -f $temporary 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Rebuilt archive is invalid: $temporary" }
    $relative = [IO.Path]::GetRelativePath($build, $archive.FullName)
    $backup = Join-Path (Join-Path $backupRoot (Split-Path -Leaf $build)) $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
    Copy-Item -LiteralPath $archive.FullName -Destination $backup
    if ((Get-FileHash -LiteralPath $backup).Hash -ne (Get-FileHash -LiteralPath $archive.FullName).Hash) {
      throw "Archive backup verification failed: $backup"
    }
    Move-Item -LiteralPath $temporary -Destination $archive.FullName -Force
    $diagnostic = & $objdump -f $archive.FullName 2>&1
    if ($LASTEXITCODE -ne 0) {
      Copy-Item -LiteralPath $backup -Destination $archive.FullName -Force
      throw "Archive validation failed; restored backup: $archive"
    }
  }
  [PSCustomObject]@{Display=$name;Checked=$archives.Count;Damaged=$bad.Count;Repaired=if($Repair){$bad.Count}else{0};Backups=if($Repair -and $bad.Count){$backupRoot}else{$null}} | ConvertTo-Json -Compress
}
