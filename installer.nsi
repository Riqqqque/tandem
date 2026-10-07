OutFile "tandem-setup.exe"

!ifndef PLUGIN_SOURCE_DIR
    !define PLUGIN_SOURCE_DIR "release\Release\tandem"
!endif

Unicode true
RequestExecutionLevel user

SetDatablockOptimize on
SetCompress auto
SetCompressor /SOLID lzma

Name "Tandem"
Caption "Tandem for OBS Studio"
Icon "${NSISDIR}\Contrib\Graphics\Icons\win-install.ico"

Var /Global DefInstDir
Function .onInit
    ReadEnvStr $0 "ALLUSERSPROFILE"
    StrCpy $DefInstDir "$0\obs-studio\plugins"
    StrCpy $INSTDIR "$DefInstDir"

    IfFileExists "$DefInstDir\tandem\*.*" AskUninst DontAskUninst
    AskUninst:
        MessageBox MB_YESNO|MB_ICONQUESTION "Tandem is already installed.$\r$\n$\r$\nYes = install or update$\r$\nNo = remove Tandem" IDYES NotDoUninst IDNO DoUninst
    DoUninst:
        ; Only Tandem's own folder. Other plugins live next to it.
        RMDir /r "$DefInstDir\tandem"
        MessageBox MB_OK|MB_ICONINFORMATION "Tandem was removed. Your settings stay in each OBS profile (tandem.json) and stream keys stay in Windows Credential Manager under Tandem/."
        Quit
    NotDoUninst:
    DontAskUninst:
FunctionEnd

Function onDirPageLeave
StrCmp "$INSTDIR" "$DefInstDir" DirNotModified DirModified
DirModified:
MessageBox MB_OK|MB_ICONSTOP "Please don't change the install directory."
Abort
DirNotModified:
FunctionEnd

Page directory "" "" onDirPageLeave
Page instfiles

Section
SetOutPath "$INSTDIR"
File /r "${PLUGIN_SOURCE_DIR}"
SectionEnd
