<#
.SYNOPSIS
  Optional: install Debugging Tools for Windows (cdb, symbol tools) under
  G:\DevTools\WindowsSDK\Debuggers\x64 without touching C:.

  The debuggers are not part of the Visual Studio manifest that
  fetch-portable-msvc.py reads, so they come from the Windows SDK web
  installer's /layout mode (download only, no install), and the resulting MSI is
  admin-extracted with msiexec /a (again: no install, no registry).

  Use: cdb.exe -g -G -o -cf cmds.txt G:\LumaForge\build\debug\bin\LumaForge.exe
  where cmds.txt contains e.g. ".sympath G:\LumaForge\build\debug\bin", "g", "kv 50", "q".
#>
[CmdletBinding()]
param(
    [string]$Dest      = 'G:\DevTools\WindowsSDK\Debuggers',
    [string]$Cache     = 'G:\Caches\msvc',
    [string]$SetupUrl  = 'https://go.microsoft.com/fwlink/?linkid=2286561'   # winsdksetup.exe (SDK 10.0.26100)
)
$ErrorActionPreference = 'Stop'
$env:TEMP = 'G:\Caches\tmp'; $env:TMP = 'G:\Caches\tmp'
New-Item -ItemType Directory -Force -Path $Cache, $env:TEMP | Out-Null

if (Test-Path (Join-Path $Dest 'x64\cdb.exe')) {
    Write-Host "== Debuggers already present at $Dest\x64"
    & (Join-Path $Dest 'x64\cdb.exe') -version
    return
}

$setup = Join-Path $Cache 'winsdksetup.exe'
if (-not (Test-Path $setup)) {
    Write-Host "== Downloading winsdksetup.exe -> $setup"
    Invoke-WebRequest -Uri $SetupUrl -OutFile $setup -UseBasicParsing
}

$layout = Join-Path $Cache 'sdklayout'
$msi = Join-Path $layout 'Installers\X64 Debuggers And Tools-x64_en-us.msi'
if (-not (Test-Path $msi)) {
    Write-Host "== Creating SDK layout (debuggers only) in $layout"
    $p = Start-Process $setup -ArgumentList "/layout `"$layout`" /features OptionId.WindowsDesktopDebuggers /quiet /norestart" -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "winsdksetup /layout failed ($($p.ExitCode))" }
    if (-not (Test-Path $msi)) { throw "layout did not produce $msi" }
}

$stage = Join-Path $Cache 'dbg_stage'
if (Test-Path $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
Write-Host "== Admin-extracting debugger MSI to $stage"
$p = Start-Process msiexec.exe -ArgumentList "/a `"$msi`" /qn TARGETDIR=$stage" -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "msiexec /a failed ($($p.ExitCode))" }

$src = Join-Path $stage 'Windows Kits\10\Debuggers\x64'
if (-not (Test-Path (Join-Path $src 'cdb.exe'))) { throw "cdb.exe not found under $src" }
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Move-Item -LiteralPath $src -Destination (Join-Path $Dest 'x64') -Force
Remove-Item -Recurse -Force -LiteralPath $stage

Write-Host "== Installed:"
& (Join-Path $Dest 'x64\cdb.exe') -version
