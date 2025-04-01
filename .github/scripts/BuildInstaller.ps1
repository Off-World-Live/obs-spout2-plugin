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

$ScriptContents = Get-Content -Path $ScriptFile
$ReplacedContent = $ScriptContents.Replace("APPVERSION `"DebugVersion`"", "APPVERSION `"$VERSION`"")
$ReplacedContent = $ReplacedContent.Replace("RELEASEDIR `"release\Release`"", "RELEASEDIR `"release\$Configuration`"")

Set-Content -Path $ReplacedFile -Value $ReplacedContent

"-- Generate Installer"
. "$NSISPath" $ReplacedFile