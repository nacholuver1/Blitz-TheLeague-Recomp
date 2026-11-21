# Builds Release and packs the player download: dist\BlitzTheLeague-<version>-win64.zip
# Contents: blitz.exe, the SDK DLLs, README.txt and licenses\. Never any game data.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 -Version 1.0.0 [-NoBuild]
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [switch]$NoBuild
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root "blitz\out\build\win-amd64\Release"
$sdk = Join-Path $root "deps\rexglue-sdk"
$name = "BlitzTheLeague-$Version-win64"
$dist = Join-Path $root "dist"
$stage = Join-Path $dist $name
$zip = Join-Path $dist "$name.zip"

if (-not $NoBuild) {
  Push-Location $PSScriptRoot
  try {
    cmd /c ".\build_blitz.bat release"
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
  } finally { Pop-Location }
}

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $bin "blitz.exe") $stage
Get-ChildItem $bin -Filter *.dll | Copy-Item -Destination $stage
Copy-Item (Join-Path $root "release\README.txt") $stage

# Licenses: this project, the SDK (covers Xenia), and every SDK third-party library.
$lic = Join-Path $stage "licenses"
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $root "LICENSE") (Join-Path $lic "blitz-recomp.txt")
Copy-Item (Join-Path $root "THIRD_PARTY_NOTICES.md") $lic
Copy-Item (Join-Path $sdk "LICENSE") (Join-Path $lic "rexglue-sdk.txt")
Get-ChildItem (Join-Path $sdk "thirdparty") -Directory | ForEach-Object {
  $files = Get-ChildItem $_.FullName -File | Where-Object { $_.Name -match '^(LICEN[SC]E|COPYING)' }
  foreach ($f in $files) {
    Copy-Item $f.FullName (Join-Path $lic "$($_.Name)-$($f.Name).txt")
  }
}

# Guard: nothing from the game may end up in the download.
$bad = Get-ChildItem $stage -Recurse -File |
  Where-Object { $_.Extension -in '.xex', '.iso', '.mpk', '.of', '.obf', '.lf', '.sfd' -or $_.Name -eq 'blitz.toml' }
if ($bad) { throw "game or user files in the release: $($bad.FullName -join ', ')" }

Compress-Archive -Path $stage -DestinationPath $zip
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
"Release: $zip ($size MB)"
