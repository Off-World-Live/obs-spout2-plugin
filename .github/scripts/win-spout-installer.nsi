RequestExecutionLevel admin
; Script generated with the Venis Install Wizard

Unicode True

; Define your application name
!define APPNAME "Spout 2 OBS Plugin"
!define APPVERSION "DebugVersion"
!define RELEASEDIR "release\Release"
!define APPNAMEANDVERSION "Spout 2 OBS Plugin ${APPVERSION}"

; Minimum OBS Studio version this build can load on. libobs refuses to load a module compiled
; against a newer libobs major.minor than the running OBS ("failed to load win-spout"), so this
; must match the obs-studio version in buildspec.json. BuildInstaller.ps1 rewrites both values
; from buildspec.json at package time; the defaults here are only for manual builds.
!define MIN_OBS_MAJOR 32
!define MIN_OBS_MINOR 1

; Main Install settings
Name "${APPNAMEANDVERSION}"
InstallDirRegKey HKLM "Software\${APPNAME}" ""
InstallDir "$COMMONPROGRAMDATA\obs-studio\plugins\win-spout"
OutFile "..\..\release\OBS_Spout2_Plugin_Install_v${APPVERSION}.exe"

; Use compression
SetCompressor Zlib

; Modern interface settings
!include "MUI.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Set languages (first is default language)
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_RESERVEFILE_LANGDLL

; Refuse to install onto an OBS Studio that is too old to load this build (issues #105, #94).
; The OBS installer records its directory in HKLM\SOFTWARE\OBS Studio; portable/zip installs
; have no registry entry, so those only get a warning.
Function .onInit
	SetRegView 64
	ReadRegStr $0 HKLM "SOFTWARE\OBS Studio" ""
	${If} $0 == ""
		MessageBox MB_ICONEXCLAMATION|MB_OKCANCEL \
			"No OBS Studio installation was found in the registry.$\r$\n$\r$\n\
			This plugin requires OBS Studio ${MIN_OBS_MAJOR}.${MIN_OBS_MINOR} or newer. \
			If you use a portable OBS, extract the '-portable.zip' release onto your OBS folder instead of running this installer.$\r$\n$\r$\n\
			Continue anyway?" /SD IDOK IDOK +2
		Abort
		Return
	${EndIf}

	ClearErrors
	GetDLLVersion "$0\bin\64bit\obs64.exe" $R0 $R1
	${If} ${Errors}
		MessageBox MB_ICONEXCLAMATION|MB_OKCANCEL \
			"Could not read the version of$\r$\n$0\bin\64bit\obs64.exe$\r$\n$\r$\n\
			This plugin requires OBS Studio ${MIN_OBS_MAJOR}.${MIN_OBS_MINOR} or newer. Continue anyway?" /SD IDOK IDOK +2
		Abort
		Return
	${EndIf}

	IntOp $R2 $R0 >> 16         ; major
	IntOp $R3 $R0 & 0x0000FFFF  ; minor
	IntOp $R4 $R1 >> 16         ; patch

	; Compare major.minor as a single number (major * 1000 + minor) to keep the test simple.
	IntOp $R5 $R2 * 1000
	IntOp $R5 $R5 + $R3
	IntOp $R6 ${MIN_OBS_MAJOR} * 1000
	IntOp $R6 $R6 + ${MIN_OBS_MINOR}

	${If} $R5 < $R6
		MessageBox MB_ICONSTOP|MB_OK \
			"OBS Studio $R2.$R3.$R4 was found at$\r$\n$0$\r$\n$\r$\n\
			This version of the Spout 2 plugin (${APPVERSION}) requires OBS Studio ${MIN_OBS_MAJOR}.${MIN_OBS_MINOR} or newer \
			and would show 'The following OBS plugins failed to load: win-spout'.$\r$\n$\r$\n\
			Please update OBS Studio, or download an older plugin release that matches your OBS version from$\r$\n\
			https://github.com/Off-World-Live/obs-spout2-plugin/releases" /SD IDOK
		Abort
	${EndIf}
FunctionEnd

Section "Spout 2 OBS Plugin" Section1
	; Install into the $INSTDIR chosen on the directory page (default
	; ProgramData\obs-studio\plugins\win-spout) instead of always forcing ProgramData.

	; Set Section properties
	SetOverwrite on
	AllowSkipFiles off

	; Set Section Files and Shortcuts
	SetOutPath "$INSTDIR\bin\64bit"
  File "..\..\${RELEASEDIR}\win-spout\bin\64bit\win-spout.dll"
	File "..\..\deps\Spout2\BUILD\Binaries\x64\Spout.dll"
	File "..\..\deps\Spout2\BUILD\Binaries\x64\SpoutDX.dll"
	File "..\..\deps\Spout2\BUILD\Binaries\x64\SpoutLibrary.dll"

	SetOutPath "$INSTDIR\data\locale\"
	File "..\..\data\locale\en-US.ini"
	File "..\..\data\locale\zh-CN.ini"
	File "..\..\data\locale\pt-BR.ini"
	File "..\..\data\locale\es-ES.ini"
	CreateDirectory "$SMPROGRAMS\Spout 2 OBS Plugin"
	CreateShortCut "$SMPROGRAMS\Spout 2 OBS Plugin\Uninstall Spout2 OBS Plugin.lnk" "$INSTDIR\uninstall-spout2-plugin.exe"

SectionEnd

Section -FinishSection

	WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" "$INSTDIR\uninstall-spout2-plugin.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Publisher" "OBS Spout2 Plugin"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "HelpLink" "https://github.com/Off-World-Live/obs-spout2-source-plugin"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "${APPVERSION}"
	WriteUninstaller "$INSTDIR\uninstall-spout2-plugin.exe"

SectionEnd

; Modern install component descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${Section1} "Install the Spout 2 OBS Plugin to your installed OBS Studio version"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

UninstallText "This will uninstall Spout2 OBS Studio plugin from your system"

;Uninstall section
Section Uninstall
	SectionIn RO
	AllowSkipFiles off
	;Remove from registry...
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
	DeleteRegKey HKLM "SOFTWARE\${APPNAME}"

	; Delete self

	Delete "$INSTDIR\uninstall-spout2-plugin.exe"

	; Delete Shortcuts
	Delete "$SMPROGRAMS\Spout 2 OBS Plugin\Uninstall Spout2 OBS Plugin.lnk"

	; Clean up Spout 2 OBS Plugin
	Delete "$INSTDIR\bin\64bit\win-spout.dll"
	Delete "$INSTDIR\bin\64bit\Spout.dll"
	Delete "$INSTDIR\bin\64bit\SpoutDX.dll"
	Delete "$INSTDIR\bin\64bit\SpoutLibrary.dll"
	Delete "$INSTDIR\data\locale\en-US.ini"
	Delete "$INSTDIR\data\locale\pt-BR.ini"
	Delete "$INSTDIR\data\locale\zh-CN.ini"
	Delete "$INSTDIR\data\locale\es-ES.ini"

	; Remove remaining directories
	RMDir "$SMPROGRAMS\Spout 2 OBS Plugin"

SectionEnd

; eof
