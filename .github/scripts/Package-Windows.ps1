[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    $PortableOutputName = "${ProductName}-${ProductVersion}-windows-${Target}-portable"
    $InstallRoot = "${ProjectRoot}/release/${Configuration}/${ProductName}"
    $PortableRoot = "${ProjectRoot}/release/${PortableOutputName}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip",
            $PortableRoot
        )
    }

    Remove-Item @RemoveArgs

    # The cmake install tree only contains win-spout.dll. The Spout runtime DLLs it
    # depends on are placed elsewhere by a POST_BUILD step, so stage them into the
    # plugin directory next to win-spout.dll here to make both the standard and portable
    # archives self-contained (mirrors the DLLs bundled by the NSIS installer).
    $SpoutBinariesDir = "${ProjectRoot}/deps/Spout2/BUILD/Binaries/x64"
    $SpoutRuntimeDlls = @('Spout.dll', 'SpoutDX.dll', 'SpoutLibrary.dll')
    foreach ( $Dll in $SpoutRuntimeDlls ) {
        Copy-Item -Path "${SpoutBinariesDir}/${Dll}" -Destination "${InstallRoot}/${Dll}" -Force
    }

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*", "${PortableOutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs

    # Portable layout (OBS 33+): nest the plugin directory under plugins/ so the archive
    # can be extracted straight onto the root of a portable OBS install, giving
    # plugins/<plugin>/<plugin>.dll and plugins/<plugin>/data.
    Log-Group "Archiving ${ProductName} portable layout..."
    $PortablePluginsPath = "${PortableRoot}/plugins"

    New-Item -Path $PortablePluginsPath -ItemType Directory -Force | Out-Null

    Copy-Item -Path $InstallRoot -Destination $PortablePluginsPath -Recurse -Force

    $PortableCompressArgs = @{
        Path = (Get-ChildItem -Path $PortableRoot)
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${PortableOutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @PortableCompressArgs
    Remove-Item -Path $PortableRoot -Recurse -Force
    Log-Group
}

Package
