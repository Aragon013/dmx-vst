; NSIS Installer Script for LuxSync DMX VST3 (Windows)
; This script installs the VST3 plugin to the standard VST3 folder

!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

; Configuration
!define PRODUCT_NAME "LuxSync DMX VST3"
!define PRODUCT_VERSION "0.1.0"
!define PRODUCT_PUBLISHER "Aragon"
!define PRODUCT_WEB_SITE "https://github.com/Aragonlu/dmx-vst"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"

; Check Windows version (require Windows 7+)
${If} ${IsWin2003}
    MessageBox MB_OK "Windows 7 or later is required!"
    Quit
${EndIf}

; Set output file
OutFile "LuxSync-DMX-VST3-v${PRODUCT_VERSION}-Windows-x64.exe"
InstallDir "$PROGRAMFILES\Common Files\VST3"
ShowInstDetails show
ShowUnInstDetails show

; MUI Settings
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Installer sections
Section "LuxSync DMX VST3 Plugin"
  SectionIn RO
  
  SetOutPath "$INSTDIR\Aragon\LuxSync DMX.vst3"
  
  ; Copy all plugin files (replace if exists)
  SetOverwrite try
  File /r "artifacts\windows\vst3\LuxSync DMX.vst3\*.*"
  
  DetailPrint "Installed to: $INSTDIR\Aragon\LuxSync DMX.vst3"
SectionEnd

Section "Register Plugin"
  DetailPrint "Registering plugin in VST3 host cache..."
  ; Note: Most DAWs auto-scan VST3 folder, but we can add registry entry if needed
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
SectionEnd

; Uninstaller section
Section "Uninstall"
  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
  RMDir /r "$INSTDIR\Aragon\LuxSync DMX.vst3"
  MessageBox MB_OK "Plugin uninstalled successfully! You may need to restart your DAW."
SectionEnd

Function .onInit
  ${If} ${RunningX64}
    ; Continue on 64-bit
  ${Else}
    MessageBox MB_OK "This installer requires Windows 64-bit"
    Quit
  ${EndIf}
FunctionEnd
