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
double m_cachedTileBG_CS = 0.0;

// argb32
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

  double cs = m_contentScale;
  int physCardW = std::max(1, (int)std::round(m_cardW * cs));
  int physCardH = std::max(1, (int)std::round(m_cardH * cs));
  int physLogoW = std::max(1, (int)std::round(m_logoW * cs));
  int physLogoH = std::max(1, (int)std::round(m_logoH * cs));
  int scale100 = std::max(100, (int)std::round(cs * 100.0));

  // Background-тайл нужен только чтобы не пересоздавать фон.
  if (!m_cachedTileBG.IsOk() || m_cachedTileBG_CS != cs ||
      m_cachedTileBG.GetWidth() != physCardW ||
      m_cachedTileBG.GetHeight() != physCardH) {
    m_cachedTileBG = CreateTileBackground(physCardW, physCardH);
    m_cachedTileBG.SetScaleFactor(cs);
    m_cachedTileBG_CS = cs;
  }

  wxBitmap bmp(physCardW, physCardH);
  bmp.SetScaleFactor(cs);
  wxMemoryDC mdc(bmp);

  // DC работает в DIP-координатах (bitmap с scaleFactor=cs).
  // Вложенные bitmap'ы тоже должны иметь SetScaleFactor(cs),
  // тогда draw-операции корректны в DIP.
  mdc.DrawBitmap(m_cachedTileBG, 0, 0);

  const Channel &ch = m_channels[index];

  const std::string key = LogoCache::MakeScaledKey(
      ch.getPlaylistName(), ch.getName().empty() ? ch.getLogo() : ch.getName(),
      physLogoW, physLogoH, scale100);

  LogoCache::LogoBitmapPtr logoPtr = LogoCache::GetCachedBitmapPtr(key);
  if (!logoPtr)
    RequestLogo(index);

  bool realLogo = (logoPtr && logoPtr->IsOk());

  const LayoutInfo &L = m_layout;

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

    static auto fg = wxColour(32, 32, 32);
    if (wxSystemSettings::GetAppearance().IsDark())
      fg = wxColour(240, 240, 240);
    mdc.SetTextForeground(fg);
    mdc.DrawText(text, tx, ty);
  }

  wxBitmap star = GetStarBitmap(ch);
  if (star.IsOk()) {
    wxBitmap scaled = GetScaledStar(star, L.starSize);
    mdc.DrawBitmap(scaled, L.starDx, L.starDy, true);
  }

  mdc.SelectObject(wxNullBitmap);

  m_tileCache[index] = std::make_shared<wxBitmap>(std::move(bmp));
  AddTileToLRU(index, m_tileCache[index]);
}

void CardsBase::OnEnvironmentChanged(wxEvent &evt) {
  // Оба события (DPI_CHANGED, DISPLAY_CHANGED) могут прийти в одном цикле
  // обработки сообщений. Коалесцируем: только первый запускает пересборку,
  // остальные игнорируются, пока запланированная не выполнится.
  evt.Skip();

  bool expected = false;
  if (!m_layoutRebuildScheduled.compare_exchange_strong(expected, true))
    return;

  CallAfter([this]() {
    m_layoutRebuildScheduled.store(false);
    if (m_closing)
      return;

    double oldCS = m_contentScale;

    UpdateLayout();
    InitLRULimits();

    if (std::abs(m_contentScale - oldCS) > 0.001)
      LogoCache::ClearScaled();

    WarmUpTiles();
    Refresh();
  });
}

int CardsBase::GetStarSizeForCardH(int cardH) {
  return std::max(24, (int)(cardH * 0.6));
}

wxRect CardsBase::GetStarRect(int col, int row) const {
  wxRect cardRect = GetCardRect(col, row);
  return wxRect(cardRect.x + m_layout.starDx, cardRect.y + m_layout.starDy,
                m_layout.starSize, m_layout.starSize);
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

bool CardsBase::RemoveChannel(const std::string &name,
                              const std::string &playlistName) {
  // Ищем канал
  auto it = std::find_if(
      m_channels.begin(), m_channels.end(), [&](const Channel &ch) {
        return ch.getName() == name && ch.getPlaylistName() == playlistName;
      });

  if (it == m_channels.end())
    return false;

  size_t removedIndex = std::distance(m_channels.begin(), it);

  // Удаляем канал из вектора
  m_channels.erase(it);

  // Перестраиваем кэш тайлов с учётом сдвига индексов
  {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    // 1) Перестраиваем m_tileCache
    std::unordered_map<size_t, LogoCache::LogoBitmapPtr> newCache;
    for (auto &kv : m_tileCache) {
      size_t oldIdx = kv.first;
      if (oldIdx == removedIndex)
        continue;
      size_t newIdx = (oldIdx > removedIndex) ? oldIdx - 1 : oldIdx;
      newCache[newIdx] = kv.second;
    }
    m_tileCache = std::move(newCache);

    // 2) Перестраиваем LRU
    std::list<size_t> newLRU;
    for (size_t oldIdx : m_tileLRU) {
      if (oldIdx == removedIndex)
        continue;
      size_t newIdx = (oldIdx > removedIndex) ? oldIdx - 1 : oldIdx;
      newLRU.push_back(newIdx);
    }
    m_tileLRU = std::move(newLRU);

    // 3) Перестраиваем m_tileLRUCache (аналогично)
    std::unordered_map<size_t, LogoCache::LogoBitmapPtr> newLRUCache;
    for (auto &kv : m_tileLRUCache) {
      size_t oldIdx = kv.first;
      if (oldIdx == removedIndex)
        continue;
      size_t newIdx = (oldIdx > removedIndex) ? oldIdx - 1 : oldIdx;
      newLRUCache[newIdx] = kv.second;
    }
    m_tileLRUCache = std::move(newLRUCache);

    // 4) m_scaledKeyToIndices можно очистить — он перестроится при WarmUpTiles
    m_scaledKeyToIndices.clear();
  }

  // Обновляем макет (пересчитываем количество колонок и размеры)
  UpdateLayout();
  InitLRULimits();

  // Перерисовываем всю сетку (без сброса скролла, т.к. виртуальный размер
  // изменился)
  Refresh();

  LOG_DEBUG("CardsBase: Removed channel '%s' from playlist '%s' (index %zu)",
            name.c_str(), playlistName.c_str(), removedIndex);
  return true;
}
