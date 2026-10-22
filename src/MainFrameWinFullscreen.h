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

inline bool HandleNcCalcSize(HWND hwnd, WPARAM wParam, LPARAM lParam,
                             bool isFullscreen) {
  if (wParam != TRUE || !isFullscreen || !hwnd)
    return false;

  NCCALCSIZE_PARAMS *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam);

  RECT windowRect;
  if (!::GetWindowRect(hwnd, &windowRect))
    return false;

  params->rgrc[0].right =
      params->rgrc[0].left + (windowRect.right - windowRect.left);
  params->rgrc[0].bottom =
      params->rgrc[0].top + (windowRect.bottom - windowRect.top);

  return true;
}

} // namespace WinFullscreen

#endif // __WXMSW__
#endif // MAINFRAME_WIN_FULLSCREEN_H