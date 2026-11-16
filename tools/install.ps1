# Installs (or removes) GScreen for the current user: no admin rights needed.
#   install.ps1                 copy out\GScreen → %LOCALAPPDATA%\Programs\GScreen + Start menu shortcut
#   install.ps1 -Source <dir>   install from another folder (e.g. an unzipped release)
#   install.ps1 -Uninstall      stop GScreen, restore the plain monitor, remove files and shortcut
param(
    [string]$Source = "$PSScriptRoot\..\out\GScreen",
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$target = Join-Path $env:LOCALAPPDATA 'Programs\GScreen'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'GScreen.lnk'

function Stop-GScreen {
    # The settings window, the watchdog instance of the core (and the pre-0.1.2 GScreen.Watchdog.exe) go first,
    # so killing the core below does not trigger a crash restore while files are being replaced.
    foreach ($name in 'GScreen.App', 'GScreen.Watchdog') { Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force }
    Get-CimInstance Win32_Process -Filter "Name = 'GScreen.Core.exe'" |
        Where-Object { $_.CommandLine -like '*--watchdog*' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Get-Process 'GScreen.Core' -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    # Put the plain monitor back in case the core was split.
    if (Test-Path "$target\GScreen.Core.exe") { Start-Process "$target\GScreen.Core.exe" -ArgumentList '--restore' -Wait }
}

Stop-GScreen

if ($Uninstall) {
    Remove-Item $target -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $shortcut -Force -ErrorAction SilentlyContinue
    Write-Output "GScreen removed. Settings remain in $env:APPDATA\GScreen (delete it to reset)."
    return
}

$Source = (Resolve-Path $Source).Path
if (-not (Test-Path "$Source\GScreen.App.exe")) { throw "No GScreen build in $Source (run build.cmd first)" }
if (Test-Path $target) { Remove-Item $target -Recurse -Force }
New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item "$Source\*" $target -Recurse -Force

$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut($shortcut)
$lnk.TargetPath = "$target\GScreen.App.exe"
$lnk.WorkingDirectory = $target
$lnk.Description = 'Split the monitor into virtual monitors'
$lnk.Save()

$ver = (Get-Item "$target\GScreen.Core.exe").VersionInfo.ProductVersion
Write-Output "GScreen $ver installed to $target"
Write-Output "Start menu: GScreen"
