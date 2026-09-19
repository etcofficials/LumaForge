<#
.SYNOPSIS
  Configure the CURRENT PowerShell session for the portable MSVC + Windows SDK on G:.
  Equivalent of "vcvars64.bat" for the portable toolchain. Dot-source it:

      . G:\DevTools\setup-msvc.ps1

  Sets PATH, INCLUDE, LIB, LIBPATH and the usual VCTools*/WindowsSdk* variables
  for x64 host -> x64 target. Nothing is persisted; nothing touches the registry.

  Layout expected (created by G:\LumaForge\tools\fetch-portable-msvc.py):
      G:\DevTools\MSVC\<toolset-version>\{bin,include,lib}
      G:\DevTools\MSVC\Redist\...
      G:\DevTools\WindowsSDK\{Include,Lib,bin}\<sdk-version>
#>
[CmdletBinding()]
param(
    [string]$DevTools = 'G:\DevTools',
    [switch]$Quiet
)

if ($env:LUMAFORGE_MSVC_ENV -eq '1') {
    if (-not $Quiet) { Write-Host "portable MSVC environment already active (MSVC $env:VCToolsVersion, SDK $env:WindowsSDKVersion)" }
    return
}

$msvcRoot = Join-Path $DevTools 'MSVC'
$sdkRoot  = Join-Path $DevTools 'WindowsSDK'
if (-not (Test-Path $msvcRoot) -or -not (Test-Path (Join-Path $sdkRoot 'Include'))) {
    throw "Portable toolchain not found under $DevTools. Run G:\LumaForge\tools\fetch-portable-msvc.py first."
}

$msvcVer = Get-ChildItem $msvcRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
           Sort-Object { [version]$_.Name } | Select-Object -Last 1 -ExpandProperty Name
$sdkVer  = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
           Sort-Object { [version]$_.Name } | Select-Object -Last 1 -ExpandProperty Name
if (-not $msvcVer) { throw "No MSVC toolset directory under $msvcRoot" }
if (-not $sdkVer)  { throw "No Windows SDK version directory under $sdkRoot\Include" }

$vc = Join-Path $msvcRoot $msvcVer

# Same variable names the official vcvars scripts export, so tools that look
# for them (CMake, Qt Creator, various build scripts) behave normally.
$env:VCToolsInstallDir   = "$vc\"
$env:VCToolsVersion      = $msvcVer
$env:VCINSTALLDIR        = "$msvcRoot\"
$env:WindowsSdkDir       = "$sdkRoot\"
$env:WindowsSDKVersion   = "$sdkVer\"
$env:WindowsSdkBinPath   = "$sdkRoot\bin\"
$env:WindowsSdkVerBinPath= "$sdkRoot\bin\$sdkVer\"
$env:WindowsLibPath      = "$sdkRoot\UnionMetadata\$sdkVer;$sdkRoot\References\$sdkVer"
$env:UCRTVersion         = $sdkVer
$env:UniversalCRTSdkDir  = "$sdkRoot\"
$env:VSCMD_ARG_HOST_ARCH = 'x64'
$env:VSCMD_ARG_TGT_ARCH  = 'x64'
$env:Platform            = 'x64'

$env:INCLUDE = @(
    "$vc\include",
    "$sdkRoot\Include\$sdkVer\ucrt",
    "$sdkRoot\Include\$sdkVer\shared",
    "$sdkRoot\Include\$sdkVer\um",
    "$sdkRoot\Include\$sdkVer\winrt",
    "$sdkRoot\Include\$sdkVer\cppwinrt"
) -join ';'

$env:LIB = @(
    "$vc\lib\x64",
    "$sdkRoot\Lib\$sdkVer\ucrt\x64",
    "$sdkRoot\Lib\$sdkVer\um\x64"
) -join ';'
$env:LIBPATH = "$vc\lib\x64"

# Debug builds need the debug CRT DLLs (msvcp140d/vcruntime140d/ucrtbased) at run time.
$debugCrt = Get-ChildItem (Join-Path $msvcRoot 'Redist') -Directory -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*debug_nonredist\x64\Microsoft.VC*.DebugCRT' } | Select-Object -First 1 -ExpandProperty FullName
$debugUcrt = Join-Path $msvcRoot 'Redist\debug_ucrt_x64'

$pathAdd = @(
    "$vc\bin\Hostx64\x64",
    "$sdkRoot\bin\$sdkVer\x64",
    "$sdkRoot\bin\$sdkVer\x64\ucrt"
)
if ($debugCrt)              { $pathAdd += $debugCrt }
if (Test-Path $debugUcrt)   { $pathAdd += $debugUcrt }
$env:PATH = ($pathAdd -join ';') + ';' + $env:PATH

$env:LUMAFORGE_MSVC_ENV = '1'
$env:LUMAFORGE_MSVC_ROOT = $vc
$env:LUMAFORGE_SDK_ROOT  = $sdkRoot
$env:LUMAFORGE_SDK_VERSION = $sdkVer

if (-not $Quiet) {
    Write-Host "portable MSVC $msvcVer  ($vc)"
    Write-Host "Windows SDK   $sdkVer  ($sdkRoot)"
}
