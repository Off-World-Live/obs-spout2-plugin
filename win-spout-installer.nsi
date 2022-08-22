; Script generated with the Venis Install Wizard

Unicode True

; Define your application name
!define APPNAME "Spout 2 OBS Plugin"
!define APPVERSION "DebugVersion"
!define APPNAMEANDVERSION "Spout 2 OBS Plugin ${APPVERSION}"

; Main Install settings
Name "${APPNAMEANDVERSION}"
InstallDirRegKey HKLM "Software\${APPNAME}" ""
InstallDir "$PROGRAMFILES64\obs-studio"
OutFile "OBS_Spout2_Plugin_Install_v${APPVERSION}.exe"

Var INSTALL_BASE_DIR
Var OBS_INSTALL_DIR


; Use compression
SetCompressor Zlib

; Modern interface settings
!include "MUI.nsh"

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Set languages (first is default language)
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_RESERVEFILE_LANGDLL
Section "Spout 2 OBS Plugin" Section1
	StrCpy $INSTALL_BASE_DIR "$PROGRAMFILES64\obs-studio"

	ReadRegStr $OBS_INSTALL_DIR HKLM "SOFTWARE\OBS Studio" ""

	!if "$OBS_INSTALL_DIR" != ""
		StrCpy $INSTALL_BASE_DIR "$OBS_INSTALL_DIR"
	!endif


	StrCpy $InstDir "$INSTALL_BASE_DIR"

	IfFileExists "$INSTDIR\*.*" +3
		MessageBox MB_OK|MB_ICONSTOP "OBS Directory doesn't exist!"
		Abort


	; Set Section properties
	SetOverwrite on
	AllowSkipFiles off

	; Set Section Files and Shortcuts
	SetOutPath "$INSTDIR\obs-plugins\64bit\"
	File "..\..\build64\plugins\win-spout\Release\win-spout.dll"
	File "..\..\plugins\win-spout\deps\Spout2\BUILD\Binaries\x64\Spout.dll"
	File "..\..\plugins\win-spout\deps\Spout2\BUILD\Binaries\x64\SpoutDX.dll"
	File "..\..\plugins\win-spout\deps\Spout2\BUILD\Binaries\x64\SpoutLibrary.dll"

	SetOutPath "$INSTDIR\data\obs-plugins\win-spout\locale\"
	File "data\locale\en-US.ini"
	File "data\locale\zh-CN.ini"
	CreateDirectory "$SMPROGRAMS\Spout 2 OBS Plugin"
	CreateShortCut "$SMPROGRAMS\Spout 2 OBS Plugin\Uninstall Spout2 OBS Plugin.lnk" "$INSTDIR\obs-plugins\uninstall-spout2-plugin.exe"

SectionEnd

Section -FinishSection

	WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" "$INSTDIR\obs-plugins\uninstall-spout2-plugin.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Publisher" "OBS Spout2 Plugin"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "HelpLink" "https://github.com/Off-World-Live/obs-spout2-source-plugin"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "${APPVERSION}"
	WriteUninstaller "$INSTDIR\obs-plugins\uninstall-spout2-plugin.exe"

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
	Delete "$INSTDIR\64bit\win-spout.dll"
	Delete "$INSTDIR\64bit\Spout.dll"
	Delete "$INSTDIR\64bit\SpoutDX.dll"
	Delete "$INSTDIR\64bit\SpoutLibrary.dll"
	Delete "$INSTDIR\..\data\obs-plugins\win-spout\locale\en-US.ini"
	Delete "$INSTDIR\..\data\obs-plugins\win-spout\locale\zh-CN.ini"

	; We still clean up .exp and .lib files even though we don't currently install them 
	; because we used to install them in older versions
	Delete "$INSTDIR\64bit\win-spout.exp"
	Delete "$INSTDIR\64bit\win-spout.lib"
	Delete "$INSTDIR\64bit\Spout.lib"
	Delete "$INSTDIR\64bit\SpoutLibrary.lib"

	; Remove remaining directories
	RMDir "$SMPROGRAMS\Spout 2 OBS Plugin"
	RMDir "$INSTDIR\..\data\obs-plugins\win-spout\locale\"
	RMDir "$INSTDIR\..\data\obs-plugins\win-spout\"

SectionEnd

; eof
