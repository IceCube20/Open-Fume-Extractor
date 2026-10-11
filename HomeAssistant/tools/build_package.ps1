$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$component = Join-Path $root 'custom_components/open_fume_extractor'
$version = (Get-Content -LiteralPath (Join-Path $component 'manifest.json') -Raw | ConvertFrom-Json).version
$stage = Join-Path $env:TEMP ("ofe-ha-package-" + [guid]::NewGuid().ToString('N'))
$target = Join-Path $stage 'custom_components/open_fume_extractor'
New-Item -ItemType Directory -Force -Path $target | Out-Null
Get-ChildItem -LiteralPath $component -Recurse -File |
  Where-Object { $_.Extension -in '.py', '.json', '.png', '.js' -and $_.FullName -notmatch '[\\/]__pycache__[\\/]' } |
  ForEach-Object {
    $relative = $_.FullName.Substring($component.Length).TrimStart('\', '/')
    $destination = Join-Path $target $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $destination
  }
foreach ($name in @('README.md', 'LICENSE')) {
  Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $stage $name)
}
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$archive = Join-Path $dist ("open-fume-extractor-ha-" + $version + ".zip")
Compress-Archive -LiteralPath (Join-Path $stage 'custom_components'), (Join-Path $stage 'README.md'), (Join-Path $stage 'LICENSE') -DestinationPath $archive -Force
Get-Item -LiteralPath $archive | Select-Object FullName, Length
Get-FileHash -LiteralPath $archive -Algorithm SHA256
