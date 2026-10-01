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
    # plugin bin directory here to make both the standard and portable archives
    # self-contained (mirrors the DLLs bundled by the NSIS installer).
    $SpoutBinariesDir = "${ProjectRoot}/deps/Spout2/BUILD/Binaries/x64"
    $SpoutRuntimeDlls = @('Spout.dll', 'SpoutDX.dll', 'SpoutLibrary.dll')
    foreach ( $Dll in $SpoutRuntimeDlls ) {
        Copy-Item -Path "${SpoutBinariesDir}/${Dll}" -Destination "${InstallRoot}/bin/64bit/${Dll}" -Force
    }

    # libobs refuses modules compiled against a newer libobs major.minor than the running OBS,
    # so tell zip users which OBS this build needs (the NSIS installer checks it at install time).
    $ObsVersion = $BuildSpec.dependencies.'obs-studio'.version
    if ( $ObsVersion -match '^(\d+)\.(\d+)' ) {
        $MinObs = "$($Matches[1]).$($Matches[2])"
    } else {
        throw "Cannot parse obs-studio version '$ObsVersion' from buildspec.json"
    }
    $RequiresNote = @(
        "${ProductName} ${ProductVersion} was built against OBS Studio ${ObsVersion}."
        "It requires OBS Studio ${MinObs} or newer; older OBS versions will refuse to load it"
        "('The following OBS plugins failed to load: win-spout')."
        ""
        "Standard zip: extract so that you get"
        "  C:\ProgramData\obs-studio\plugins\${ProductName}\bin\64bit\${ProductName}.dll"
        "  C:\ProgramData\obs-studio\plugins\${ProductName}\data\locale\en-US.ini"
        ""
        "Portable zip (-portable.zip): extract onto your OBS folder so that you get"
        "  <OBS folder>\obs-plugins\64bit\${ProductName}.dll"
        "  <OBS folder>\data\obs-plugins\${ProductName}\locale\en-US.ini"
    ) -join "`r`n"
    $RequiresFileName = "REQUIRES-OBS-${MinObs}.txt"
    Set-Content -Path "${ProjectRoot}/release/${Configuration}/${RequiresFileName}" -Value $RequiresNote

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*", "${PortableOutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs

    # Traditional portable layout: re-arrange <plugin>/bin/64bit and <plugin>/data into
    # obs-plugins/64bit and data/obs-plugins/<plugin> so the archive can be extracted
    # straight onto an OBS install.
    Log-Group "Archiving ${ProductName} portable layout..."
    $PortableBinPath = "${PortableRoot}/obs-plugins/64bit"
    $PortableDataPath = "${PortableRoot}/data/obs-plugins/${ProductName}"

    New-Item -Path $PortableBinPath -ItemType Directory -Force | Out-Null
    New-Item -Path $PortableDataPath -ItemType Directory -Force | Out-Null

    Copy-Item -Path "${InstallRoot}/bin/64bit/*" -Destination $PortableBinPath -Recurse -Force
    if ( Test-Path "${InstallRoot}/data" ) {
        Copy-Item -Path "${InstallRoot}/data/*" -Destination $PortableDataPath -Recurse -Force
    }
    Copy-Item -Path "${ProjectRoot}/release/${Configuration}/${RequiresFileName}" -Destination $PortableRoot -Force

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
