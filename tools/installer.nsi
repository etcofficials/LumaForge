; LumaForge Windows installer (NSIS 3, Modern UI 2).
; Built by tools\package-installer.ps1 from the portable package folder:
;   makensis /DVERSION=0.4.0 /DSRCDIR=<dist\LumaForge-v0.4.0-Windows-x64> /DOUTFILE=<setup.exe> /DICON=<ico> /DDIRS=<uninstall-dirs.nsh> installer.nsi
;
; Per-user install (no administrator rights): %LOCALAPPDATA%\Programs\LumaForge.
; LumaForge keeps its data in app\LumaForgeData next to the program when it
; runs from the system drive, so the install folder must be writable.
; Uninstalling removes the program but keeps LumaForgeData (projects, exports).

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!include "MUI2.nsh"

!define APPNAME "LumaForge"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\LumaForge"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\LumaForge"
InstallDirRegKey HKCU "${UNINSTKEY}" "InstallLocation"
BrandingText "${APPNAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APPNAME} ${VERSION} Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "LumaForge contributors (GPL; see licenses)"

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\LumaForge Launcher.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start LumaForge"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\README.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "LumaForge (required)" SecMain
    SectionIn RO
    SetOutPath "$INSTDIR"
    File "${SRCDIR}\LumaForge Launcher.exe"
    File "${SRCDIR}\README.txt"
    File /r "${SRCDIR}\app"
    File /r "${SRCDIR}\docs"
    File /r "${SRCDIR}\licenses"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    CreateDirectory "$SMPROGRAMS\LumaForge"
    CreateShortcut "$SMPROGRAMS\LumaForge\LumaForge.lnk" "$INSTDIR\LumaForge Launcher.exe" "" "$INSTDIR\app\LumaForge.exe" 0
    CreateShortcut "$SMPROGRAMS\LumaForge\Uninstall LumaForge.lnk" "$INSTDIR\Uninstall.exe"

    WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "LumaForge"
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\app\LumaForge.exe"
    WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKCU "${UNINSTKEY}" "URLInfoAbout" "https://github.com/etcofficials/LumaForge"
    WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Desktop shortcut" SecDesktop
    CreateShortcut "$DESKTOP\LumaForge.lnk" "$INSTDIR\LumaForge Launcher.exe" "" "$INSTDIR\app\LumaForge.exe" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "The LumaForge program files."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A LumaForge shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    ; Program files only: app\LumaForgeData (projects, exports, caches) is kept.
    Delete "$INSTDIR\app\*.dll"
    Delete "$INSTDIR\app\*.exe"
    !include "${DIRS}"
    RMDir "$INSTDIR\app"
    RMDir /r "$INSTDIR\docs"
    RMDir /r "$INSTDIR\licenses"
    Delete "$INSTDIR\LumaForge Launcher.exe"
    Delete "$INSTDIR\README.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    Delete "$SMPROGRAMS\LumaForge\LumaForge.lnk"
    Delete "$SMPROGRAMS\LumaForge\Uninstall LumaForge.lnk"
    RMDir "$SMPROGRAMS\LumaForge"
    Delete "$DESKTOP\LumaForge.lnk"
    DeleteRegKey HKCU "${UNINSTKEY}"
SectionEnd
