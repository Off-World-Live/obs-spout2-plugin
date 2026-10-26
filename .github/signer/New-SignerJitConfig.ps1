[CmdletBinding()]
param(
    [string] $Repository = 'Off-World-Live/obs-spout2-plugin',
    [string] $Label = 'signer',
    [string] $InboxPath = "${env:ProgramData}\gh-signer\inbox",
    [string] $TokenFile = "${HOME}\.gh-signer\jit.token"
)
# Run as a repository admin (your normal account, unelevated) when a release is ready to sign.
# Registers a single-use just-in-time runner with GitHub and hands its config to gh-signer.
# The runner deregisters itself after one job; nothing persists on GitHub between releases.
#
# Authenticates with a fine-grained token (this repository only, Administration: read and write)
# stored DPAPI-encrypted for the current Windows user in $TokenFile, falling back to gh's login.

$ErrorActionPreference = 'Stop'

if ( ! ( Test-Path -Path $InboxPath ) ) {
    throw "${InboxPath} not found: run Install-Signer.ps1 first"
}

if ( Test-Path -Path $TokenFile ) {
    $env:GH_TOKEN = [System.Net.NetworkCredential]::new('', (Get-Content -Path $TokenFile | ConvertTo-SecureString)).Password
}

$Name = "signer-${env:COMPUTERNAME}-$(Get-Date -Format 'yyyyMMdd-HHmmss')".ToLowerInvariant()
$Body = @{
    name = $Name
    runner_group_id = 1
    labels = @('self-hosted', 'Windows', 'X64', $Label)
    work_folder = '_work'
} | ConvertTo-Json -Compress

$Response = $Body | gh api --method POST "repos/${Repository}/actions/runners/generate-jitconfig" --input -
if ( $LASTEXITCODE -ne 0 ) {
    throw 'Failed to generate JIT runner config (requires repository admin)'
}
$JitConfig = ($Response | ConvertFrom-Json).encoded_jit_config

Set-Content -Path (Join-Path $InboxPath 'jitconfig') -Value $JitConfig -NoNewline
"Registered single-use runner '${Name}'. Switch to the gh-signer account, log in to SimplySign Desktop and run:"
"  pwsh -NoProfile -ExecutionPolicy Bypass -File `"${env:ProgramData}\gh-signer\Start-SigningRunner.ps1`""
