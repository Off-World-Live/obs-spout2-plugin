#Requires -Version 7.0
<#
.SYNOPSIS
  ONE-TIME, ELEVATED setup for building and testing the OBS Spout2 plugin on this machine.
  Installs Visual Studio 2022 Build Tools (C++ workload + Windows SDK 10.0.22621, as pinned by
  CMakePresets.json), CMake, NSIS, and grants Users modify rights on the installed plugin folder so
  the test loop can replace win-spout.dll without elevation.

  Run from an elevated PowerShell 7:   pwsh -File tests\setup-admin.ps1
#>
[CmdletBinding()]
param(
    [switch] $SkipVisualStudio,
    [switch] $SkipCMake,
    [switch] $SkipNSIS,
    [string] $LogFile = (Join-Path $PSScriptRoot '.stage\setup-admin.log')
)

$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force -Path (Split-Path $LogFile) | Out-Null
Start-Transcript -Path $LogFile -Append | Out-Null

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "This script must run elevated (Run as Administrator)."
    Stop-Transcript | Out-Null
    exit 1
}

$wingetCommon = @('--accept-source-agreements', '--accept-package-agreements', '--disable-interactivity')

if (-not $SkipVisualStudio) {
    Step "Visual Studio 2022 Build Tools (VC tools + Windows SDK 22621)"
    $vsOverride = '--quiet --wait --norestart --nocache ' +
        '--add Microsoft.VisualStudio.Workload.VCTools ' +
        '--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ' +
        '--add Microsoft.VisualStudio.Component.Windows11SDK.22621 ' +
        '--add Microsoft.VisualStudio.Component.VC.CMake.Project ' +
        '--includeRecommended'
    winget install -e --id Microsoft.VisualStudio.2022.BuildTools @wingetCommon --override $vsOverride
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "winget returned $LASTEXITCODE for VS Build Tools (-1978335189 = already installed; 3010 = reboot required)."
    }
}

if (-not $SkipCMake) {
    Step "CMake"
    winget install -e --id Kitware.CMake @wingetCommon
    if ($LASTEXITCODE -ne 0) { Write-Warning "winget returned $LASTEXITCODE for CMake" }
}

if (-not $SkipNSIS) {
    Step "NSIS (only needed to build the installer locally)"
    winget install -e --id NSIS.NSIS @wingetCommon
    if ($LASTEXITCODE -ne 0) { Write-Warning "winget returned $LASTEXITCODE for NSIS" }
}

Step "Grant Users modify rights on the installed plugin folder (so the test loop can replace the DLLs)"
$pluginDir = 'C:\ProgramData\obs-studio\plugins\win-spout'
if (-not (Test-Path $pluginDir)) { New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null }
icacls $pluginDir /grant 'BUILTIN\Users:(OI)(CI)M' /T | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Warning "icacls returned $LASTEXITCODE" }

Step "Verification"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
} else {
    Write-Warning "vswhere.exe not found - VS Build Tools did not install"
}
$cmakeExe = @("$env:ProgramFiles\CMake\bin\cmake.exe", (Get-Command cmake -ErrorAction SilentlyContinue).Source) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if ($cmakeExe) { & $cmakeExe --version | Select-Object -First 1 } else { Write-Warning "cmake not found" }

Write-Host "`nsetup-admin.ps1 finished. Open a NEW shell so PATH changes are picked up." -ForegroundColor Green
Stop-Transcript | Out-Null
