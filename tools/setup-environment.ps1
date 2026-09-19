<#
.SYNOPSIS
  Initialise the LumaForge development environment in the CURRENT PowerShell session.
  Run this after every reboot / in every new shell before building:

      . G:\LumaForge\tools\setup-environment.ps1

  It configures:
    - portable MSVC + Windows SDK     (via G:\DevTools\setup-msvc.ps1)
    - CMake, Ninja                    (G:\DevTools\CMake, G:\DevTools\Ninja)
    - Qt 6                            (G:\Qt\<version>\msvc2022_64)
    - FFmpeg runtime DLLs             (G:\LumaForge\third_party\ffmpeg\bin)
    - caches for pip / TEMP on G:     (session only)
    - LUMAFORGE_* variables consumed by CMakePresets.json

  Nothing is persisted to the registry or the system PATH.
#>
[CmdletBinding()]
param(
    [string]$DevTools = 'G:\DevTools',
    [string]$QtRoot   = 'G:\Qt',
    [switch]$Quiet
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if ($env:LUMAFORGE_ENV -eq '1') {
    if (-not $Quiet) { Write-Host 'LumaForge environment already active.' }
    return
}

# 1. Compiler + SDK
. (Join-Path $PSScriptRoot 'setup-msvc.ps1') -DevTools $DevTools -Quiet:$Quiet

# 2. CMake + Ninja
$cmakeBin = Join-Path $DevTools 'CMake\bin'
$ninjaDir = Join-Path $DevTools 'Ninja'
foreach ($p in @($cmakeBin, $ninjaDir)) {
    if (-not (Test-Path $p)) { throw "Missing $p - run G:\LumaForge\tools\fetch-portable-tools.ps1" }
}

# 3. Qt (newest 6.x under G:\Qt with an msvc2022_64 build)
$qtPrefix = Get-ChildItem $QtRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^6\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'msvc2022_64\bin\Qt6Core.dll')) } |
            Sort-Object { [version]$_.Name } | Select-Object -Last 1 |
            ForEach-Object { Join-Path $_.FullName 'msvc2022_64' }
if (-not $qtPrefix) { throw "No Qt 6 msvc2022_64 build under $QtRoot - run G:\LumaForge\tools\fetch-qt.ps1" }

# 4. FFmpeg
$ffmpegRoot = Join-Path $repo 'third_party\ffmpeg'
if (-not (Test-Path (Join-Path $ffmpegRoot 'include\libavcodec\avcodec.h'))) {
    throw "FFmpeg dev libraries not found in $ffmpegRoot - run G:\LumaForge\tools\fetch-portable-tools.ps1"
}

$env:PATH = "$cmakeBin;$ninjaDir;$qtPrefix\bin;$ffmpegRoot\bin;" + $env:PATH

# 5. Variables consumed by CMakePresets.json / scripts
$env:LUMAFORGE_ROOT         = $repo
$env:LUMAFORGE_DEVTOOLS     = $DevTools
$env:LUMAFORGE_QT_PREFIX    = $qtPrefix
$env:LUMAFORGE_FFMPEG_ROOT  = $ffmpegRoot
$env:CMAKE_GENERATOR        = 'Ninja'

# 6. Keep caches / temp off C: for this session
$env:PIP_CACHE_DIR = 'G:\Caches\pip'
$env:TEMP = 'G:\Caches\tmp'; $env:TMP = 'G:\Caches\tmp'
New-Item -ItemType Directory -Force -Path $env:TEMP, $env:PIP_CACHE_DIR | Out-Null

$env:LUMAFORGE_ENV = '1'

if (-not $Quiet) {
    Write-Host ("CMake         " + ((& "$cmakeBin\cmake.exe" --version | Select-Object -First 1) -replace 'cmake version ', '') + "  ($cmakeBin)")
    Write-Host ("Ninja         " + (& "$ninjaDir\ninja.exe" --version) + "  ($ninjaDir)")
    Write-Host ("Qt            " + (& "$qtPrefix\bin\qmake.exe" -query QT_VERSION) + "  ($qtPrefix)")
    Write-Host ("FFmpeg        " + $ffmpegRoot)
    Write-Host 'LumaForge environment ready.  Build with:  cmake --preset debug ; cmake --build --preset debug'
}
