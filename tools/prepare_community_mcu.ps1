param([string]$ProjectRoot = (Join-Path $PSScriptRoot '..'))
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$destination = [IO.Path]::GetFullPath((Join-Path $root 'tools/tests/build-checks/community-mcu'))
if (!$destination.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Community source snapshot must remain inside the project.'
}
$hashes = [ordered]@{}
foreach ($name in @('OpenFumeExtractorMCU_S3', 'OpenFumeExtractorMCU_P4')) {
  $source = (Resolve-Path -LiteralPath (Join-Path $root "OpenFumeExtractorMaster/$name")).Path
  foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File) {
    $relative = $file.FullName.Substring($source.Length + 1)
    if ($relative -match '(^|[\\/])(build|data|__pycache__|\.[^\\/]+)([\\/]|$)' -or
        $file.Name -match '(?i)privateconfig|MasterBoardLocalConfig|\.(pem|key|bin|elf|map|o|a|d)$') {continue}
    if ($file.Extension -notin @('.ino','.h','.hpp','.c','.cpp','.csv','.json','.md','.txt') -and
        $file.Name -notin @('LICENSE','NOTICE')) {continue}
    $target = Join-Path $destination "$name/$relative"
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
    if (!(Test-Path -LiteralPath $target) -or (Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash.ToLowerInvariant() -ne $hash) {
      New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
      Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    }
    $hashes[("$name/$relative".Replace('\','/'))] = $hash
  }
}
New-Item -ItemType Directory -Force -Path $destination | Out-Null
if (Get-ChildItem -LiteralPath $destination -Recurse -File | Where-Object {$_.Name -match '(?i)privateconfig|MasterBoardLocalConfig|\.(pem|key)$'}) {
  throw 'Private/local configuration detected in community snapshot; preserved, but not approved for publication.'
}
$manifest = [ordered]@{ purpose='Community sources only; NOT a compiled release'; private_config_copied=$false; files_sha256=$hashes }
[IO.File]::WriteAllText((Join-Path $destination 'community-source-manifest.json'), ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
Write-Output "Community sources prepared, NOT compiled or flashed: $destination"
