#include "CardsBase.h"
#include "LogoCache.h"
#include "Profiler.h"
#include "Utils.h"
#include "VP_SvgIcon.h"

#include <wx/display.h>

#include <algorithm>

void CardsBase::UpdateLayout() {
  const int oldCols = m_cols;
  const int oldHover = m_hoverIndex;
  const int oldCardW = m_cardW;
  const double oldCS = m_contentScale;

  int clientW = GetClientSize().GetWidth();
  if (clientW <= 0)
    clientW = 800;

  // Размер карточки — по ширине монитора, не окна.
  // Число колонок ниже — по ширине окна.
  int displayW = clientW; // fallback
  {
    int displayIdx = wxDisplay::GetFromWindow(this);
    if (displayIdx == wxNOT_FOUND)
      displayIdx = 0;
    wxRect area = wxDisplay(displayIdx).GetClientArea();
    if (area.GetWidth() > 0)
      displayW = area.GetWidth();
  }

  auto L = ComputeCardLayoutForDisplay(displayW);

  m_cardW = L.cardW;
  m_cardH = L.cardH;
  m_pad = L.pad;
  m_logoGap = L.logoGap;
  m_starW = L.starSize;
  m_logoW = L.logoW;
  m_logoH = L.logoH;

  m_layout.cardW = L.cardW;
  m_layout.cardH = L.cardH;
  m_layout.pad = L.pad;
  m_layout.logoGap = L.logoGap;
  m_layout.starSize = L.starSize;
  m_layout.logoZoneLeft = L.logoZoneLeft;
  m_layout.logoZoneRight = L.logoZoneRight;
  m_layout.logoZoneW = L.logoZoneW;
  m_layout.favZoneSize = L.favZoneSize;
  m_layout.logoW = L.logoW;
  m_layout.logoH = L.logoH;
  m_layout.logoDx = L.logoDx;
  m_layout.logoDy = L.logoDy;
  m_layout.starDx = L.starDx;
  m_layout.starDy = L.starDy;

  // Гэпы — в DIP.
  m_gapX = FromDIP(6);
  m_gapY = FromDIP(6);

  m_colW = m_cardW + m_gapX;
  m_rowH = m_cardH + m_gapY;

  m_cols = std::max(1, clientW / std::max(1, m_colW));
  if (!m_channels.empty())
    m_cols = std::min(m_cols, (int)m_channels.size());

  size_t rows =
      m_channels.empty() ? 1 : (m_channels.size() + m_cols - 1) / m_cols;

  int totalGridWidth =
      (m_cols > 0) ? (m_cols * m_cardW + (m_cols - 1) * m_gapX) : 0;

  m_gridOffsetX = std::max(0, (clientW - totalGridWidth) / 2);

  int totalW = std::max(clientW, totalGridWidth);
  int totalH = (int)rows * m_rowH;
  SetVirtualSize(totalW, totalH);

  double newCS = GetContentScale(this);
  m_contentScale = newCS;

  if (oldCardW != m_cardW || oldCS != newCS) {
    const int oldPhysW = std::max(1, (int)std::round(oldCardW * oldCS));
    const int oldPhysH =
        std::max(1, (int)std::round((double)oldCardW * CARD_BASE_H_DIP /
                                    CARD_BASE_W_DIP * oldCS));
    const int oldScale100 = std::max(100, (int)std::round(oldCS * 100.0));

    LogoCache::ClearScaledRemoveSizes(
        {std::make_tuple(oldPhysW, oldPhysH, oldScale100)});

    m_tileCache.clear();
    m_tileLRU.clear();
    m_tileLRUCache.clear();
  }

  Refresh(false);

  bool hoverInvalid = (m_hoverIndex >= (int)m_channels.size());

  bool hoverRowChanged = false;
  if (oldHover >= 0 && oldCols > 0 && m_cols > 0) {
    int oldRow = oldHover / oldCols;
    int newRow = oldHover / m_cols;
    hoverRowChanged = (oldRow != newRow);
  }

  if (hoverInvalid || hoverRowChanged) {
    int old = m_hoverIndex;
    m_hoverIndex = -1;
    m_hoverFav = false;
    if (old >= 0)
      InvalidateCardClientRectByIndex(old);
  }

  int winId = this->GetId();
  CallAfterSafeById(winId, [](wxWindow *w) {
    auto *self = dynamic_cast<CardsBase *>(w);
    if (!self)
      return;
    if (self->m_closing)
      return;
    self->UpdateHoverAtPoint(self->m_lastMouseClientPos);
  });

  {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_textCache.clear();
    m_textSizeCache.clear();
  }
}

void CardsBase::DrawCardFrame(wxDC &dc, int index, const wxColour &color,
                              int thicknessDIP) const {
  if (index < 0 || index >= (int)m_channels.size() || m_cols <= 0)
    return;

  const int row = index / m_cols;
  const int col = index % m_cols;
  const int x = m_gridOffsetX + col * m_colW;
  const int y = row * m_rowH;

  const int thickness = FromDIP(thicknessDIP);

  wxPen pen(color, thickness);
  pen.SetCap(wxCAP_BUTT);
  pen.SetJoin(wxJOIN_MITER);
  dc.SetPen(pen);
  dc.SetBrush(*wxTRANSPARENT_BRUSH);

  const int half = thickness / 2;
  dc.DrawRectangle(x + half, y + half, m_cardW - thickness,
                   m_cardH - thickness);
}

wxBitmap CardsBase::GetScaledStar(const wxBitmap &star, int sizeDip) {
  if (!star.IsOk())
    return wxBitmap();

  double cs = m_contentScale;
  int sizePhys = std::max(1, (int)std::round(sizeDip * cs));
  wxImage img = star.ConvertToImage();
  img.Rescale(sizePhys, sizePhys, wxIMAGE_QUALITY_HIGH);
  wxBitmap bmp(img);
  bmp.SetScaleFactor(cs); // логический размер = sizeDip
  return bmp;
}

void CardsBase::DrawCardBase(wxDC &dc, size_t index, const wxRect &rect,
                             bool /*hovered*/) {
  const Channel &ch = m_channels[index];
  const int pad = m_pad;
  const LayoutInfo &L = m_layout;

  dc.SetBrush(wxBrush(LogoCache::GetDefaultCardBgColor()));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(rect);

  LogoCache::LogoBitmapPtr bmpPtr = nullptr;
  const std::string &url = ch.getLogo();
  double cs = m_contentScale;
  int physW = std::max(1, (int)std::round(m_logoW * cs));
  int physH = std::max(1, (int)std::round(m_logoH * cs));
  int scale100 = std::max(100, (int)std::round(cs * 100.0));

  if (!url.empty()) {
    const std::string key = LogoCache::MakeScaledKey(
        ch.getPlaylistName(), ch.getName(), physW, physH, scale100);

    bmpPtr = LogoCache::GetCachedBitmapPtr(key);

    if (!bmpPtr)
      RequestLogo(index);
  }

  bool hasLogo = bmpPtr && bmpPtr->IsOk() && bmpPtr->GetWidth() > 8 &&
                 bmpPtr->GetHeight() > 8;

  if (!hasLogo) {
    wxString text = GetTruncatedText(ch);

    int logoAreaLeft = rect.x + L.logoDx;
    int logoAreaRight = rect.x + L.starDx - pad;
    int logoAreaW = logoAreaRight - logoAreaLeft;

    int fontSize = FromDIP(12);
    int minFont = FromDIP(8);
    wxFont font = wxFontInfo(fontSize).Bold();
    dc.SetFont(font);

    wxCoord tw, th;
    dc.GetTextExtent(text, &tw, &th);

    while (tw > logoAreaW && fontSize > minFont) {
      fontSize -= 1;
      font = wxFontInfo(fontSize).Bold();
      dc.SetFont(font);
      dc.GetTextExtent(text, &tw, &th);
    }

    if (tw > logoAreaW) {
      while (text.Length() > 3) {
        text.RemoveLast();
        text += "...";
        dc.GetTextExtent(text, &tw, &th);
        if (tw <= logoAreaW)
          break;
        text.RemoveLast(3);
      }
    }

    int tx = logoAreaLeft + (logoAreaW - tw) / 2;
    int ty = rect.y + L.logoDy + (m_logoH - th) / 2;

    static auto fg = wxColour(32, 32, 32);
    if (wxSystemSettings::GetAppearance().IsDark())
      fg = wxColour(240, 240, 240);
    dc.SetTextForeground(fg);
    dc.SetFont(font);
    dc.DrawText(text, tx, ty);
  }

  if (hasLogo)
    dc.DrawBitmap(*bmpPtr, rect.x + L.logoDx, rect.y + L.logoDy, true);

  wxBitmap star = GetStarBitmap(ch);
  if (star.IsOk()) {
    wxBitmap scaled = GetScaledStar(star, L.starSize);
    dc.DrawBitmap(scaled, rect.x + L.starDx, rect.y + L.starDy, true);
  }
}

void CardsBase::MarkCardDirty(int index) {
  if (index < 0)
    return;
  int row = index / m_cols;
  int col = index % m_cols;

  m_dirtyCards.push_back(index);
  if (!m_redrawTimer.IsRunning())
    m_redrawTimer.StartOnce(16);

  int x = m_gridOffsetX + col * m_colW;
  int y = row * m_rowH;
  RefreshRect(wxRect(x, y, m_cardW, m_cardH), false);
}

void CardsBase::OnRedrawTimer(wxTimerEvent &) {
  if (m_dirtyCards.empty())
    return;
  wxRect dirty;
  for (int idx : m_dirtyCards) {
    if (idx < 0 || idx >= (int)m_channels.size())
      continue;
    int row = idx / m_cols;
    int col = idx % m_cols;
    int x = m_gridOffsetX + col * m_colW;
    int y = row * m_rowH;
    dirty.Union(wxRect(x, y, m_cardW, m_cardH));
  }
  m_dirtyCards.clear();
  RefreshRect(dirty, false);
}

void CardsBase::OnPaint(wxPaintEvent &) {
  PROFILE_SCOPE("OnPaint");

  int sx, sy;
  GetViewStart(&sx, &sy);
  int px, py;
  GetScrollPixelsPerUnit(&px, &py);
  if (px <= 0)
    px = 1;
  if (py <= 0)
    py = 1;
  int scrollY = sy * py;

  wxAutoBufferedPaintDC dc(this);
  PrepareDC(dc);
  dc.SetBackground(wxBrush(GetBackgroundColour()));
  dc.Clear();

  if (m_cols <= 0 || m_rowH <= 0)
    return;

  const int clientH = GetClientSize().GetHeight();
  int firstRow = scrollY / m_rowH;
  int lastRow = (scrollY + clientH + m_rowH - 1) / m_rowH;
  if (firstRow < 0)
    firstRow = 0;

  const size_t maxRows =
      m_channels.empty() ? 1 : (m_channels.size() + m_cols - 1) / m_cols;
  if (lastRow > (int)maxRows)
    lastRow = (int)maxRows;

  for (int row = firstRow; row < lastRow; ++row) {
    int y = row * m_rowH;
    for (int col = 0; col < m_cols; ++col) {
      int index = row * m_cols + col;
      if (index >= (int)m_channels.size())
        break;

      auto it = m_tileCache.find((size_t)index);

      if (it != m_tileCache.end() && it->second && it->second->IsOk()) {
        int x = m_gridOffsetX + col * m_colW;
        dc.DrawBitmap(*it->second, x, y, true);
        continue;
      }

      RenderTile((size_t)index);

      it = m_tileCache.find((size_t)index);
      if (it != m_tileCache.end() && it->second && it->second->IsOk()) {
        int x = m_gridOffsetX + col * m_colW;
        dc.DrawBitmap(*it->second, x, y, true);
      }
    }
  }

  if (m_hoverIndex >= 0) {
    DrawCardFrame(dc, m_hoverIndex, wxColour(140, 140, 140), 2);
  }

  if (m_focusIndex >= 0) {
    DrawCardFrame(dc, m_focusIndex, wxColour(140, 140, 140), 3);
  }

  static wxLongLong lastWarm = 0;
  wxLongLong now = wxGetUTCTimeMillis();

  if (now - lastWarm > 50) {
    lastWarm = now;
    int winId = this->GetId();
    CallAfterSafeById(winId, [](wxWindow *w) {
      auto *self = dynamic_cast<CardsBase *>(w);
      if (!self)
        return;
      if (self->m_closing)
        return;
      self->WarmUpTiles();
    });
  }
}
