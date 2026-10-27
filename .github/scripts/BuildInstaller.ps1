param (
	[Parameter(Mandatory = $true)]
	[string]$Version,
	[string]$Configuration = "Release",
	[string]$NSISPath = "makensis.exe"
)
# Builds the plugin and then bundles the installer

$ErrorActionPreference = "Stop"

$BuildSpec = Get-Content -Path "$PSScriptRoot\..\..\buildspec.json" -Raw | ConvertFrom-Json
$ProductName = $BuildSpec.name
$ProductVersion = $BuildSpec.version

"-- Updating NSIS Installation file"
$ScriptFile = Join-Path -Path "$PSScriptRoot" -ChildPath "win-spout-installer.nsi"
$ReplacedFile = Join-Path -Path "$PSScriptRoot" -ChildPath "win-spout-installer.versioned.nsi"

# The installer refuses OBS versions older than the libobs the plugin was compiled against
# (libobs will not load a module built for a newer major.minor), so derive that floor from
# the obs-studio dependency in buildspec.json rather than hard-coding it in the .nsi.
$ObsVersion = $BuildSpec.dependencies.'obs-studio'.version
if ( $ObsVersion -notmatch '^(\d+)\.(\d+)' ) {
	throw "Cannot parse obs-studio version '$ObsVersion' from buildspec.json"
}
$MinObsMajor = $Matches[1]
$MinObsMinor = $Matches[2]
"-- Installer will require OBS Studio >= $MinObsMajor.$MinObsMinor (from buildspec.json obs-studio $ObsVersion)"

$ScriptContents = Get-Content -Path $ScriptFile
$ReplacedContent = $ScriptContents.Replace("APPVERSION `"DebugVersion`"", "APPVERSION `"$VERSION`"")
$ReplacedContent = $ReplacedContent.Replace("RELEASEDIR `"release\Release`"", "RELEASEDIR `"release\$Configuration`"")
$ReplacedContent = $ReplacedContent -replace '^!define MIN_OBS_MAJOR \d+$', "!define MIN_OBS_MAJOR $MinObsMajor"
$ReplacedContent = $ReplacedContent -replace '^!define MIN_OBS_MINOR \d+$', "!define MIN_OBS_MINOR $MinObsMinor"

Set-Content -Path $ReplacedFile -Value $ReplacedContent

"-- Generate Installer"
. "$NSISPath" $ReplacedFile