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

  // Получаем толщину невидимой границы изменения размера (Win10/11)
  // SM_CXPADDEDBORDER добавлен в Win10 и дает недостающие пиксели.
  const int frameX =
      GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
  const int frameY =
      GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);

  // Расширяем клиентскую область на толщину невидимых границ.
  // Это заставляет Windows считать, что клиентская область включает
  // в себя эти границы, и она перестает рисовать их как светлую полосу.
  params->rgrc[0].left -= frameX;
  params->rgrc[0].right += frameX;
  params->rgrc[0].top -= frameY;
  params->rgrc[0].bottom += frameY;

  return true; // MSWWindowProc вернет 0
}
} // namespace WinFullscreen

#endif // __WXMSW__
#endif // MAINFRAME_WIN_FULLSCREEN_H