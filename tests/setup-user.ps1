#Requires -Version 7.0
<#
.SYNOPSIS
  Non-elevated setup for the OBS Spout2 plugin test harness: Python 3.12 (user scope), a venv with
  the harness requirements, and (with -MakePortable) a throw-away portable copy of the installed OBS
  so tests never touch your real OBS configuration.

  pwsh -File tests\setup-user.ps1 [-MakePortable] [-ObsSource 'C:\Program Files\obs-studio'] [-PortableRoot 'C:\AgenticWork\obs-test\obs-studio']
#>
[CmdletBinding()]
param(
    [switch] $MakePortable,
    [string] $ObsSource = 'C:\Program Files\obs-studio',
    [string] $PortableRoot = 'C:\AgenticWork\obs-test\obs-studio',
    [switch] $SkipPython
)

$ErrorActionPreference = 'Stop'
$testsRoot = $PSScriptRoot
function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }

function Find-Python312 {
    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
        "$env:ProgramFiles\Python312\python.exe"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        $p = & $py.Source -3.12 -c "import sys; print(sys.executable)" 2>$null
        if ($LASTEXITCODE -eq 0 -and $p) { return $p.Trim() }
    }
    return $null
}

if (-not $SkipPython) {
    Step "Python 3.12 (user scope)"
    $python = Find-Python312
    if (-not $python) {
        winget install -e --id Python.Python.3.12 --scope user --accept-source-agreements --accept-package-agreements --disable-interactivity
        if ($LASTEXITCODE -ne 0) { Write-Warning "winget returned $LASTEXITCODE for Python 3.12" }
        $python = Find-Python312
    }
    if (-not $python) { throw "Python 3.12 not found after install. The Store 'python' alias is NOT a real interpreter." }
    Write-Host "Using $python"

    Step "Virtual environment + requirements"
    $venv = Join-Path $testsRoot '.venv'
    if (-not (Test-Path (Join-Path $venv 'Scripts\python.exe'))) { & $python -m venv $venv }
    $venvPy = Join-Path $venv 'Scripts\python.exe'
    & $venvPy -m pip install --upgrade pip --quiet
    & $venvPy -m pip install -r (Join-Path $testsRoot 'requirements.txt')
    & $venvPy -c "import obsws_python, PIL, numpy, pytest; print('harness deps OK')"
}

if ($MakePortable) {
    Step "Portable OBS copy: $ObsSource -> $PortableRoot"
    if (-not (Test-Path (Join-Path $ObsSource 'bin\64bit\obs64.exe'))) { throw "OBS not found at $ObsSource" }
    New-Item -ItemType Directory -Force -Path $PortableRoot | Out-Null
    # /MIR would delete our seeded config on re-run, so copy additively and never remove.
    robocopy $ObsSource $PortableRoot /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with $LASTEXITCODE" }

    $fixtures = Join-Path $testsRoot 'fixtures\portable'
    Copy-Item -Path (Join-Path $fixtures '*') -Destination $PortableRoot -Recurse -Force

    # Seed LastVersion/InfoIncrement from the user's own global.ini so OBS does not show first-run / what's-new dialogs.
    $userGlobal = "$env:APPDATA\obs-studio\global.ini"
    $portGlobal = Join-Path $PortableRoot 'config\obs-studio\global.ini'
    if ((Test-Path $userGlobal) -and (Test-Path $portGlobal)) {
        $src = Get-Content $userGlobal
        $dst = Get-Content $portGlobal
        foreach ($key in @('LastVersion', 'InfoIncrement')) {
            $line = $src | Where-Object { $_ -match "^$key=" } | Select-Object -First 1
            if ($line) {
                if ($dst -match "^$key=") { $dst = $dst -replace "^$key=.*", $line } else { $dst = $dst -replace '^\[General\]$', "[General]`n$line" }
            }
        }
        Set-Content -Path $portGlobal -Value $dst
    }
    Write-Host "Portable OBS ready at $PortableRoot (portable_mode.txt present: $(Test-Path (Join-Path $PortableRoot 'portable_mode.txt')))"
}

Write-Host "`nsetup-user.ps1 finished." -ForegroundColor Green
