#ifndef MAINFRAME_WIN_FULLSCREEN_H
#define MAINFRAME_WIN_FULLSCREEN_H

#if defined(__WXMSW__)

#include <dwmapi.h>
#include <windows.h>

namespace WinFullscreen {

inline void ApplyDwmFullscreen(HWND hwnd, bool fs) {
  if (!hwnd)
    return;

  BOOL compositionEnabled = FALSE;
  if (SUCCEEDED(DwmIsCompositionEnabled(&compositionEnabled)) &&
      compositionEnabled) {
    MARGINS margins{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
  }

  if (fs) {
    int cornerPref = 1;
    DWORD borderNone = 0xFFFFFFFE;
    DwmSetWindowAttribute(hwnd, 33, &cornerPref, sizeof(cornerPref));
    DwmSetWindowAttribute(hwnd, 34, &borderNone, sizeof(borderNone));
  } else {
    int cornerDefault = 0;
    DWORD borderDefault = 0xFFFFFFFF;
    DwmSetWindowAttribute(hwnd, 33, &cornerDefault, sizeof(cornerDefault));
    DwmSetWindowAttribute(hwnd, 34, &borderDefault, sizeof(borderDefault));
  }
}
} // namespace WinFullscreen

#endif // __WXMSW__
#endif // MAINFRAME_WIN_FULLSCREEN_H