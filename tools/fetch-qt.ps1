<#
.SYNOPSIS
  Install Qt 6 (MSVC 2022 x64, minimal modules) under G:\Qt using aqtinstall.

  aqtinstall downloads the official prebuilt archives from download.qt.io
  (the same files the Qt Online Installer uses) - no Qt account, no installer.

  Components:  qtbase (Core/Gui/Widgets/...), qtsvg (SVG icon plugin), qttools (windeployqt)
  Python venv: G:\DevTools\pyvenv   (aqtinstall lives here, not in the C: Python)
  Caches:      G:\Caches\pip, G:\Caches\qt
#>
[CmdletBinding()]
param(
    [string]$QtRoot   = 'G:\Qt',
    [string]$Version  = '6.8.3',
    [string]$Arch     = 'win64_msvc2022_64',
    [string[]]$Archives = @('qtbase', 'qtsvg', 'qttools'),
    [string]$Venv     = 'G:\DevTools\pyvenv'
)
# Native tools (pip, aqt) log to stderr; treat only non-zero exit codes as failures.
$ErrorActionPreference = 'Continue'
$env:TEMP = 'G:\Caches\tmp'; $env:TMP = 'G:\Caches\tmp'
$env:PIP_CACHE_DIR = 'G:\Caches\pip'
New-Item -ItemType Directory -Force -Path $QtRoot, 'G:\Caches\qt', $env:TEMP, $env:PIP_CACHE_DIR | Out-Null

if (-not (Test-Path (Join-Path $Venv 'Scripts\python.exe'))) {
    Write-Host "== Creating Python venv at $Venv"
    python -m venv $Venv
}
$py = Join-Path $Venv 'Scripts\python.exe'
Write-Host '== Installing aqtinstall into the venv'
& $py -m pip install --quiet --upgrade pip aqtinstall
if ($LASTEXITCODE -ne 0) { throw "pip install aqtinstall failed ($LASTEXITCODE)" }
& $py -m aqt version

Write-Host "== Installing Qt $Version $Arch -> $QtRoot  (archives: $($Archives -join ', '))"
& $py -m aqt install-qt windows desktop $Version $Arch --outputdir $QtRoot --archives @Archives --keep --archive-dest 'G:\Caches\qt'
if ($LASTEXITCODE -ne 0) { throw "aqt install-qt failed ($LASTEXITCODE)" }
if (-not (Test-Path (Join-Path $QtRoot "$Version\msvc2022_64\bin\Qt6Core.dll"))) { throw "Qt6Core.dll missing after install" }

$prefix = Join-Path $QtRoot "$Version\msvc2022_64"
Write-Host "== Installed:"
& (Join-Path $prefix 'bin\qmake.exe') -query QT_VERSION
Get-ChildItem (Join-Path $prefix 'lib\cmake') -Directory | Where-Object { $_.Name -in 'Qt6', 'Qt6Core', 'Qt6Gui', 'Qt6Widgets', 'Qt6Svg' } | ForEach-Object { "  cmake package: " + $_.Name }
