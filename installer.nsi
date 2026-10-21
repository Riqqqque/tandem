OutFile "tandem-setup.exe"

!ifndef VERSION
    !define VERSION "dev"
!endif

!ifndef PLUGIN_SOURCE_DIR
    !define PLUGIN_SOURCE_DIR "release\Release\tandem"
!endif

Unicode true
RequestExecutionLevel user

SetDatablockOptimize on
SetCompress auto
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "WordFunc.nsh"

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tandem"
!define GUIDE_URL "https://github.com/Riqqqque/tandem/wiki/Quick-Start"
!define MIN_OBS "32.2.0.0"

Name "Tandem"
Caption "Tandem for OBS Studio"
BrandingText "Tandem"
ShowInstDetails nevershow
ShowUninstDetails nevershow

Var ObsDir
Var ObsExe

; ---------------------------------------------------------------- helpers

; Sets $0 to 1 while OBS is running.
!macro IsObsRunning
    nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq obs64.exe" /NH | find /I "obs64.exe"'
    Pop $0
    Pop $1
    ${If} $0 == 0
        StrCpy $0 1
    ${Else}
        StrCpy $0 0
    ${EndIf}
!macroend

; Waits until OBS is closed, or quits when the user cancels.
!macro WaitForObsClosed
    ${Do}
        !insertmacro IsObsRunning
        ${If} $0 == 0
            ${Break}
        ${EndIf}
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "OBS Studio is open.$\r$\n$\r$\nClose OBS, then click Retry." IDRETRY +2
        Quit
    ${Loop}
!macroend

Function FindObs
    StrCpy $ObsDir ""
    StrCpy $ObsExe ""
    ReadRegStr $ObsDir HKLM "Software\OBS Studio" ""
    ${If} $ObsDir == ""
        ReadRegStr $ObsDir HKCU "Software\OBS Studio" ""
    ${EndIf}
    ${If} $ObsDir == ""
    ${AndIf} ${FileExists} "$PROGRAMFILES64\obs-studio\bin\64bit\obs64.exe"
        StrCpy $ObsDir "$PROGRAMFILES64\obs-studio"
    ${EndIf}
    ${If} $ObsDir != ""
    ${AndIf} ${FileExists} "$ObsDir\bin\64bit\obs64.exe"
        StrCpy $ObsExe "$ObsDir\bin\64bit\obs64.exe"
    ${EndIf}
FunctionEnd

; ---------------------------------------------------------------- pages

!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "Install Tandem"
!define MUI_WELCOMEPAGE_TEXT "Tandem lets OBS Studio stream to Twitch, Kick and YouTube at the same time, and shows all their chats in one place.$\r$\n$\r$\nClick Install. It takes a few seconds and doesn't need administrator rights."
!define MUI_PAGE_CUSTOMFUNCTION_SHOW WelcomeShow
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_TITLE "Tandem is installed"
!define MUI_FINISHPAGE_TEXT "Open OBS and look for the Tandem dock. Tick the platforms you want, paste each stream key (every box links to where you find it) and press Go Live."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Start OBS now"
!define MUI_FINISHPAGE_RUN_FUNCTION StartObs
!define MUI_FINISHPAGE_SHOWREADME "${GUIDE_URL}"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open the quick start guide"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!define MUI_PAGE_CUSTOMFUNCTION_SHOW FinishShow
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------- install

Function .onInit
    ReadEnvStr $0 "ALLUSERSPROFILE"
    StrCpy $INSTDIR "$0\obs-studio\plugins\tandem"

    Call FindObs
    ${If} $ObsExe == ""
        MessageBox MB_YESNO|MB_ICONQUESTION "OBS Studio doesn't seem to be installed.$\r$\n$\r$\nTandem is a plugin for OBS Studio 32.2 or newer. Get OBS from obsproject.com first.$\r$\n$\r$\nInstall Tandem anyway?" IDYES +2
        Quit
    ${Else}
        ClearErrors
        GetDLLVersion "$ObsExe" $R0 $R1
    ${EndIf}
    ${If} $ObsExe != ""
    ${AndIfNot} ${Errors}
        IntOp $R2 $R0 >> 16
        IntOp $R3 $R0 & 0xFFFF
        IntOp $R4 $R1 >> 16
        IntOp $R5 $R1 & 0xFFFF
        ${VersionCompare} "$R2.$R3.$R4.$R5" "${MIN_OBS}" $R6
        ${If} $R6 == 2
            MessageBox MB_YESNO|MB_ICONEXCLAMATION "Your OBS Studio is version $R2.$R3.$R4. Tandem needs 32.2 or newer and won't load until you update OBS (Help > Check for Updates).$\r$\n$\r$\nInstall Tandem anyway?" IDYES +2
            Quit
        ${EndIf}
    ${EndIf}

    !insertmacro WaitForObsClosed
FunctionEnd

Section
    ; Replace the previous version cleanly; settings live in the OBS profiles, not here.
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\data"
    SetOutPath "$INSTDIR"
    File /r "${PLUGIN_SOURCE_DIR}\*.*"

    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "Tandem for OBS Studio"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "Rique"
    WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "https://github.com/Riqqqque/tandem"
    WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Function WelcomeShow
    ; The next step installs right away, so say so on the button.
    GetDlgItem $0 $HWNDPARENT 1
    SendMessage $0 ${WM_SETTEXT} 0 "STR:Install"
FunctionEnd

Function FinishShow
    ; No point offering to start OBS if it couldn't be found.
    ${If} $ObsExe == ""
        SendMessage $mui.FinishPage.Run ${BM_SETCHECK} 0 0
        EnableWindow $mui.FinishPage.Run 0
    ${EndIf}
FunctionEnd

Function StartObs
    ${If} $ObsExe != ""
        SetOutPath "$ObsDir\bin\64bit"
        Exec '"$ObsExe"'
    ${EndIf}
FunctionEnd

; ---------------------------------------------------------------- uninstall

Function un.onInit
    !insertmacro WaitForObsClosed
FunctionEnd

Section "Uninstall"
    ; Only Tandem's own folder. Other plugins live next to it.
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\data"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    DeleteRegKey HKCU "${UNINST_KEY}"
    MessageBox MB_OK|MB_ICONINFORMATION "Tandem was removed. Your settings stay in each OBS profile (tandem.json) and stream keys stay in Windows Credential Manager under Tandem/."
SectionEnd
