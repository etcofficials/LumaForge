# Creates "LumaForge" shortcuts (Desktop and Start Menu) to a LumaForge package folder.
# Only two small .lnk files are written; remove them with -Remove.
param([string]$Package = '', [switch]$Remove)
$ErrorActionPreference = 'Stop'
if ($Package -eq '') {
    $Package = Get-ChildItem (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist') -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}
$launcher = Join-Path $Package 'LumaForge Launcher.exe'
$targets = @((Join-Path ([Environment]::GetFolderPath('Desktop')) 'LumaForge.lnk'),
             (Join-Path ([Environment]::GetFolderPath('Programs')) 'LumaForge.lnk'))
if ($Remove) { foreach ($t in $targets) { if (Test-Path $t) { Remove-Item $t -Force; "removed $t" } }; return }
if (-not (Test-Path $launcher)) { throw "Launcher not found: $launcher" }
$shell = New-Object -ComObject WScript.Shell
foreach ($t in $targets) {
    $lnk = $shell.CreateShortcut($t)
    $lnk.TargetPath = $launcher
    $lnk.WorkingDirectory = $Package
    $lnk.IconLocation = "$launcher,0"
    $lnk.Description = 'LumaForge video editor'
    $lnk.Save()
    "created $t -> $launcher"
}
