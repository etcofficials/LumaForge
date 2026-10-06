# Builds the portable package dist\LumaForge-v<version>-Windows-x64.zip from
# build\release\bin. Everything stays on G:. Run after:
#   cmake --preset release ; cmake --build --preset release
param([string]$Version = '', [switch]$NoZip)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build\release\bin'
if (-not (Test-Path (Join-Path $bin 'LumaForge.exe'))) { throw "Release build not found in $bin" }
if ($Version -eq '') {
    $Version = (Get-Item (Join-Path $bin 'LumaForge.exe')).VersionInfo.ProductVersion
}
$name = "LumaForge-v$Version-Windows-x64"
$dist = Join-Path $root 'dist'
$out = Join-Path $dist $name
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $out 'app') | Out-Null

# app\: the runtime, without tests, debug files and test-only plugins.
$exclude = @('lf_tests.exe', 'lf_ui_tests.exe', 'LumaForge Launcher.exe')
Get-ChildItem $bin -File | Where-Object {
    $exclude -notcontains $_.Name -and $_.Extension -notin @('.pdb', '.ilk', '.lib', '.exp', '.log', '.ini')
} | Copy-Item -Destination (Join-Path $out 'app')
foreach ($dir in Get-ChildItem $bin -Directory) {
    if ($dir.Name -in @('logs', 'LumaForge', 'LumaForgeTests')) { continue }
    Copy-Item $dir.FullName -Destination (Join-Path $out 'app') -Recurse
}
# The offscreen platform plugin is only for the UI tests.
Get-ChildItem (Join-Path $out 'app\platforms') -Filter 'qoffscreen*' -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem (Join-Path $out 'app') -Recurse -Filter '*.pdb' | Remove-Item -Force

Copy-Item (Join-Path $bin 'LumaForge Launcher.exe') -Destination $out
New-Item -ItemType Directory -Force (Join-Path $out 'docs') | Out-Null
Copy-Item (Join-Path $root 'docs\USER_GUIDE.md') -Destination (Join-Path $out 'docs')
New-Item -ItemType Directory -Force (Join-Path $out 'licenses') | Out-Null
Copy-Item (Join-Path $root 'third_party\ffmpeg\LICENSE.txt') -Destination (Join-Path $out 'licenses\FFmpeg-LICENSE.txt')
Copy-Item (Join-Path $root 'third_party\ffmpeg\PROVENANCE.json') -Destination (Join-Path $out 'licenses\FFmpeg-PROVENANCE.json')

$readme = @"
LumaForge $Version - portable video editor for Windows 10/11 (x64)

Start:   double-click "LumaForge Launcher.exe"
Guide:   docs\USER_GUIDE.md
Data:    a LumaForgeData folder on the drive LumaForge runs from
         (projects, exports, proxies, caches, autosave, backups, logs).
         Nothing is written to your user profile or the registry.
Remove:  delete this folder (and LumaForgeData if you no longer need it).

Requirements: Direct3D 11 graphics (feature level 11_0) recommended; without
it LumaForge previews in software. Exports use x264/x265 on the CPU or a
hardware encoder when one is detected.

Third-party software:
 - Qt $((Get-Item (Join-Path $bin 'Qt6Core.dll')).VersionInfo.ProductVersion) (LGPL v3, dynamically linked; source: https://download.qt.io)
 - FFmpeg (GPL build, see licenses\FFmpeg-LICENSE.txt and
   licenses\FFmpeg-PROVENANCE.json for the exact build and its source)
Because the FFmpeg build is GPL, this package as a whole is distributed
under the terms of the GPL.
"@
Set-Content -Path (Join-Path $out 'README.txt') -Value $readme -Encoding ASCII

$size = (Get-ChildItem $out -Recurse -File | Measure-Object Length -Sum).Sum
"Package folder: $out ({0:N0} MB)" -f ($size / 1MB)
if (-not $NoZip) {
    $zip = Join-Path $dist "$name.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal
    "ZIP: $zip ({0:N0} MB)" -f ((Get-Item $zip).Length / 1MB)
}
