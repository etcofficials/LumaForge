# Builds dist\LumaForge-v<version>-Setup.exe (NSIS) from the portable package
# folder made by tools\package-release.ps1. NSIS lives on G: (G:\DevTools\nsis).
param([string]$Version = '', [string]$Makensis = 'G:\DevTools\nsis\makensis.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
if ($Version -eq '') {
    $Version = (Get-Item (Join-Path $root 'build\release\bin\LumaForge.exe')).VersionInfo.ProductVersion
}
$src = Join-Path $dist "LumaForge-v$Version-Windows-x64"
if (-not (Test-Path (Join-Path $src 'app\LumaForge.exe'))) { throw "Package folder not found: $src (run tools\package-release.ps1 first)" }
if (-not (Test-Path $Makensis)) { throw "makensis not found: $Makensis" }

# The uninstaller removes exactly the program folders that were installed.
$dirs = Join-Path $env:TEMP 'lumaforge-uninstall-dirs.nsh'
$lines = Get-ChildItem (Join-Path $src 'app') -Directory | ForEach-Object { '    RMDir /r "$INSTDIR\app\' + $_.Name + '"' }
Set-Content -Path $dirs -Value $lines -Encoding ASCII

$out = Join-Path $dist "LumaForge-v$Version-Setup.exe"
$icon = Join-Path $root 'resources\icons\lumaforge.ico'
& $Makensis /V2 "/DVERSION=$Version" "/DSRCDIR=$src" "/DOUTFILE=$out" "/DICON=$icon" "/DDIRS=$dirs" (Join-Path $PSScriptRoot 'installer.nsi')
if ($LASTEXITCODE -ne 0) { throw "makensis failed ($LASTEXITCODE)" }
"Installer: $out ({0:N0} MB)" -f ((Get-Item $out).Length / 1MB)
