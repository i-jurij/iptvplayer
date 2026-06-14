#include "CardsBase.h"
#include "LogoCache.h"
#include "Profiler.h"
#include "Utils.h"
#include "VP_SvgIcon.h"

#include <algorithm>

void CardsBase::UpdateLayout() {
  const int oldCols = m_cols;
  const int oldHover = m_hoverIndex;

  int clientW = GetClientSize().GetWidth();
  if (clientW <= 0) {
    clientW = 800;
  }

  int dpi = m_currentDPI;
  auto L = GetLayoutInfoForDPI(dpi);

  m_cardW = L.cardW;
  m_cardH = L.cardH;
  m_pad = L.pad;
  m_logoGap = L.logoGap;
  m_starW = L.starSize;
  m_logoW = L.logoW;
  m_logoH = L.logoH;

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

wxBitmap CardsBase::GetScaledStar(const wxBitmap &star, int size) {
  if (!star.IsOk())
    return wxBitmap();

  wxImage img = star.ConvertToImage();
  img.Rescale(size, size, wxIMAGE_QUALITY_HIGH);
  return wxBitmap(img);
}

void CardsBase::DrawCardBase(wxDC &dc, size_t index, const wxRect &rect,
                             bool /*hovered*/) {
  const Channel &ch = m_channels[index];
  const int pad = m_pad;
  const LayoutInfo &L = GetLayoutInfoForDPI(m_currentDPI);

  dc.SetBrush(wxBrush(LogoCache::GetDefaultCardBgColor()));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(rect);

  LogoCache::LogoBitmapPtr bmpPtr = nullptr;
  const std::string &url = ch.getLogo();
  int dpi = GetNormDPI(this);

  if (!url.empty()) {
    const std::string key = MakeLogoCacheKey(ch.getPlaylistName(), ch.getName(),
                                             m_logoW, m_logoH, dpi);

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

    // dc.SetTextForeground(*wxWHITE);
    static auto fg = wxColour(32, 32, 32);
    if (wxSystemSettings::GetAppearance().IsDark()) {
      fg = wxColour(240, 240, 240);
    }
    dc.SetTextForeground(fg);
    dc.SetFont(font);
    dc.DrawText(text, tx, ty);
  }

  if (hasLogo) {
    dc.DrawBitmap(*bmpPtr, rect.x + L.logoDx, rect.y + L.logoDy, true);
  }

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

  int dpi = m_currentDPI;

  // tile‑only: рисуем по тайлам
  for (int row = firstRow; row < lastRow; ++row) {
    int y = row * m_rowH;
    for (int col = 0; col < m_cols; ++col) {
      int index = row * m_cols + col;
      if (index >= (int)m_channels.size())
        break;

      auto it = m_tileCacheDPI[dpi].find(index);

      // --- REUSE: если тайл уже есть, просто рисуем ---
      if (it != m_tileCacheDPI[dpi].end() && it->second && it->second->IsOk()) {
        int x = m_gridOffsetX + col * m_colW;
        dc.DrawBitmap(*it->second, x, y, true);
        continue;
      }

      // --- FALLBACK: тайла нет → создаём ---
      RenderTile(index);

      it = m_tileCacheDPI[dpi].find(index);
      if (it != m_tileCacheDPI[dpi].end() && it->second && it->second->IsOk()) {
        int x = m_gridOffsetX + col * m_colW;
        dc.DrawBitmap(*it->second, x, y, true);
      }
    }
  }

  if (m_focusIndex >= 0 && m_focusIndex < (int)m_channels.size()) {
    int row = m_focusIndex / m_cols;
    int col = m_focusIndex % m_cols;

    int x = m_gridOffsetX + col * m_colW;
    int y = row * m_rowH;

    wxColour borderColor(140, 140, 140);
    int thickness = FromDIP(3);

    wxPen pen(borderColor, thickness);
    pen.SetCap(wxCAP_BUTT);
    pen.SetJoin(wxJOIN_MITER);
    dc.SetPen(pen);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int half = thickness / 2;
    wxRect r(x + half, y + half, m_cardW - thickness, m_cardH - thickness);
    dc.DrawRectangle(r.x, r.y, r.width, r.height);
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
