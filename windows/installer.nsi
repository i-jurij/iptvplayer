; =============================================================================
; windows/installer.nsi — NSIS-инсталлятор iptvplayer.
; =============================================================================

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef INSTALL_SRC
  !error "INSTALL_SRC не задан (передайте через -DINSTALL_SRC)"
!endif
!ifndef OUTPUT
  !define OUTPUT "iptvplayer-${VERSION}-win64-setup.exe"
!endif

Name "IPTV Player ${VERSION}"
OutFile "${OUTPUT}"
InstallDir "$PROGRAMFILES64\IPTV Player"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "${INSTALL_SRC}\*.*"

    CreateDirectory "$SMPROGRAMS\IPTV Player"
    CreateShortcut "$SMPROGRAMS\IPTV Player\IPTV Player.lnk" "$INSTDIR\iptvplayer.exe"
    CreateShortcut "$DESKTOP\IPTV Player.lnk" "$INSTDIR\iptvplayer.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\IPTVPlayer" \
        "DisplayName" "IPTV Player"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\IPTVPlayer" \
        "UninstallString" "$INSTDIR\uninstall.exe"
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\*.*"
    RMDir /r "$INSTDIR"

    Delete "$SMPROGRAMS\IPTV Player\IPTV Player.lnk"
    RMDir "$SMPROGRAMS\IPTV Player"
    Delete "$DESKTOP\IPTV Player.lnk"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\IPTVPlayer"
SectionEnd