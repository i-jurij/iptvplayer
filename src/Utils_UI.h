#pragma once

#include <wx/app.h>
#include <wx/font.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/string.h>
#include <wx/window.h>

#include <cstddef>
#include <ctime>
#include <functional>
#include <string>

// ============================================================================
// Индикаторы состояния для списков/таблиц.
// ============================================================================
namespace UiGlyph {
inline constexpr const char *kOn = "\xE2\x97\x8F";       // ●  U+25CF online
inline constexpr const char *kOff = "\xE2\x88\x92";      // −  U+2212 offline
inline constexpr const char *kChecking = "\xE2\x97\x8C"; // ◌  U+25CC checking
inline constexpr const char *kPresent = "\xE2\x97\x89";  // ◉  U+25C9 файл есть
inline constexpr const char *kMissing = "\xC3\x97";      // ×  U+00D7 файла нет
} // namespace UiGlyph

// Стандартные размеры диалогов плейлистов.
inline constexpr int DIALOG_WIDTH = 640;
inline constexpr int URL_DIALOG_HEIGHT = 480;
inline constexpr int EDIT_DIALOG_HEIGHT = 350;

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
// Human-readable размер: "512 B", "1.2 MB", "2.35 GB".
// bytes < 0 → пустая строка.
wxString FormatBytes(long long bytes);

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
// Widget helpers
// ============================================================================
// Жирный заголовок секции в sizer.
wxStaticBoxSizer *AddSectionHeader(wxWindow *parent, wxSizer *parentSizer,
                                   const wxString &text, int border = 10);

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
