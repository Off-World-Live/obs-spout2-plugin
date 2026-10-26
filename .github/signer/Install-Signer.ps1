#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string] $UserName = 'gh-signer',
    [string] $RunnerRoot = 'C:\gh-signer',
    [string] $ConfigRoot = "${env:ProgramData}\gh-signer",
    [string] $RunnerVersion = 'latest',
    # GitHub logins allowed to push and trigger signing runs. Kept only in the installed policy, not the repo.
    [string[]] $AllowedActors
)
# One-time, elevated setup of the release signing runner on this machine. Safe to re-run: it
# refreshes the hooks, policy and runner binaries and re-applies permissions.
#
# Layout:
#   $ConfigRoot  (admin-owned; gh-signer read/execute only) hooks, policy.json, Start-SigningRunner.ps1
#   $ConfigRoot\inbox  (admin write; gh-signer may read/delete) single-use JIT runner config
#   $RunnerRoot\runner  (gh-signer modify) GitHub Actions runner binaries and _work

$ErrorActionPreference = 'Stop'

$SourceRoot = $PSScriptRoot
$AdminSid = '*S-1-5-32-544'
$SystemSid = '*S-1-5-18'
$InstallingUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name

function Invoke-Icacls {
    & icacls.exe @args | Out-Null
    if ( $LASTEXITCODE -ne 0 ) {
        throw "icacls $($args -join ' ') failed"
    }
}

"-- Signing account '${UserName}'"
$User = Get-LocalUser -Name $UserName -ErrorAction SilentlyContinue
if ( ! $User ) {
    $Password = Read-Host -AsSecureString -Prompt "Choose a password for the new standard user '${UserName}'"
    $User = New-LocalUser -Name $UserName -Password $Password -PasswordNeverExpires -AccountNeverExpires `
        -Description 'GitHub Actions release signing runner'
    Add-LocalGroupMember -SID 'S-1-5-32-545' -Member $UserName
}
foreach ( $Group in @('S-1-5-32-544', 'S-1-5-32-551', 'S-1-5-32-555', 'S-1-5-32-580') ) {
    # Administrators, Backup Operators, Remote Desktop Users, Remote Management Users
    if ( Get-LocalGroupMember -SID $Group -ErrorAction SilentlyContinue | Where-Object { $_.SID -eq $User.SID } ) {
        throw "${UserName} is a member of privileged group ${Group}; remove it before continuing"
    }
}

"-- Prerequisites"
# The Store build of PowerShell is per-user and makes winget treat the MSI as an upgrade, which
# fails, so install the machine-wide MSI for gh-signer straight from the PowerShell release.
$MachinePwsh = "${env:ProgramFiles}\PowerShell\7\pwsh.exe"
if ( ! ( Test-Path -Path $MachinePwsh ) ) {
    $PwshRelease = Invoke-RestMethod -Uri 'https://api.github.com/repos/PowerShell/PowerShell/releases/latest' `
        -Headers @{ 'User-Agent' = 'gh-signer-install' }
    $MsiAsset = $PwshRelease.assets | Where-Object { $_.name -match '^PowerShell-[0-9.]+-win-x64\.msi$' } | Select-Object -First 1
    $HashAsset = $PwshRelease.assets | Where-Object { $_.name -eq 'hashes.sha256' } | Select-Object -First 1

    $Msi = Join-Path ([System.IO.Path]::GetTempPath()) $MsiAsset.name
    $HashFile = Join-Path ([System.IO.Path]::GetTempPath()) 'pwsh-hashes.sha256'
    Invoke-WebRequest -Uri $MsiAsset.browser_download_url -OutFile $Msi
    Invoke-WebRequest -Uri $HashAsset.browser_download_url -OutFile $HashFile

    $HashPattern = "^([0-9a-fA-F]{64})\s+\*?$([regex]::Escape($MsiAsset.name))$"
    $HashLine = Get-Content -Path $HashFile | Where-Object { $_.Trim() -match $HashPattern } | Select-Object -First 1
    Remove-Item -Path $HashFile -Force
    $ExpectedMsiHash = if ( $HashLine -and ( $HashLine.Trim() -match $HashPattern ) ) { $Matches[1].ToUpperInvariant() }
    if ( ! $ExpectedMsiHash -or ( (Get-FileHash -Path $Msi -Algorithm SHA256).Hash -ne $ExpectedMsiHash ) ) {
        Remove-Item -Path $Msi -Force
        throw "PowerShell MSI checksum did not match $($PwshRelease.tag_name) hashes.sha256"
    }
    $MsiSignature = Get-AuthenticodeSignature -FilePath $Msi
    if ( $MsiSignature.Status -ne 'Valid' -or $MsiSignature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation' ) {
        Remove-Item -Path $Msi -Force
        throw "PowerShell MSI is not validly signed by Microsoft ($($MsiSignature.Status))"
    }
    "   $($MsiAsset.name) checksum and Microsoft signature verified"

    $Msiexec = Start-Process -FilePath msiexec.exe -Wait -PassThru -ArgumentList @(
        '/i', "`"${Msi}`"", '/qn', '/norestart', 'ADD_PATH=1', 'USE_MU=0', 'ENABLE_MU=0'
    )
    Remove-Item -Path $Msi -Force
    if ( $Msiexec.ExitCode -notin @(0, 3010) ) {
        throw "PowerShell MSI install failed with exit code $($Msiexec.ExitCode)"
    }
}
"   machine-wide PowerShell: ${MachinePwsh}"

if ( ! ( Test-Path -Path "${env:ProgramFiles(x86)}\NSIS\makensis.exe" ) ) {
    winget install --id NSIS.NSIS --exact --scope machine --silent --accept-package-agreements --accept-source-agreements
    if ( $LASTEXITCODE -ne 0 ) {
        throw "winget install NSIS.NSIS exited with ${LASTEXITCODE}"
    }
}
if ( ! ( Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue ) ) {
    Write-Warning 'signtool.exe not found: install the Windows SDK signing tools'
}
if ( ! ( Test-Path 'C:\Program Files\Certum\SimplySign Desktop\SimplySignDesktop.exe' ) ) {
    Write-Warning 'SimplySign Desktop not found: it is required to access the signing key'
}

"-- Policy and hooks in ${ConfigRoot}"
New-Item -Path "${ConfigRoot}\hooks", "${ConfigRoot}\inbox" -ItemType Directory -Force | Out-Null
Copy-Item -Path "${SourceRoot}\hooks\*.ps1" -Destination "${ConfigRoot}\hooks" -Force
if ( ! $AllowedActors ) {
    $AllowedActors = (Read-Host -Prompt 'GitHub logins allowed to trigger signing (comma separated)') -split ',' |
        ForEach-Object { $_.Trim() } | Where-Object { $_ }
}
if ( ! $AllowedActors ) {
    throw 'At least one allowed actor is required'
}
$Policy = Get-Content -Path "${SourceRoot}\policy.json" -Raw | ConvertFrom-Json
$Policy.allowedActors = @($AllowedActors)
$Policy | ConvertTo-Json -Depth 4 | Set-Content -Path "${ConfigRoot}\hooks\policy.json"
"   allowed actors: $($AllowedActors -join ', ')"
Copy-Item -Path "${SourceRoot}\Start-SigningRunner.ps1" -Destination $ConfigRoot -Force

Invoke-Icacls $ConfigRoot /inheritance:r /grant:r "${SystemSid}:(OI)(CI)F" "${AdminSid}:(OI)(CI)F" "${UserName}:(OI)(CI)RX"
Get-ChildItem -Path $ConfigRoot -Recurse | ForEach-Object { Invoke-Icacls $_.FullName /reset }
# The JIT config is written by the (unelevated) maintainer and consumed and deleted by gh-signer.
Invoke-Icacls "${ConfigRoot}\inbox" /inheritance:r /grant:r "${SystemSid}:(OI)(CI)F" "${AdminSid}:(OI)(CI)F" `
    "${InstallingUser}:(OI)(CI)M" "${UserName}:(OI)(CI)(RX,DE)"

"-- Runner binaries in ${RunnerRoot}\runner"
$ReleaseUri = if ( $RunnerVersion -eq 'latest' ) {
    'https://api.github.com/repos/actions/runner/releases/latest'
} else {
    "https://api.github.com/repos/actions/runner/releases/tags/v${RunnerVersion}"
}
$Release = Invoke-RestMethod -Uri $ReleaseUri -Headers @{ 'User-Agent' = 'gh-signer-install' }
$Asset = $Release.assets | Where-Object { $_.name -match '^actions-runner-win-x64-[0-9.]+\.zip$' } | Select-Object -First 1
if ( $Release.body -notmatch '<!-- BEGIN SHA win-x64 -->([0-9a-f]{64})<!-- END SHA win-x64 -->' ) {
    throw "No win-x64 checksum published in runner release $($Release.tag_name)"
}
$ExpectedHash = $Matches[1]

$Download = Join-Path ([System.IO.Path]::GetTempPath()) $Asset.name
Invoke-WebRequest -Uri $Asset.browser_download_url -OutFile $Download
$ActualHash = (Get-FileHash -Path $Download -Algorithm SHA256).Hash.ToLowerInvariant()
if ( $ActualHash -ne $ExpectedHash ) {
    Remove-Item -Path $Download -Force
    throw "Runner checksum mismatch: expected ${ExpectedHash}, got ${ActualHash}"
}
"   $($Asset.name) sha256 ${ActualHash} verified"

if ( Test-Path "${RunnerRoot}\runner" ) {
    Remove-Item -Path "${RunnerRoot}\runner" -Recurse -Force
}
New-Item -Path "${RunnerRoot}\runner" -ItemType Directory -Force | Out-Null
Expand-Archive -Path $Download -DestinationPath "${RunnerRoot}\runner"
Remove-Item -Path $Download -Force

# Only gh-signer and administrators can see the runner; gh-signer needs write for _work and self-updates.
Invoke-Icacls $RunnerRoot /inheritance:r /grant:r "${SystemSid}:(OI)(CI)F" "${AdminSid}:(OI)(CI)F" "${UserName}:(OI)(CI)M"
Get-ChildItem -Path $RunnerRoot -Recurse | ForEach-Object { Invoke-Icacls $_.FullName /reset }

"-- Hook environment (machine scope, admin-only)"
[Environment]::SetEnvironmentVariable('ACTIONS_RUNNER_HOOK_JOB_STARTED', "${ConfigRoot}\hooks\job-started.ps1", 'Machine')
[Environment]::SetEnvironmentVariable('ACTIONS_RUNNER_HOOK_JOB_COMPLETED', "${ConfigRoot}\hooks\job-completed.ps1", 'Machine')

"-- Audit: top-level folders on fixed drives that ${UserName} can write to"
$UserSids = @($User.SID.Value, 'S-1-5-32-545', 'S-1-5-11', 'S-1-1-0') # self, Users, Authenticated Users, Everyone
Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -match '^[A-Z]:\\$' -and (Test-Path $_.Root) } | ForEach-Object {
    Get-ChildItem -Path $_.Root -Directory -Force -ErrorAction SilentlyContinue | Where-Object {
        $_.FullName -ne $RunnerRoot -and $_.Name -notin @('$Recycle.Bin', 'System Volume Information')
    } | ForEach-Object {
        $Folder = $_.FullName
        $Writable = (Get-Acl -Path $Folder -ErrorAction SilentlyContinue).Access | Where-Object {
            $Sid = try { $_.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value } catch { $null }
            $_.AccessControlType -eq 'Allow' -and
            $_.FileSystemRights -match 'Modify|Write|FullControl' -and
            $UserSids -contains $Sid
        }
        if ( $Writable ) { Write-Warning "${UserName} can write to ${Folder}" }
    }
}

""
"Setup complete. Next: sign in to Windows as ${UserName} once, start SimplySign Desktop and log in,"
"then follow the release procedure in .github/signer/README.md."
