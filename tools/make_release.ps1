# Builds Release and packs the player download: dist\BlitzTheLeague-<version>-win64.zip
# Contents: blitz.exe, the SDK DLLs, README.txt and licenses\. Never any game data.
#
# The version is blitz/CMakeLists.txt project(VERSION): the exe shows it and its updater
# compares it with GitHub's latest release, so bump it there. Keep the zip name ending in
# -win64.zip: the updater looks for it.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 [-Version 1.1.0] [-NoBuild]
param(
  [string]$Version,
  [switch]$NoBuild
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$cmakeVersion = [regex]::Match((Get-Content (Join-Path $root "blitz\CMakeLists.txt") -Raw),
  'project\(\w+ VERSION ([0-9.]+)').Groups[1].Value
if (-not $cmakeVersion) { throw "no project(... VERSION x.y.z) in blitz\CMakeLists.txt" }
if (-not $Version) { $Version = $cmakeVersion }
if ($Version -ne $cmakeVersion) { throw "-Version $Version differs from CMakeLists.txt's ${cmakeVersion}: bump it there" }
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
# RecompLauncher reads this next to the exe (Easy Setup, settings): docs/ADD-SUPPORT.md upstream
Copy-Item (Join-Path $root "recomplauncher.json") $stage

# Licenses: this project, the SDK (covers Xenia), and every SDK third-party library.
$lic = Join-Path $stage "licenses"
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $root "LICENSE") (Join-Path $lic "blitz-recomp.txt")
Copy-Item (Join-Path $root "THIRD_PARTY_NOTICES.md") $lic
Copy-Item (Join-Path $sdk "LICENSE") (Join-Path $lic "rexglue-sdk.txt")
Get-ChildItem (Join-Path $sdk "thirdparty") -Directory | ForEach-Object {
  $files = Get-ChildItem $_.FullName -File | Where-Object { $_.Name -match '^(LICEN[SC]E|COPYING)' }
  foreach ($f in $files) {
    $dest = "$($_.Name)-$($f.Name)"
    if ($dest -notlike "*.txt") { $dest += ".txt" }
    Copy-Item $f.FullName (Join-Path $lic $dest)
  }
}

# Guard: nothing from the game may end up in the download.
$bad = Get-ChildItem $stage -Recurse -File |
  Where-Object { $_.Extension -in '.xex', '.iso', '.mpk', '.of', '.obf', '.lf', '.sfd' -or $_.Name -eq 'blitz.toml' }
if ($bad) { throw "game or user files in the release: $($bad.FullName -join ', ')" }

Compress-Archive -Path $stage -DestinationPath $zip
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
"Release: $zip ($size MB)"
