Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APP_NAME "Tux IME"
!define APP_VERSION "0.14.0"
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
    SetShellVarContext all
    SetOutPath "$INSTDIR"
    nsExec::ExecToLog 'taskkill /IM tuxime-server.exe /F'
    System::Call 'kernel32::GetTickCount()i.r0'
    IfFileExists "$INSTDIR\bin\tuxime-x86_64.dll" 0 tux_no_old_dll
    StrCpy $1 "$INSTDIR\bin\tuxime-x86_64.old-$0.dll"
    Delete "$1"
    Rename "$INSTDIR\bin\tuxime-x86_64.dll" "$1"
    tux_no_old_dll:
    IfFileExists "$INSTDIR\bin\tuxime-x86.dll" 0 tux_no_old_x86
    StrCpy $2 "$INSTDIR\bin\tuxime-x86.old-$0.dll"
    Delete "$2"
    Rename "$INSTDIR\bin\tuxime-x86.dll" "$2"
    tux_no_old_x86:
    File /r "${DIST_DIR}\*.*"
    File "setup-language.ps1"
    ; The previous DLL can stay locked by a running TSF client. It is no
    ; longer referenced, so remove it now or at the next reboot. A unique
    ; backup name avoids colliding with an older .old.dll that is also
    ; still loaded by another process.
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86_64.old-*.dll"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86.old-*.dll"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86_64.old.dll"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86.old.dll"

    ${If} ${RunningX64}
        ExecWait '"$WINDIR\Sysnative\regsvr32.exe" /s "$INSTDIR\bin\tuxime-x86_64.dll"'
        IfFileExists "$INSTDIR\bin\tuxime-x86.dll" 0 tux_no_x86_register
        ExecWait '"$WINDIR\SysWOW64\regsvr32.exe" /s "$INSTDIR\bin\tuxime-x86.dll"'
        tux_no_x86_register:
        ExecWait '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\setup-language.ps1"'
    ${EndIf}

    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateDirectory "$SMPROGRAMS\Tux IME"
    CreateShortCut "$SMPROGRAMS\Tux IME\Tux IME 设置.lnk" \
        "$INSTDIR\bin\tuxime-server.exe" "--settings" \
        "$INSTDIR\bin\penguin.ico" 0
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
    SetShellVarContext all
    SetRegView 64
    ${If} ${RunningX64}
        ExecWait '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\setup-language.ps1" -Remove'
        ExecWait '"$WINDIR\Sysnative\regsvr32.exe" /u /s "$INSTDIR\bin\tuxime-x86_64.dll"'
        IfFileExists "$INSTDIR\bin\tuxime-x86.dll" 0 tux_no_x86_unregister
        ExecWait '"$WINDIR\SysWOW64\regsvr32.exe" /u /s "$INSTDIR\bin\tuxime-x86.dll"'
        tux_no_x86_unregister:
    ${EndIf}

    ; A TSF client in a still-running application can relaunch the old server
    ; right after taskkill. Rename the executables first so it cannot come
    ; back from the path we are about to delete. Unique names avoid a
    ; collision with a previous uninstall's still-locked files.
    System::Call 'kernel32::GetTickCount()i.r0'
    nsExec::ExecToLog 'taskkill /IM tuxime-server.exe /F'
    IfFileExists "$INSTDIR\bin\tuxime-server.exe" 0 tux_no_server_rename
    Rename "$INSTDIR\bin\tuxime-server.exe" "$INSTDIR\bin\tuxime-server.uninstall-$0.exe"
    tux_no_server_rename:
    IfFileExists "$INSTDIR\bin\tuxime-x86_64.dll" 0 tux_no_dll_rename
    Rename "$INSTDIR\bin\tuxime-x86_64.dll" "$INSTDIR\bin\tuxime-x86_64.uninstall-$0.dll"
    tux_no_dll_rename:
    IfFileExists "$INSTDIR\bin\tuxime-x86.dll" 0 tux_no_x86_dll_rename
    Rename "$INSTDIR\bin\tuxime-x86.dll" "$INSTDIR\bin\tuxime-x86.uninstall-$0.dll"
    tux_no_x86_dll_rename:
    nsExec::ExecToLog 'taskkill /IM tuxime-server.exe /F'
    nsExec::ExecToLog 'taskkill /IM tuxime-server.uninstall-*.exe /F'
    Sleep 500

    DeleteRegKey HKLM "${APP_KEY}"
    DeleteRegKey HKLM "Software\TuxIME"
    Delete "$SMPROGRAMS\Tux IME\Tux IME 设置.lnk"
    RMDir "$SMPROGRAMS\Tux IME"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-server.uninstall-*.exe"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86_64.uninstall-*.dll"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86.uninstall-*.dll"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86_64.old-*.dll"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86.old-*.dll"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86_64.old.dll"
    Delete /REBOOTOK "$INSTDIR\bin\tuxime-x86.old.dll"
    RMDir /r "$INSTDIR"
    RMDir /REBOOTOK "$INSTDIR\bin"
    RMDir /REBOOTOK "$INSTDIR\lib"
    RMDir /REBOOTOK "$INSTDIR\data"
    RMDir /REBOOTOK "$INSTDIR"
SectionEnd
