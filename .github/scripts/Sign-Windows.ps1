[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $CertificateName,
    [Parameter(Mandatory = $true)]
    [string[]] $Path,
    [string] $TimestampUrl = 'http://timestamp.digicert.com',
    [switch] $MachineStore
)
# Signs files with a certificate from the Windows certificate store, selected by subject name

$ErrorActionPreference = 'Stop'

function Find-SignTool {
    $Command = Get-Command -Name signtool.exe -ErrorAction SilentlyContinue
    if ( $Command ) {
        return $Command.Source
    }

    $Candidate = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Sort-Object -Property FullName -Descending |
        Select-Object -First 1

    if ( ! $Candidate ) {
        throw 'signtool.exe not found. Install the Windows SDK on the signing runner or add signtool to PATH.'
    }

    return $Candidate.FullName
}

$SignTool = Find-SignTool
$Files = $Path | ForEach-Object { (Resolve-Path -Path $_).Path }

"-- Using ${SignTool}"
$SignArgs = @('sign', '/n', $CertificateName, '/fd', 'sha256', '/tr', $TimestampUrl, '/td', 'sha256', '/v')
if ( $MachineStore ) {
    $SignArgs += '/sm'
}

foreach ( $File in $Files ) {
    "-- Signing ${File}"
    & $SignTool @SignArgs $File
    if ( $LASTEXITCODE -ne 0 ) {
        throw "Signing failed for ${File}"
    }

    & $SignTool verify /pa $File
    if ( $LASTEXITCODE -ne 0 ) {
        throw "Signature verification failed for ${File}"
    }
}
