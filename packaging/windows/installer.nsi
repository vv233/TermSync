; TermSync Windows installer. Packages the windeployqt-staged dist\TermSync
; folder. Invoked from the repo root as:
;   makensis -DVERSION=<tag> packaging\windows\installer.nsi

!include "MUI2.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
; Absolute path to the windeployqt-staged folder, passed via /DDISTDIR=...
!ifndef DISTDIR
  !define DISTDIR "dist\TermSync"
!endif

Name "TermSync ${VERSION}"
OutFile "TermSync-${VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES64\TermSync"
InstallDirRegKey HKLM "Software\TermSync" "InstallDir"
RequestExecutionLevel admin
Unicode true
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\termsync.exe"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "TermSync (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; Everything windeployqt staged (exe + Qt DLLs + plugins).
  File /r "${DISTDIR}\*"

  CreateShortcut "$SMPROGRAMS\TermSync.lnk" "$INSTDIR\termsync.exe"
  CreateShortcut "$DESKTOP\TermSync.lnk" "$INSTDIR\termsync.exe"

  WriteRegStr HKLM "Software\TermSync" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Add/Remove Programs entry.
  !define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TermSync"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "TermSync"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "TermSync"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\termsync.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\TermSync.lnk"
  Delete "$DESKTOP\TermSync.lnk"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TermSync"
  DeleteRegKey HKLM "Software\TermSync"
SectionEnd
