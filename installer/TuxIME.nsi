Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APP_NAME "Tux IME"
!define APP_VERSION "0.1.0"
!define APP_PUBLISHER "Tux IME Project"
!define APP_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TuxIME"
!define DIST_DIR "..\dist\TuxIME-final"

Name "${APP_NAME}"
OutFile "TuxIME-Setup-${APP_VERSION}.exe"
InstallDir "$PROGRAMFILES64\TuxIME"
InstallDirRegKey HKLM "Software\TuxIME" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${APP_NAME} ${APP_VERSION}"

!define MUI_ABORTWARNING
!define MUI_ICON "..\dist\TuxIME-final\bin\penguin.ico"
!define MUI_UNICON "..\dist\TuxIME-final\bin\penguin.ico"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.md"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "查看安装与启用说明"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    SetRegView 64
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "Tux IME currently requires 64-bit Windows."
        Abort
    ${EndIf}
FunctionEnd

Section "Tux IME" SecMain
    SetOutPath "$INSTDIR"
    nsExec::ExecToLog 'taskkill /IM tuxime-server.exe /F'
    File /r "${DIST_DIR}\*.*"

    ${If} ${RunningX64}
        ExecWait '"$WINDIR\Sysnative\regsvr32.exe" /s "$INSTDIR\bin\tuxime-x86_64.dll"'
    ${EndIf}

    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\TuxIME" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${APP_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${APP_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "${APP_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "${APP_KEY}" "DisplayIcon" "$INSTDIR\bin\penguin.ico"
    WriteRegStr HKLM "${APP_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${APP_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "${APP_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKLM "${APP_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${APP_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKLM "${APP_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Uninstall"
    SetRegView 64
    nsExec::ExecToLog 'taskkill /IM tuxime-server.exe /F'
    ${If} ${RunningX64}
        ExecWait '"$WINDIR\Sysnative\regsvr32.exe" /u /s "$INSTDIR\bin\tuxime-x86_64.dll"'
    ${EndIf}

    DeleteRegKey HKLM "${APP_KEY}"
    DeleteRegKey HKLM "Software\TuxIME"
    RMDir /r "$INSTDIR"
SectionEnd
