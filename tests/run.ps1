#Requires -Version 7.0
<#
.SYNOPSIS
  Build -> install -> test loop for the OBS Spout2 plugin (see tests/README.md).

  pwsh -File tests\run.ps1 -Build -Install -Test [-Filter <pytest -k expr>] [-Variant <user.ini variant>]
  pwsh -File tests\run.ps1 -Restore          # put the backed-up (released) plugin back

.OUTPUTS
  Exit codes: 0 all green (pass / xfail / skip)   1 test failures   2 build failed
              3 OBS is running (install/restore refused)   4 tooling missing
#>
[CmdletBinding()]
param(
    [switch] $Build,
    [switch] $Install,
    [switch] $Test,
    [string] $Filter = '',
    [string] $Variant = 'default',
    [switch] $Restore,
    [switch] $UpdateGolden,
    [switch] $KeepObs,
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [string] $ResultsDir = ''
)

$ErrorActionPreference = 'Stop'
$testsRoot = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $testsRoot '..')).Path
$pluginDir = if ($env:OBS_SPOUT_PLUGIN_DIR) { $env:OBS_SPOUT_PLUGIN_DIR } else { 'C:\ProgramData\obs-studio\plugins\win-spout' }
$portableObs = if ($env:OBS_TEST_PORTABLE) { $env:OBS_TEST_PORTABLE } else { 'C:\AgenticWork\obs-test\obs-studio' }
$stageDir = Join-Path $testsRoot '.stage'
$backupDir = Join-Path $testsRoot '.backup'
$releaseDir = Join-Path $repoRoot "release\$Configuration\win-spout"
$releaseDll = Join-Path $releaseDir 'bin\64bit\win-spout.dll'
$installedDll = Join-Path $pluginDir 'bin\64bit\win-spout.dll'
$installedJson = Join-Path $stageDir 'installed.json'

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Fail($code, $msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit $code }

function Find-VenvPython {
    $candidates = @((Join-Path $testsRoot '.venv\Scripts\python.exe'))
    if ($env:OBS_SPOUT_VENV_PYTHON) { $candidates = @($env:OBS_SPOUT_VENV_PYTHON) + $candidates }
    # git worktrees: fall back to the main checkout's venv
    try {
        $common = (& git -C $repoRoot rev-parse --path-format=absolute --git-common-dir 2>$null)
        if ($common) { $candidates += (Join-Path (Split-Path $common -Parent) 'tests\.venv\Scripts\python.exe') }
    } catch {}
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path } }
    return $null
}

function Get-ObsProcesses {
    @(Get-CimInstance Win32_Process -Filter "Name='obs64.exe'" -ErrorAction SilentlyContinue)
}

function Get-Sha256($path) { (Get-FileHash -Algorithm SHA256 -Path $path).Hash.ToLowerInvariant() }

function Get-Commit {
    try { (& git -C $repoRoot rev-parse --short HEAD 2>$null) } catch { '' }
}

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $p = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'
    if (Test-Path $p) { return $p }
    return $null
}

function Find-MSVC {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1
    if ($path) { return $path }
    return $null
}

if (-not ($Build -or $Install -or $Test -or $Restore)) {
    Get-Help $PSCommandPath -Detailed
    exit 0
}

New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

# ---------------------------------------------------------------------------------------------
if ($Restore) {
    Step "Restore the backed-up plugin into $pluginDir"
    if (Get-ObsProcesses) { Fail 3 'obs64.exe is running; close OBS before restoring the plugin.' }
    if (-not (Test-Path (Join-Path $backupDir 'bin\64bit\win-spout.dll'))) { Fail 4 "no backup at $backupDir" }
    robocopy (Join-Path $backupDir 'bin\64bit') (Join-Path $pluginDir 'bin\64bit') *.dll /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { Fail 4 "robocopy failed ($LASTEXITCODE); run tests\setup-admin.ps1 elevated to grant write access" }
    robocopy (Join-Path $backupDir 'data\locale') (Join-Path $pluginDir 'data\locale') /NFL /NDL /NJH /NJS /NP | Out-Null
    Remove-Item -Force -ErrorAction SilentlyContinue $installedJson
    Write-Host "Restored. Installed DLL sha256: $(Get-Sha256 $installedDll)"
    if (-not ($Build -or $Install -or $Test)) { exit 0 }
}

# ---------------------------------------------------------------------------------------------
if ($Build) {
    Step 'Check build tooling'
    $cmake = Find-CMake
    $msvc = Find-MSVC
    if (-not $cmake -or -not $msvc) {
        Write-Host "cmake: $(if ($cmake) { $cmake } else { 'MISSING' })"
        Write-Host "MSVC : $(if ($msvc) { $msvc } else { 'MISSING' })"
        Fail 4 'C++ build tooling is missing. Run (elevated):  pwsh -File tests\setup-admin.ps1   then open a new shell.'
    }
    Write-Host "cmake: $cmake"
    Write-Host "MSVC : $msvc"

    Step 'Restore the Spout2 submodule at the recorded commit (prebuilt libs)'
    & git -C $repoRoot submodule update --init deps/Spout2
    if ($LASTEXITCODE -ne 0) { Fail 2 'git submodule update failed' }
    if (-not (Test-Path (Join-Path $repoRoot 'deps\Spout2\BUILD\Binaries\x64\SpoutDX.lib'))) {
        Fail 2 'deps/Spout2/BUILD/Binaries/x64/SpoutDX.lib missing (submodule not at the recorded commit?)'
    }

    Step "Build the plugin ($Configuration) with .github/scripts/Build-Windows.ps1"
    & pwsh -NoProfile -File (Join-Path $repoRoot '.github\scripts\Build-Windows.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $releaseDll)) { Fail 2 "plugin build failed (exit $LASTEXITCODE)" }

    Step 'Build tests/spout-tool (SpoutDX sender/receiver)'
    $toolSrc = Join-Path $testsRoot 'spout-tool'
    $toolBuild = Join-Path $toolSrc 'build'
    & $cmake -S $toolSrc -B $toolBuild -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { Fail 2 'spout-tool configure failed' }
    & $cmake --build $toolBuild --config Release --parallel
    if ($LASTEXITCODE -ne 0) { Fail 2 'spout-tool build failed' }

    $buildInfo = @{ sha256 = (Get-Sha256 $releaseDll); dll = $releaseDll; configuration = $Configuration; commit = (Get-Commit); built_at = (Get-Date).ToString('s') }
    $buildInfo | ConvertTo-Json | Set-Content (Join-Path $stageDir 'build.json')
    Write-Host "Built $releaseDll sha256=$($buildInfo.sha256)"
}

# ---------------------------------------------------------------------------------------------
if ($Install) {
    Step "Install the built plugin into $pluginDir"
    if (-not (Test-Path $releaseDll)) {
        Write-Host "No fresh build found at $releaseDll - leaving the currently installed (released) plugin in place." -ForegroundColor Yellow
        if (Test-Path $installedDll) { Write-Host "Installed DLL sha256: $(Get-Sha256 $installedDll)" }
    } else {
        $running = Get-ObsProcesses
        if ($running) {
            $running | ForEach-Object { Write-Host "  obs64.exe pid $($_.ProcessId): $($_.ExecutablePath)" }
            Fail 3 'obs64.exe is running (it holds win-spout.dll). Close OBS and re-run -Install.'
        }
        if (-not (Test-Path (Join-Path $backupDir 'bin\64bit\win-spout.dll')) -and (Test-Path $installedDll)) {
            Write-Host "Backing up the installed plugin once to $backupDir"
            robocopy (Join-Path $pluginDir 'bin\64bit') (Join-Path $backupDir 'bin\64bit') /NFL /NDL /NJH /NJS /NP | Out-Null
            robocopy (Join-Path $pluginDir 'data\locale') (Join-Path $backupDir 'data\locale') /NFL /NDL /NJH /NJS /NP | Out-Null
        }
        robocopy (Join-Path $releaseDir 'bin\64bit') (Join-Path $pluginDir 'bin\64bit') *.dll /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { Fail 4 "robocopy into $pluginDir failed ($LASTEXITCODE); run tests\setup-admin.ps1 elevated to grant write access" }
        robocopy (Join-Path $releaseDir 'data\locale') (Join-Path $pluginDir 'data\locale') /NFL /NDL /NJH /NJS /NP | Out-Null
        $sha = Get-Sha256 $installedDll
        if ($sha -ne (Get-Sha256 $releaseDll)) { Fail 4 'installed DLL hash does not match the build output' }
        @{ sha256 = $sha; source = $releaseDll; installed_at = (Get-Date).ToString('s'); commit = (Get-Commit); plugin_dir = $pluginDir } |
            ConvertTo-Json | Set-Content $installedJson
        Write-Host "Installed. sha256=$sha  (recorded in $installedJson)"
    }
}

# ---------------------------------------------------------------------------------------------
if ($Test) {
    Step 'Run the pytest harness'
    $venvPy = Find-VenvPython
    if (-not $venvPy) { Fail 4 'tests\.venv not found. Run:  pwsh -File tests\setup-user.ps1' }
    if (-not (Test-Path (Join-Path $portableObs 'bin\64bit\obs64.exe'))) {
        Fail 4 "portable test OBS not found at $portableObs. Run:  pwsh -File tests\setup-user.ps1 -MakePortable"
    }
    if (-not (Test-Path $installedDll)) { Fail 4 "plugin not installed at $installedDll (run -Build -Install or install the release)" }

    $run = if ($ResultsDir) { $ResultsDir } else { Join-Path $testsRoot ("results\" + (Get-Date).ToString('yyyyMMdd-HHmmss')) }
    New-Item -ItemType Directory -Force -Path $run | Out-Null
    $pytestArgs = @('-m', 'pytest', $testsRoot, '--results-dir', $run, '--junitxml', (Join-Path $run 'junit.xml'), '--variant', $Variant)
    if ($Filter) { $pytestArgs += @('-k', $Filter) }
    if ($UpdateGolden) { $pytestArgs += '--update-golden' }
    if ($KeepObs) { $pytestArgs += '--keep-obs' }

    Push-Location $testsRoot
    try {
        & $venvPy @pytestArgs
        $rc = $LASTEXITCODE
    } finally { Pop-Location }

    $summaryPath = Join-Path $run 'summary.json'
    $reportPath = Join-Path $run 'report.md'
    $counts = ''
    if (Test-Path $summaryPath) {
        $summary = Get-Content $summaryPath -Raw | ConvertFrom-Json
        $counts = ($summary.counts.PSObject.Properties | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ' '
    }
    Write-Host ''
    Write-Host "Report: $reportPath"
    Write-Host "Counts: $counts"
    switch ($rc) {
        0 { exit 0 }
        5 { Write-Host 'pytest collected no tests (check -Filter)' -ForegroundColor Yellow; exit 1 }
        default { exit 1 }
    }
}
