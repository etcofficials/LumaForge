<#
.SYNOPSIS
  Verify the LumaForge portable toolchain end-to-end and print a summary.

  Tests
    1  cl.exe compiles                      7  FFmpeg headers found by CMake
    2  link.exe links                       8  FFmpeg libraries link + load
    3  rc.exe compiles resources, mt.exe embeds a manifest
    4  CMake runs                           9  Windows SDK headers/libs usable
    5  Ninja runs                          10  tiny Win32 C++ executable runs
    6  Qt found by CMake                   11  tiny Qt Widgets application runs

  Build products go to G:\LumaForge\build\toolchain-check. Exit code 0 == all passed.
#>
[CmdletBinding()]
param([switch]$KeepBuild)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'setup-environment.ps1') -Quiet

$src   = Join-Path $PSScriptRoot 'toolchain-check'
$build = Join-Path $repo 'build\toolchain-check'
if (Test-Path $build) { Remove-Item -Recurse -Force $build }
New-Item -ItemType Directory -Force -Path $build | Out-Null

$results = [ordered]@{}
$info    = [ordered]@{}
function Step([string]$Name, [scriptblock]$Body) {
    Write-Host ("-- {0,-52}" -f $Name) -NoNewline
    try {
        $out = & $Body
        $script:results[$Name] = 'PASS'
        Write-Host ' PASS' -ForegroundColor Green
        if ($out) { $out | ForEach-Object { Write-Host "     $_" -ForegroundColor DarkGray } }
    } catch {
        $script:results[$Name] = 'FAIL'
        Write-Host ' FAIL' -ForegroundColor Red
        Write-Host "     $($_.Exception.Message)" -ForegroundColor Red
    }
}
function Run([string]$Exe, [string[]]$Arguments, [string]$Cwd = $build) {
    Push-Location $Cwd
    try {
        $out = @(& $Exe @Arguments 2>&1 | ForEach-Object { "$_" })
        if ($LASTEXITCODE -ne 0) { throw "$([IO.Path]::GetFileName($Exe)) exited $LASTEXITCODE`n" + ($out -join "`n") }
        return $out
    } finally { Pop-Location }
}

Write-Host "`nLumaForge toolchain diagnostic`n"

# ---- 1,2,9,10 : cl + link + SDK + run -------------------------------------
Step '1/2/9  cl.exe + link.exe + Windows SDK headers/libs' {
    $o = Run 'cl.exe' @('/nologo', '/EHsc', '/std:c++20', '/W4', '/O2', '/DUNICODE', '/D_UNICODE',
                        (Join-Path $src 'hello_win32.cpp'), '/Fe:hello_win32.exe', '/link', 'kernel32.lib', 'user32.lib')
    if (-not (Test-Path (Join-Path $build 'hello_win32.exe'))) { throw 'hello_win32.exe not produced' }
    $script:info['Compiler'] = (Get-Command cl.exe).Source
    $banner = & { $ErrorActionPreference = 'Continue'; (& cl.exe 2>&1 | Out-String) }
    if ($banner -match 'Version ([\d\.]+)') { $script:info['MSVC version'] = "$($Matches[1]) (toolset $env:VCToolsVersion)" }
}
Step '3      rc.exe (resources) + mt.exe (manifest)' {
    Run 'rc.exe' @('/nologo', '/fo', 'hello_win32.res', (Join-Path $src 'hello_win32.rc')) | Out-Null
    Run 'cl.exe' @('/nologo', '/EHsc', '/std:c++20', '/O2', (Join-Path $src 'hello_win32.cpp'), 'hello_win32.res',
                   '/Fe:hello_win32_rc.exe', '/link', 'kernel32.lib') | Out-Null
    Run 'mt.exe' @('-nologo', '-manifest', (Join-Path $src 'hello_win32.manifest'), '-outputresource:hello_win32_rc.exe;1') | Out-Null
    $v = (Get-Item (Join-Path $build 'hello_win32_rc.exe')).VersionInfo
    if ($v.ProductName -ne 'LumaForge') { throw "VERSIONINFO not embedded (ProductName='$($v.ProductName)')" }
    "rc.exe: " + (Get-Command rc.exe).Source
    "mt.exe: " + (Get-Command mt.exe).Source
}
Step '10     tiny Win32 executable runs' {
    Run (Join-Path $build 'hello_win32_rc.exe') @()
}

# ---- 4,5 : cmake + ninja --------------------------------------------------
Step '4      CMake runs' {
    $o = @(Run 'cmake.exe' @('--version')); $script:info['CMake'] = ($o[0] -replace 'cmake version ', '') + '  ' + (Get-Command cmake.exe).Source; $o[0]
}
Step '5      Ninja runs' {
    $o = @(Run 'ninja.exe' @('--version')); $script:info['Ninja'] = "$($o[0])  " + (Get-Command ninja.exe).Source; "ninja $($o[0])"
}

# ---- 6,7,8,11 : CMake project with Qt + FFmpeg ---------------------------
$cm = Join-Path $build 'cmake'
Step '6/7    CMake configures Qt 6 + FFmpeg project (Ninja)' {
    $o = Run 'cmake.exe' @('-S', $src, '-B', $cm, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                           "-DCMAKE_PREFIX_PATH=$env:LUMAFORGE_QT_PREFIX", "-DFFMPEG_ROOT=$env:LUMAFORGE_FFMPEG_ROOT")
    foreach ($line in $o) {
        if ($line -match '^-- LF_(\w+)=(.*)$') { $script:info["_$($Matches[1])"] = $Matches[2] }
    }
    "Qt6_DIR      = " + $script:info['_QT_DIR']
    "FFmpeg root  = " + $script:info['_FFMPEG_ROOT']
}
Step '8/11   Ninja builds qt_check + ffmpeg_check' {
    Run 'cmake.exe' @('--build', $cm) | Select-Object -Last 1
}
Step '8      ffmpeg_check runs (libraries load)' {
    Run (Join-Path $cm 'ffmpeg_check.exe') @() -Cwd (Join-Path $env:LUMAFORGE_FFMPEG_ROOT 'bin')
}
Step '11     qt_check runs (window + platform plugin)' {
    Run (Join-Path $cm 'qt_check.exe') @()
}

# ---- summary --------------------------------------------------------------
$info['Windows SDK']  = "$env:LUMAFORGE_SDK_VERSION  $env:LUMAFORGE_SDK_ROOT"
$info['Qt']           = "$($info['_QT_VERSION'])  $env:LUMAFORGE_QT_PREFIX"
$info['FFmpeg']       = "$($info['_FFMPEG_VERSION']) [$($info['_FFMPEG_LICENSE'])]  $env:LUMAFORGE_FFMPEG_ROOT"
$info['Architecture'] = 'x64 host -> x64 target'
$info['Generator']    = "Ninja  ($($info['_MAKE_PROGRAM']))"

Write-Host "`n==================== Toolchain summary ===================="
foreach ($k in 'Compiler', 'MSVC version', 'Windows SDK', 'CMake', 'Ninja', 'Qt', 'FFmpeg', 'Architecture', 'Generator') {
    Write-Host ("{0,-14} {1}" -f "${k}:", $info[$k])
}
Write-Host "`nPaths in use:"
Write-Host ("  MSVC       " + $env:LUMAFORGE_MSVC_ROOT)
Write-Host ("  SDK        " + $env:LUMAFORGE_SDK_ROOT)
Write-Host ("  rc.exe     " + $info['_RC_COMPILER'])
Write-Host ("  mt.exe     " + $info['_MT'])
Write-Host ("  Qt6Config  " + $info['_QT_DIR'])
Write-Host ("  Build dir  " + $build)

$failed = @($results.GetEnumerator() | Where-Object { $_.Value -ne 'PASS' })
Write-Host ""
if ($failed.Count -eq 0) {
    Write-Host "ALL $($results.Count) CHECKS PASSED" -ForegroundColor Green
} else {
    Write-Host "$($failed.Count) of $($results.Count) checks FAILED:" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  - $($_.Key)" -ForegroundColor Red }
}
if (-not $KeepBuild -and $failed.Count -eq 0) { Remove-Item -Recurse -Force $build -ErrorAction SilentlyContinue }
exit $failed.Count
