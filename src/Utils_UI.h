#pragma once

#include <wx/app.h>
#include <wx/font.h>
#include <wx/string.h>
#include <wx/window.h>

#include <cstddef>
#include <ctime>
#include <functional>
#include <string>

// ============================================================================
// Измерение текста с переносом
// ============================================================================
int MeasureWrappedTextHeight(wxWindow *parent, const wxString &text,
                             int wrapWidth, const wxFont &font);

// ============================================================================
// Диалоги
// ============================================================================
void showError(wxWindow *parent, const wxString &message,
               const wxString &title = "Error");
void showInfo(wxWindow *parent, const wxString &message,
              const wxString &caption = "Info");

// ============================================================================
// Форматирование времени
// ============================================================================
wxString formatTimestamp(std::time_t timestamp);
std::string FormatLocalTime(time_t t, const wxString &format);

// ============================================================================
// DPI / Responsive layout
// ============================================================================
double GetContentScale(wxWindow *ctx);

inline constexpr int LIST_LOGO_SIZE_DIP = 40;
inline constexpr int CARD_BASE_W_DIP = 340;
inline constexpr int CARD_BASE_H_DIP = 90;

int CardWidthForDisplay(int displayW);

struct CardLayoutInfo {
  int cardW = 0, cardH = 0;
  int pad = 0, logoGap = 0;
  int starSize = 0;
  int logoZoneLeft = 0, logoZoneRight = 0, logoZoneW = 0;
  int favZoneSize = 0;
  int logoW = 0, logoH = 0;
  int logoDx = 0, logoDy = 0;
  int starDx = 0, starDy = 0;

  int titleH = 0;
  int titleDx = 0, titleDy = 0;
  int titleFontPt = 9;
};

CardLayoutInfo ComputeCardLayoutForDisplay(int displayW);

// ============================================================================
// Безопасный CallAfter по window id
// ============================================================================
inline void CallAfterSafeById(int winId, std::function<void(wxWindow *)> fn) {
  wxTheApp->CallAfter([winId, fn = std::move(fn)]() mutable {
    wxWindow *w = wxWindow::FindWindowById(winId);
    if (!w)
      return;
    fn(w);
  });
}

template <typename F, typename = std::enable_if_t<!std::is_convertible<
                          F, std::function<void(wxWindow *)>>::value>>
inline void CallAfterSafeById(int winId, F &&fn) {
  auto wrapper = [fn = std::forward<F>(fn)](wxWindow * /*w*/) mutable { fn(); };
  CallAfterSafeById(winId, std::function<void(wxWindow *)>(std::move(wrapper)));
}
