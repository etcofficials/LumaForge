<#
.SYNOPSIS
  Fetch portable CMake, Ninja and FFmpeg development libraries onto G:.

  CMake   -> G:\DevTools\CMake            (official Kitware portable zip)
  Ninja   -> G:\DevTools\Ninja            (official ninja-build zip)
  FFmpeg  -> G:\LumaForge\third_party\ffmpeg  (BtbN FFmpeg-Builds, win64 shared: headers + .lib + .dll)

  Downloads are cached in G:\Caches\downloads and re-used on later runs.
  Nothing is written to C:.
#>
[CmdletBinding()]
param(
    [string]$DevTools    = 'G:\DevTools',
    [string]$Cache       = 'G:\Caches\downloads',
    [string]$FFmpegDest  = 'G:\LumaForge\third_party\ffmpeg',
    # 'gpl' includes libx264 (GPL); 'lgpl' excludes it. See TOOLS.md for licensing notes.
    [ValidateSet('gpl', 'lgpl')] [string]$FFmpegLicense = 'gpl',
    [string]$FFmpegBranch = 'n8.1'
)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$env:TEMP = 'G:\Caches\tmp'; $env:TMP = 'G:\Caches\tmp'
New-Item -ItemType Directory -Force -Path $Cache, $env:TEMP | Out-Null

function Get-GitHubRelease([string]$Repo, [string]$Tag) {
    $url = if ($Tag) { "https://api.github.com/repos/$Repo/releases/tags/$Tag" } else { "https://api.github.com/repos/$Repo/releases/latest" }
    Invoke-RestMethod -Uri $url -Headers @{ 'User-Agent' = 'LumaForge-toolchain-setup' }
}
function Get-Cached([string]$Url, [string]$Name) {
    $p = Join-Path $Cache $Name
    if (-not (Test-Path $p)) {
        Write-Host "  downloading $Name"
        Invoke-WebRequest -Uri $Url -OutFile "$p.part" -UseBasicParsing -Headers @{ 'User-Agent' = 'LumaForge-toolchain-setup' }
        Move-Item "$p.part" $p
    }
    return $p
}
function Expand-Flat([string]$Zip, [string]$Dest) {
    # Extract to a staging dir, then flatten a single top-level folder into $Dest.
    $stage = Join-Path $env:TEMP ([IO.Path]::GetFileNameWithoutExtension($Zip) + '_stage')
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    Expand-Archive -Path $Zip -DestinationPath $stage
    $top = Get-ChildItem $stage
    $src = if ($top.Count -eq 1 -and $top[0].PSIsContainer) { $top[0].FullName } else { $stage }
    if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
    New-Item -ItemType Directory -Force -Path (Split-Path $Dest) | Out-Null
    Move-Item $src $Dest
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
}

# ---------------------------------------------------------------- CMake
Write-Host '== CMake'
$rel = Get-GitHubRelease 'Kitware/CMake'
$asset = $rel.assets | Where-Object { $_.name -like 'cmake-*-windows-x86_64.zip' } | Select-Object -First 1
$zip = Get-Cached $asset.browser_download_url $asset.name
Expand-Flat $zip (Join-Path $DevTools 'CMake')
& (Join-Path $DevTools 'CMake\bin\cmake.exe') --version | Select-Object -First 1

# ---------------------------------------------------------------- Ninja
Write-Host '== Ninja'
$rel = Get-GitHubRelease 'ninja-build/ninja'
$asset = $rel.assets | Where-Object { $_.name -eq 'ninja-win.zip' } | Select-Object -First 1
$zip = Get-Cached $asset.browser_download_url ("ninja-" + $rel.tag_name + "-win.zip")
Expand-Flat $zip (Join-Path $DevTools 'Ninja')
Write-Host ("  ninja " + (& (Join-Path $DevTools 'Ninja\ninja.exe') --version))

# ---------------------------------------------------------------- FFmpeg
Write-Host "== FFmpeg ($FFmpegBranch, $FFmpegLicense, shared)"
$rel = Get-GitHubRelease 'BtbN/FFmpeg-Builds' 'latest'
$pattern = "ffmpeg-$FFmpegBranch-latest-win64-$FFmpegLicense-shared-*.zip"
$asset = $rel.assets | Where-Object { $_.name -like $pattern } | Select-Object -First 1
if (-not $asset) { throw "No FFmpeg asset matching $pattern" }
$zip = Get-Cached $asset.browser_download_url $asset.name
Expand-Flat $zip $FFmpegDest
# Record provenance next to the libraries.
@{ asset = $asset.name; url = $asset.browser_download_url; size = $asset.size; fetched = (Get-Date -Format s); license = $FFmpegLicense } |
    ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $FFmpegDest 'PROVENANCE.json')
Get-ChildItem (Join-Path $FFmpegDest 'lib') -Filter *.lib | ForEach-Object { "  lib: " + $_.Name }

# ---------------------------------------------------------------- nlohmann/json (single header, MIT)
Write-Host '== nlohmann/json'
$jsonDest = Join-Path (Split-Path $FFmpegDest) 'nlohmann'
New-Item -ItemType Directory -Force -Path $jsonDest | Out-Null
$rel = Get-GitHubRelease 'nlohmann/json' 'v3.11.3'
$asset = $rel.assets | Where-Object { $_.name -eq 'json.hpp' } | Select-Object -First 1
$hpp = Get-Cached $asset.browser_download_url ('json-' + $rel.tag_name + '.hpp')
Copy-Item $hpp (Join-Path $jsonDest 'json.hpp') -Force
Write-Host ("  " + $rel.tag_name + " -> " + $jsonDest)
Write-Host '== Done'
