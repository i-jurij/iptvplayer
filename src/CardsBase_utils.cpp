#include "CardsBase.h"
#include "LogControl.h"
#include "LogoCache.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"
#include "VP_SvgIcon.h"

#include <algorithm>
#include <map>
#include <mutex>

wxString CardsBase::GetTruncatedText(const Channel &ch) {
  std::string key = ch.getName() + "_" + std::to_string(m_logoW);
  auto it = m_textCache.find(key);
  if (it != m_textCache.end())
    return it->second;

  wxString name = wxString::FromUTF8(ch.getName());
  wxCoord tw, th;
  wxClientDC dc(this);
  dc.SetFont(wxFontInfo(FromDIP(12)).Bold());
  dc.GetTextExtent(name, &tw, &th);

  if (tw <= m_logoW) {
    m_textCache[key] = name;
    m_textSizeCache[key] = {tw, th};
    return name;
  }

  wxString ell = "...";
  wxCoord ew, eh;
  dc.GetTextExtent(ell, &ew, &eh);
  wxString tmp = name;
  while (!tmp.IsEmpty()) {
    tmp.RemoveLast();
    dc.GetTextExtent(tmp, &tw, &th);
    if (tw + ew <= m_logoW) {
      tmp += ell;
      break;
    }
  }
  m_textCache[key] = tmp;
  m_textSizeCache[key] = {tw + ew, th};

  return tmp;
}

wxBitmap m_cachedTileBG;
int m_cachedTileBG_DPI = 0;

//argb32
wxBitmap CardsBase::CreateTileBackground(int w, int h) {
  wxBitmap bmp(w, h);
  wxMemoryDC mdc(bmp);

  mdc.SetBrush(wxBrush(LogoCache::GetDefaultCardBgColor()));
  mdc.SetPen(*wxTRANSPARENT_PEN);
  mdc.DrawRectangle(0, 0, w, h);

  mdc.SelectObject(wxNullBitmap);
  return bmp;
}

void CardsBase::RenderTile(size_t index) {
  if (index >= m_channels.size())
    return;

  // Кеш инвалидируется если размер или DPI изменился
  if (!m_cachedTileBG.IsOk() || m_cachedTileBG_DPI != m_currentDPI ||
      m_cachedTileBG.GetWidth() != m_cardW ||
      m_cachedTileBG.GetHeight() != m_cardH) {
    m_cachedTileBG = CreateTileBackground(m_cardW, m_cardH);
    m_cachedTileBG_DPI = m_currentDPI;
  }

  wxBitmap bmp(m_cardW, m_cardH);
  wxMemoryDC mdc(bmp);
  mdc.DrawBitmap(m_cachedTileBG, 0, 0);

  const Channel &ch = m_channels[index];
  int dpi = m_currentDPI;

  const std::string key = MakeLogoCacheKey(
      ch.getPlaylistName(), ch.getName().empty() ? ch.getLogo() : ch.getName(),
      m_logoW, m_logoH, dpi);

  LogoCache::LogoBitmapPtr logoPtr = LogoCache::GetCachedBitmapPtr(key);

  if (!logoPtr) {
    RequestLogo(index);
  }

  bool realLogo = (logoPtr && logoPtr->IsOk());

  const LayoutInfo &L = GetLayoutInfoForDPI(m_currentDPI);

  if (realLogo) {
    mdc.DrawBitmap(*logoPtr, L.logoDx, L.logoDy, true);
  } else {
    wxString text = GetTruncatedText(ch);

    int pad = FromDIP(4);
    int logoAreaLeft = L.logoDx;
    int logoAreaRight = L.starDx - pad;
    int logoAreaW = logoAreaRight - logoAreaLeft;

    wxFont font = wxFontInfo(FromDIP(12)).Bold();
    mdc.SetFont(font);

    wxCoord tw, th;
    mdc.GetTextExtent(text, &tw, &th);

    if (tw > logoAreaW) {
      while (text.Length() > 3) {
        text.RemoveLast();
        wxString tmp = text + "...";
        mdc.GetTextExtent(tmp, &tw, &th);
        if (tw <= logoAreaW) {
          text = tmp;
          break;
        }
      }
    }

    int tx = logoAreaLeft + (logoAreaW - tw) / 2;
    int ty = L.logoDy + (m_logoH - th) / 2;

    // mdc.SetTextForeground(*wxWHITE);
    static auto fg = wxColour(32, 32, 32);
    if (wxSystemSettings::GetAppearance().IsDark()) {
      fg = wxColour(240, 240, 240);
    }
    mdc.SetTextForeground(fg);
    mdc.DrawText(text, tx, ty);
  }

  wxBitmap star = GetStarBitmap(ch);
  if (star.IsOk()) {
    wxBitmap scaled = GetScaledStar(star, L.starSize);
    mdc.DrawBitmap(scaled, L.starDx, L.starDy, true);
  }

  mdc.SelectObject(wxNullBitmap);

  m_tileCacheDPI[dpi][index] = std::make_shared<wxBitmap>(bmp);
  AddTileToLRU(index, m_tileCacheDPI[dpi][index]);
}

int CardsBase::GetCurrentDPI() const {
  wxWindow *win = const_cast<CardsBase *>(this);
  return GetNormDPI(win);
}

void CardsBase::OnDPIChanged(wxDPIChangedEvent &evt) {
  m_currentDPI = GetCurrentDPI();
  LogoCache::OnDPIChanged(m_currentDPI);
  ClearAllCaches(true, true);

  // Удаляем все DPI-слои, кроме текущего
  for (auto it = m_tileCacheDPI.begin(); it != m_tileCacheDPI.end();) {
    if (it->first != m_currentDPI)
      it = m_tileCacheDPI.erase(it);
    else
      ++it;
  }

  // Сбрасываем LRU
  m_tileLRU.clear();
  m_tileLRUCache.clear();

  UpdateLayout();
  InitLRULimits();
  CallAfter([this]() { WarmUpTiles(); });
  Refresh();
  evt.Skip();
}

int CardsBase::GetStarSizeForCardH(int cardH) {
  return std::max(24, (int)(cardH * 0.6));
}

wxRect CardsBase::GetStarRect(int col, int row) const {
  wxRect cardRect = GetCardRect(col, row);
  const LayoutInfo &L = GetLayoutInfoForDPI(m_currentDPI);
  return wxRect(cardRect.x + L.starDx, cardRect.y + L.starDy, L.starSize,
                L.starSize);
}

wxRect CardsBase::GetCardRect(int col, int row) const {
  if (m_cols <= 0 || col < 0 || row < 0)
    return wxRect(0, 0, 0, 0);

  int x = m_gridOffsetX + col * m_colW;
  int y = row * m_rowH;

  return wxRect(x, y, m_cardW, m_cardH);
}

wxRect CardsBase::GetCardRect(size_t index) const {
  if (m_cols <= 0 || index >= m_channels.size())
    return wxRect(0, 0, 0, 0);
  int col = (int)(index % m_cols);
  int row = (int)(index / m_cols);
  return GetCardRect(col, row);
}

void CardsBase::InvalidateCardClientRect(size_t index, bool eraseBackground) {
  if (m_cols <= 0 || index >= m_channels.size())
    return;

  const int cols = m_cols;
  const int col = (int)(index % cols);
  const int row = (int)(index / cols);

  wxRect virt = GetCardRect(col, row);
  wxPoint clientTopLeft;
  CalcScrolledPosition(virt.x, virt.y, &clientTopLeft.x, &clientTopLeft.y);
  wxRect clientRect(clientTopLeft.x, clientTopLeft.y, virt.width, virt.height);
  int thickness = FromDIP(2);
  clientRect.Deflate(thickness / 2, thickness / 2);
  if (!clientRect.IsEmpty()) {
    RefreshRect(clientRect, eraseBackground);
  }
}

void CardsBase::InvalidateCardClientRectByIndex(int cardIndex,
                                                bool eraseBackground) {
  if (cardIndex < 0 || cardIndex >= (int)m_channels.size() || m_cols <= 0)
    return;

  const int cols = m_cols;
  const int row = cardIndex / cols;
  const int col = cardIndex % cols;

  wxRect virt = GetCardRect(col, row);
  wxPoint clientTopLeft;
  CalcScrolledPosition(virt.x, virt.y, &clientTopLeft.x, &clientTopLeft.y);
  wxRect clientRect(clientTopLeft.x, clientTopLeft.y, virt.width, virt.height);
  int thickness = FromDIP(2);
  clientRect.Deflate(thickness / 2, thickness / 2);
  if (!clientRect.IsEmpty()) {
    RefreshRect(clientRect, eraseBackground);
  }
}

CardsBase::LayoutInfo CardsBase::ComputeLayout(int normDPI) const {
  auto cs = GetCardSizeForDPI(normDPI);
  int cardW = GetScaledCardSize(cs.first, normDPI);
  int cardH = GetScaledCardSize(cs.second, normDPI);

  int pad = GetScaledCardSize(4, normDPI);
  int logoGap = GetScaledCardSize(1, normDPI);

  int favZoneSize = cardH;
  int logoZoneLeft = pad;
  int logoZoneRight = cardW - pad - favZoneSize;
  int logoZoneW = std::max(1, logoZoneRight - logoZoneLeft - logoGap);
  int logoH = std::max(1, cardH - 2 * pad);

  int starSize = GetStarSizeForCardH(cardH);

  int logoX = logoZoneLeft;
  int logoY = pad + (cardH - 2 * pad - logoH) / 2;
  int starX = logoZoneRight + logoGap + (favZoneSize - starSize) / 2;
  int starY = (cardH - starSize) / 2;

  LayoutInfo L;
  L.cardW = cardW;
  L.cardH = cardH;
  L.pad = pad;
  L.logoGap = logoGap;
  L.starSize = starSize;
  L.logoZoneLeft = logoZoneLeft;
  L.logoZoneRight = logoZoneRight;
  L.logoZoneW = logoZoneW;
  L.favZoneSize = favZoneSize;
  L.logoW = logoZoneW;
  L.logoH = logoH;
  L.logoDx = logoX;
  L.logoDy = logoY;
  L.starDx = starX;
  L.starDy = starY;
  return L;
}

CardsBase::LayoutInfo CardsBase::GetLayoutInfoForDPI(int rawDPI) const {
  int normDPI = NormalizeDpi(rawDPI);
  std::lock_guard<std::mutex> lock(m_layoutCacheMutex);
  auto it = m_layoutCache.find(normDPI);
  if (it != m_layoutCache.end())
    return it->second;
  m_layoutCache[normDPI] = ComputeLayout(normDPI);
  return m_layoutCache[normDPI];
}

bool CardsBase::IsBitmapNonEmpty(const wxBitmap &bmp) {
  if (!bmp.IsOk() || bmp.GetWidth() < 8 || bmp.GetHeight() < 8)
    return false;
  wxImage img = bmp.ConvertToImage();
  if (!img.HasAlpha())
    return true;
  const unsigned char *alpha = img.GetAlpha();
  int size = img.GetWidth() * img.GetHeight();
  for (int i = 0; i < size; ++i)
    if (alpha[i] > 0)
      return true;
  return false;
}

wxPoint CardsBase::GetCardClientCenter(int col, int row) const {
  wxRect virt = GetCardRect(col, row);
  wxPoint clientTopLeft;
  CalcScrolledPosition(virt.x, virt.y, &clientTopLeft.x, &clientTopLeft.y);
  int cx = clientTopLeft.x + virt.width / 2;
  int cy = clientTopLeft.y + virt.height / 2;
  return wxPoint(cx, cy);
}

void CardsBase::EnsureRowVisible(int row) {
  if (m_cols <= 0 || m_rowH <= 0 || row < 0)
    return;

  int sx = 0, sy = 0;
  GetViewStart(&sx, &sy);

  int px = 1, py = 1;
  GetScrollPixelsPerUnit(&px, &py);
  if (px <= 0)
    px = 1;
  if (py <= 0)
    py = 1;

  int viewTop = sy * py;
  int clientH = GetClientSize().GetHeight();
  int viewBottom = viewTop + clientH;

  int rowY = row * m_rowH;
  int rowBottom = rowY + m_rowH;

  if (!(rowBottom < viewTop || rowY > viewBottom))
    return;

  int targetPixelTop;
  if (rowY > viewBottom) {
    targetPixelTop = rowY - (clientH - m_rowH);
    if (targetPixelTop < 0)
      targetPixelTop = 0;
  } else {
    targetPixelTop = rowY;
  }

  int targetUnit = targetPixelTop / py;

  int maxUnit = GetScrollRange(wxVERTICAL);
  if (targetUnit < 0)
    targetUnit = 0;
  if (targetUnit > maxUnit)
    targetUnit = maxUnit;

  Scroll(-1, targetUnit);

  int col = 0;
  if (m_hoverIndex >= 0) {
    int hoverRow = m_hoverIndex / m_cols;
    if (hoverRow == row)
      col = m_hoverIndex % m_cols;
  }

  wxPoint center = GetCardClientCenter(col, row);
  m_lastMouseClientPos = center;
  UpdateHoverAtPoint(center);

  Refresh();
}

void CardsBase::PauseLogoLoading() {
  m_loadingPaused.store(true, std::memory_order_relaxed);
  LOG_DEBUG("CardsBase::PauseLogoLoading - paused");
}

void CardsBase::ResumeLogoLoading() {
  if (wxWindow *top = wxGetTopLevelParent(this)) {
    if (auto *mf = dynamic_cast<MainFrame *>(top)) {
      if (!mf->AreLogosEnabled()) {
        LOG_DEBUG(
            "CardsBase::ResumeLogoLoading - logos disabled, skipping resume");
        return;
      }
    }
  }

  bool expected = true;
  if (!m_loadingPaused.compare_exchange_strong(expected, false)) {
    return;
  }

  int winId = this->GetId();
  CallAfterSafeById(winId, [](wxWindow *w) {
    auto *self = dynamic_cast<CardsBase *>(w);
    if (!self)
      return;
    if (self->m_closing)
      return;
    if (wxWindow *top = wxGetTopLevelParent(self)) {
      if (auto *mf = dynamic_cast<MainFrame *>(top)) {
        if (!mf->AreLogosEnabled())
          return;
      }
    }
    self->WarmUpFavorites();
    self->WarmUpTiles();
  });

  LOG_DEBUG("CardsBase::ResumeLogoLoading - resumed");
}

void CardsBase::InvalidateAll() {
  wxTheApp->CallAfter([this]() { Refresh(); });
}

// for debug only ----------------------------------------
void CardsBase::DebugTileMemory() {
  size_t totalTiles = 0;
  size_t totalBytes = 0;

  for (auto &dpiLayer : m_tileCacheDPI) {
    for (auto &kv : dpiLayer.second) {
      auto bmp = kv.second;
      if (bmp && bmp->IsOk()) {
        totalTiles++;
        totalBytes += (size_t)bmp->GetWidth() * bmp->GetHeight() * 4;
      }
    }
  }

  //LOG_DEBUG("CardsBase Tiles: count=%zu, approx=%zu KB", totalTiles,
    //        totalBytes / 1024);
}

void CardsBase::DebugInternalMemory() {
  size_t textCount = m_textCache.size();
  size_t textSizeCount = m_textSizeCache.size();
  size_t layoutCount = m_layoutCache.size();

  size_t tileCount = 0;
  for (auto &dpiLayer : m_tileCacheDPI)
    tileCount += dpiLayer.second.size();

  // --- оценка памяти textCache ---
  size_t textBytes = 0;
  for (const auto &kv : m_textCache) {
    textBytes += kv.first.size();                     // ключ std::string
    textBytes += kv.second.length() * sizeof(wxChar); // значение wxString
  }

  
  // --- грубая оценка textSizeCache ---
  size_t textSizeBytes = 0;
  textSizeBytes +=
      textSizeCount * (sizeof(std::string) + sizeof(std::pair<int, int>));

  LOG_DEBUG("CardsBase Internal: text=%zu textSize=%zu layout=%zu tiles=%zu "
            "textBytes≈%zu B (≈%zu KB) textSizeBytes≈%zu B (≈%zu KB)",
            textCount, textSizeCount, layoutCount, tileCount, textBytes,
            textBytes / 1024, textSizeBytes, textSizeBytes / 1024);
  LOG_DEBUG("m_logoW=%d m_logoH=%d", m_logoW, m_logoH);
}

// --------------------------------------------