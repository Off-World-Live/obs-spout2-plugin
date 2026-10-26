[CmdletBinding()]
param(
    [string] $CertificateSubject = 'CN=Off World Live Limited',
    [string] $RunnerPath = 'C:\gh-signer\runner',
    [int] $TimeoutMinutes = 60
)
# Run as gh-signer, in its interactive session, with SimplySign Desktop logged in.
# Consumes the single-use JIT config, runs exactly one job, then exits.

$ErrorActionPreference = 'Stop'

$ConfigRoot = $PSScriptRoot
$InboxFile = Join-Path $ConfigRoot 'inbox\jitconfig'

$Principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if ( $Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
     ( Get-LocalGroupMember -SID 'S-1-5-32-544' | Where-Object { $_.SID -eq $Principal.Identity.User } ) ) {
    throw 'Refusing to run the signing runner as an administrator; sign in as gh-signer'
}

$Certificate = Get-ChildItem -Path Cert:\CurrentUser\My |
    Where-Object { $_.Subject -like "${CertificateSubject},*" -or $_.Subject -eq $CertificateSubject } |
    Where-Object { $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } |
    Select-Object -First 1
if ( ! $Certificate ) {
    throw "No usable '${CertificateSubject}' certificate in CurrentUser\My: log in to SimplySign Desktop first"
}
"Signing certificate: $($Certificate.Subject) ($($Certificate.Thumbprint)), expires $($Certificate.NotAfter)"

if ( ! ( Test-Path -Path $InboxFile ) ) {
    throw "No JIT config waiting in ${InboxFile}: run New-SignerJitConfig.ps1 as a repository admin first"
}
$JitConfig = Get-Content -Path $InboxFile -Raw
# No -Force: it rewrites file attributes first, which gh-signer is not allowed to do in the inbox.
Remove-Item -Path $InboxFile

# Set explicitly as well as machine-wide so the hooks cannot be dropped from this process.
$env:ACTIONS_RUNNER_HOOK_JOB_STARTED = Join-Path $ConfigRoot 'hooks\job-started.ps1'
$env:ACTIONS_RUNNER_HOOK_JOB_COMPLETED = Join-Path $ConfigRoot 'hooks\job-completed.ps1'

"Starting single-use runner (stops after one job or ${TimeoutMinutes} minutes)..."
$Runner = Start-Process -FilePath (Join-Path $RunnerPath 'run.cmd') -ArgumentList '--jitconfig', $JitConfig `
    -WorkingDirectory $RunnerPath -NoNewWindow -PassThru
$JitConfig = $null

if ( ! $Runner.WaitForExit($TimeoutMinutes * 60 * 1000) ) {
    Write-Warning "Timed out after ${TimeoutMinutes} minutes; stopping runner"
    & taskkill.exe /PID $Runner.Id /T /F | Out-Null
}
"Runner exited with code $($Runner.ExitCode). Log out of SimplySign Desktop."
