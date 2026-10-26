#include "Utils_UI.h"

#include <wx/dcclient.h>
#include <wx/msgdlg.h>
#include <wx/thread.h>
#include <wx/tokenzr.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

// ============================================================================
// MeasureWrappedTextHeight
// ============================================================================
int MeasureWrappedTextHeight(wxWindow *parent, const wxString &text,
                             int wrapWidth, const wxFont &font) {
  wxClientDC dc(parent);
  dc.SetFont(font);
  const int lineHeight = dc.GetCharHeight();

  int totalHeight = 0;
  wxStringTokenizer tok(text, "\n", wxTOKEN_RET_EMPTY_ALL);
  while (tok.HasMoreTokens()) {
    wxString para = tok.GetNextToken();
    para.Replace("\r", "");

    if (para.IsEmpty()) {
      totalHeight += lineHeight;
      continue;
    }

    while (!para.IsEmpty()) {
      const int len = static_cast<int>(para.length());
      int fitLen = 0;
      for (int i = 1; i <= len; ++i) {
        int w = 0, h = 0;
        dc.GetTextExtent(para.Left(i), &w, &h);
        if (w > wrapWidth)
          break;
        fitLen = i;
      }
      if (fitLen <= 0)
        fitLen = 1;

      if (fitLen < len) {
        int lastSpace = para.Left(fitLen + 1).Find(' ', true);
        if (lastSpace > 0)
          fitLen = lastSpace;
      }

      para = para.Mid(fitLen);
      para.Trim(false);
      totalHeight += lineHeight;
    }
  }
  return totalHeight;
}

// ============================================================================
// Диалоги
// ============================================================================
void showError(wxWindow *parent, const wxString &message,
               const wxString &title) {
  if (wxIsMainThread()) {
    wxMessageBox(message, title, wxOK | wxICON_ERROR, parent);
  } else {
    wxTheApp->CallAfter([parent, message, title]() {
      wxMessageBox(message, title, wxOK | wxICON_ERROR, parent);
    });
  }
}

void showInfo(wxWindow *parent, const wxString &message,
              const wxString &caption) {
  if (wxIsMainThread()) {
    wxMessageBox(message, caption, wxOK | wxICON_INFORMATION, parent);
  } else {
    wxTheApp->CallAfter([parent, message, caption]() {
      wxMessageBox(message, caption, wxOK | wxICON_INFORMATION, parent);
    });
  }
}

// ============================================================================
// Форматирование времени
// ============================================================================
wxString formatTimestamp(std::time_t timestamp) {
  if (timestamp == 0)
    return "-";
  std::tm *timeInfo = std::localtime(&timestamp);
  if (!timeInfo)
    return "-";
  std::ostringstream oss;
  oss << std::put_time(timeInfo, "%Y-%m-%d %H:%M:%S");
  return wxString(oss.str());
}

std::string FormatLocalTime(time_t t, const wxString &format) {
  if (t <= 0)
    return "";

  struct tm tm_local;
#ifdef _WIN32
  if (localtime_s(&tm_local, &t) != 0)
    return "";
#else
  if (localtime_r(&t, &tm_local) == nullptr)
    return "";
#endif

  std::string fmt = format.ToUTF8().data();
  char buffer[256];
  size_t len = strftime(buffer, sizeof(buffer), fmt.c_str(), &tm_local);
  if (len == 0)
    return "";
  return std::string(buffer, len);
}

// ============================================================================
// DPI / layout
// ============================================================================
double GetContentScale(wxWindow *ctx) {
  if (!ctx)
    return 1.0;
  double cs = ctx->GetContentScaleFactor();
  return cs < 1.0 ? 1.0 : cs;
}

int CardWidthForDisplay(int displayW_Dip) {
  if (displayW_Dip <= 0)
    return 320;
  int w = displayW_Dip / 5;
  return std::clamp(w, 200, 480);
}

CardLayoutInfo ComputeCardLayoutForDisplay(int displayW) {
  CardLayoutInfo L;
  L.cardW = CardWidthForDisplay(displayW);

  const double k = (double)L.cardW / CARD_BASE_W_DIP;
  const int baseCardH = std::max(
      1, (int)std::round((double)L.cardW * CARD_BASE_H_DIP / CARD_BASE_W_DIP));

  L.pad = std::max(2, (int)std::round(4.0 * k));
  L.logoGap = std::max(1, (int)std::round(1.0 * k));

  L.titleH = std::max(20, (int)std::round(24.0 * k));
  L.titleDx = 0;
  L.titleDy = baseCardH;
  L.titleFontPt = std::max(9, (int)std::round(10.0 * k));

  L.cardH = baseCardH + L.titleH;

  const int contentH = baseCardH;

  L.favZoneSize = contentH;
  L.logoZoneLeft = L.pad;
  L.logoZoneRight = L.cardW - L.pad - L.favZoneSize;
  L.logoZoneW = std::max(1, L.logoZoneRight - L.logoZoneLeft - L.logoGap);

  L.logoH = std::max(1, contentH - 2 * L.pad);
  L.starSize = std::max(24, (int)(contentH * 0.6));

  L.logoW = L.logoZoneW;
  L.logoDx = L.logoZoneLeft;
  L.logoDy = L.pad + (contentH - 2 * L.pad - L.logoH) / 2;
  L.starDx = L.logoZoneRight + L.logoGap + (L.favZoneSize - L.starSize) / 2;
  L.starDy = (L.favZoneSize - L.starSize) / 2;

  return L;
}
