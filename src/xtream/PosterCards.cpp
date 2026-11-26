#include "xtream/PosterCards.h"

#include "BookmarkIcon.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "StarIcon.h"
#include "Utils_Platform.h"
#include "Utils_UI.h"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>

#include <algorithm>
#include <cmath>

namespace {

// True if posters are allowed by current MainFrame settings.
bool PostersAllowedByOwner(const wxWindow *w) {
  wxWindow *tlw = wxGetTopLevelParent(const_cast<wxWindow *>(w));
  const MainFrame *mf = dynamic_cast<const MainFrame *>(tlw);
  return mf ? mf->AreLogosEnabled() : true;
}

struct PosterLayoutDips {
  int posterW = 0;
  int posterH = 0;
  int titleH = 0;
  int titleFontPt = 10;
  int iconSizeDip = 24;
  int iconMarginDip = 6;
  int iconGapDip = 6;
  int gapX = 14;
  int gapY = 14;
};

PosterLayoutDips ComputePosterLayout(int displayW_Dip) {
  PosterLayoutDips L;
  if (displayW_Dip <= 1920) {
    L.posterW = 220;
    L.posterH = 330;
    L.titleFontPt = 10;
    L.iconSizeDip = 36;    // было 24 (~2x, как starSize в каналах)
    L.iconMarginDip = 8;   // было 6
    L.iconGapDip = 8;      // было 6
  } else if (displayW_Dip <= 2560) {
    L.posterW = 260;
    L.posterH = 390;
    L.titleFontPt = 11;
    L.iconSizeDip = 42;    // было 28
    L.iconMarginDip = 10;  // было 7
    L.iconGapDip = 10;     // было 7
  } else {
    L.posterW = 300;
    L.posterH = 450;
    L.titleFontPt = 12;
    L.iconSizeDip = 48;    // было 32
    L.iconMarginDip = 12;  // было 8
    L.iconGapDip = 12;     // было 8
  }
  L.titleH = 40;
  return L;
}

Channel ConvertToChannel(const PosterItem &item) {
  Channel ch;
  ch.setName(item.name);
  ch.setLogo(item.posterUrl);
  return ch;
}

} // namespace

PosterCards::PosterCards(wxWindow *parent) : CardsBase(parent) {
  Bind(wxEVT_CONTEXT_MENU, &PosterCards::OnContextMenu, this);
  Bind(wxEVT_KEY_DOWN, &PosterCards::OnKeyDownIntercept, this);
  Bind(wxEVT_LEFT_DCLICK, &PosterCards::OnLeftDClick, this);
  m_favFilled = CreateFilledStarIcon(wxSize(128, 128));
  m_watchFilled = CreateFilledBookmarkIcon(wxSize(128, 128));
}

void PosterCards::SetItems(const std::vector<PosterItem> &items) {
  std::vector<PosterItem> copy = items;
  SetItems(std::move(copy));
}

void PosterCards::SetItems(std::vector<PosterItem> &&items) {
  m_items = std::move(items);

  // Sync with MainFrame setting: playlist change/Reset must not
  // "revive" posters if their display is disabled.
  m_loadingPaused.store(!PostersAllowedByOwner(this),
                        std::memory_order_relaxed);

  m_requestedPosters.clear();
  RebuildMediaKeyIndex();

  m_channels.clear();
  m_channels.reserve(m_items.size());
  for (const auto &item : m_items)
    m_channels.push_back(ConvertToChannel(item));

  ClearAllCaches(true, true);

  m_loadMoreInFlight = false;

  const int winId = this->GetId();
  CallAfterSafeById(winId, [winId](wxWindow *w) {
    auto *self = dynamic_cast<PosterCards *>(w);
    if (!self)
      return;
    self->UpdateLayout();
    self->InitLRULimits();
    self->Refresh();
  });
}

void PosterCards::AppendItems(const std::vector<PosterItem> &items) {
  if (items.empty())
    return;

  m_items.reserve(m_items.size() + items.size());
  m_channels.reserve(m_channels.size() + items.size());
  for (const auto &item : items) {
    m_items.push_back(item);
    m_channels.push_back(ConvertToChannel(item));
  }
  RebuildMediaKeyIndex();

  m_loadMoreInFlight = false;

  const int winId = this->GetId();
  CallAfterSafeById(winId, [winId](wxWindow *w) {
    auto *self = dynamic_cast<PosterCards *>(w);
    if (!self || self->IsBeingDeleted())
      return;
    self->UpdateLayout();
    self->Refresh();
  });
}

void PosterCards::SortItems(SortComparator comp) {
  if (!comp || m_items.size() < 2)
    return;

  std::sort(m_items.begin(), m_items.end(), comp);

  RebuildMediaKeyIndex();

  m_channels.clear();
  m_channels.reserve(m_items.size());
  for (const auto &item : m_items)
    m_channels.push_back(ConvertToChannel(item));

  const int winId = this->GetId();
  CallAfterSafeById(winId, [winId](wxWindow *w) {
    auto *self = dynamic_cast<PosterCards *>(w);
    if (!self || self->IsBeingDeleted())
      return;
    self->UpdateLayout();
    self->Refresh();
  });
}

const std::string &PosterCards::GetMediaKey(size_t index) const {
  static const std::string empty;
  if (index >= m_items.size())
    return empty;
  return m_items[index].mediaKey;
}

void PosterCards::RebuildMediaKeyIndex() {
  m_mediaKeyToIndex.clear();
  m_mediaKeyToIndex.reserve(m_items.size());
  for (size_t i = 0; i < m_items.size(); ++i) {
    if (!m_items[i].mediaKey.empty())
      m_mediaKeyToIndex[m_items[i].mediaKey] = i;
  }
}

void PosterCards::RefreshPosterLayout() {
  UpdateLayout();
  InitLRULimits();
  Refresh();
}

void PosterCards::UpdateLayout() {
  int clientW = GetClientSize().GetWidth();
  if (clientW <= 0)
    clientW = 800;

  int displayW_Dip = clientW;
#if defined(__WXMSW__)
  {
    const double dpiScale = GetDPIScaleFactor();
    if (dpiScale > 1.0)
      displayW_Dip = static_cast<int>(std::round(clientW / dpiScale));
  }
#endif
  if (displayW_Dip <= 0)
    displayW_Dip = 1920;

  const PosterLayoutDips L = ComputePosterLayout(displayW_Dip);

  auto dip = [this](int v) { return wxWindow::FromDIP(v, this); };

  m_layout.cardW = L.posterW;
  m_layout.cardH = L.posterH + L.titleH;
  m_layout.pad = 0;
  m_layout.logoGap = 0;
  m_layout.starSize = 0;
  m_layout.logoZoneLeft = 0;
  m_layout.logoZoneRight = L.posterW;
  m_layout.logoZoneW = L.posterW;
  m_layout.favZoneSize = 0;
  m_layout.logoW = L.posterW;
  m_layout.logoH = L.posterH;
  m_layout.logoDx = 0;
  m_layout.logoDy = 0;
  m_layout.starDx = L.posterW;
  m_layout.starDy = 0;
  m_layout.titleH = L.titleH;
  m_layout.titleDx = 0;
  m_layout.titleDy = L.posterH;
  m_layout.titleFontPt = L.titleFontPt;

  m_cardW = dip(L.posterW);
  m_cardH = dip(L.posterH + L.titleH);
  m_pad = 0;
  m_logoGap = 0;
  m_starW = 0;
  m_logoW = dip(L.posterW);
  m_logoH = dip(L.posterH);

  m_gapX = dip(L.gapX);
  m_gapY = dip(L.gapY);

  m_iconSizeDip = L.iconSizeDip;
  m_iconMarginDip = L.iconMarginDip;
  m_iconGapDip = L.iconGapDip;

  m_colW = m_cardW + m_gapX;
  m_rowH = m_cardH + m_gapY;

  m_cols = std::max(1, clientW / std::max(1, m_colW));
  // Cap by total (DB) count when known; otherwise by loaded count.
  const int cap = (m_totalCount > 0)
                      ? static_cast<int>(m_totalCount)
                      : static_cast<int>(m_channels.size());
  m_cols = std::min(m_cols, std::max(1, cap));

  const int totalGridWidth =
      (m_cols > 0) ? (m_cols * m_cardW + (m_cols - 1) * m_gapX) : 0;
  m_gridOffsetX = std::max(0, (clientW - totalGridWidth) / 2);

  const int totalW = std::max(clientW, totalGridWidth);
  const size_t count = (m_totalCount > 0) ? m_totalCount : m_channels.size();
  const size_t rows =
      count == 0 ? 1
                 : (count + static_cast<size_t>(m_cols) - 1) /
                       static_cast<size_t>(m_cols);
  const int totalH = static_cast<int>(rows) * m_rowH;
  SetVirtualSize(totalW, totalH);

  m_contentScale = GetContentScale(this);

  // One shared ghost bitmap for not-yet-loaded indexes. All ghost slots
  // reference the same shared_ptr — memory cost is one bitmap, not N.
  {
    const double cs = m_contentScale;
    const int pw = std::max(1, static_cast<int>(std::round(m_cardW * cs)));
    const int ph = std::max(1, static_cast<int>(std::round(m_cardH * cs)));
    wxBitmap bmp(pw, ph);
    bmp.SetScaleFactor(cs);
    wxMemoryDC mdc(bmp);
    const bool dark = ShouldUseDarkColors();
    const wxColour bg = dark ? wxColour(50, 53, 55) : wxColour(210, 210, 210);
    const wxColour border =
        dark ? wxColour(80, 83, 85) : wxColour(170, 170, 170);
    mdc.SetPen(wxPen(border));
    mdc.SetBrush(wxBrush(bg));
    mdc.DrawRectangle(0, 0, m_layout.cardW,
                      m_layout.logoH + m_layout.titleH);
    mdc.SelectObject(wxNullBitmap);
    m_ghostBmp = std::make_shared<wxBitmap>(std::move(bmp));
  }

  ClearAllCaches(false, true);
  m_requestedPosters.clear();

  m_iconsScale = 0.0;
  m_favFilledScaled = wxBitmap();
  m_watchFilledScaled = wxBitmap();
  m_favOutlineScaled = wxBitmap();
  m_watchOutlineScaled = wxBitmap();

  Refresh(false);

  if (m_hoverIndex >= static_cast<int>(m_channels.size())) {
    const int old = m_hoverIndex;
    m_hoverIndex = -1;
    if (old >= 0)
      InvalidateCardClientRectByIndex(old);
  }
}

void PosterCards::RenderTile(size_t index) {
  const double cs = m_contentScale;
  const int physCardW = std::max(1, static_cast<int>(std::round(m_cardW * cs)));
  const int physCardH = std::max(1, static_cast<int>(std::round(m_cardH * cs)));

  // Ghost placeholder for items beyond the currently loaded range.
  // Shared m_ghostBmp — one bitmap reused for all unloaded slots. Ghost
  // is only rendered for visible indexes (paint loop), so memory stays at
  // a single bitmap regardless of total count.
  if (index >= m_channels.size()) {
    if (m_ghostBmp && m_ghostBmp->IsOk()) {
      m_tileCache.Put(index, m_ghostBmp);
    }
    return;
  }

  if (index >= m_items.size())
    return;

  auto dip = [this](int v) { return wxWindow::FromDIP(v, this); };
  wxBitmap bmp(physCardW, physCardH);
  bmp.SetScaleFactor(cs);
  wxMemoryDC mdc(bmp);

  const PosterItem &item = m_items[index];
  const int posterW = m_layout.cardW;
  const int posterH = m_layout.logoH;
  const int titleH = m_layout.titleH;

  const bool dark = ShouldUseDarkColors();

  wxColour posterBg = dark ? wxColour(60, 63, 65) : wxColour(200, 200, 200);
  mdc.SetPen(*wxTRANSPARENT_PEN);
  mdc.SetBrush(wxBrush(posterBg));
  mdc.DrawRectangle(0, 0, posterW, posterH);

  bool drewPoster = false;
  if (!m_loadingPaused.load(std::memory_order_relaxed) &&
      !item.posterUrl.empty() && !m_playlistName.empty()) {
    const int physW = std::max(1, static_cast<int>(std::round(m_cardW * cs)));
    const int physH = std::max(1, static_cast<int>(std::round(m_logoH * cs)));
    const int scale100 =
        std::max(100, static_cast<int>(std::round(cs * 100.0)));
    const std::string mediaKey = item.mediaKey;
    const std::string key = LogoCache::MakePosterKey(
        m_playlistName, mediaKey, item.posterUrl, physW, physH, scale100);

    auto cached = LogoCache::GetCachedPosterPtr(key);
    if (cached && cached->IsOk()) {
      const wxSize ls = cached->GetLogicalSize();
      const int dx = (posterW - ls.x) / 2;
      const int dy = (posterH - ls.y) / 2;
      mdc.DrawBitmap(*cached, dx, dy, true);
      drewPoster = true;
    } else if (m_requestedPosters.insert(key).second) {
      LogoCache::GetPosterAsync(
          m_playlistName, mediaKey, item.posterUrl, physW, physH, posterBg,
          [this, key, mediaKey](LogoCache::LogoBitmapPtr bmp) {
            if (this->IsBeingDeleted())
              return;
            m_requestedPosters.erase(key);
            if (!bmp || !bmp->IsOk())
              return;
            // SortItems/SetItems могли изменить порядок m_items между
            // fetch и callback. Reverse-index (O(1)) — актуальный индекс
            // по mediaKey.
            auto it = m_mediaKeyToIndex.find(mediaKey);
            if (it == m_mediaKeyToIndex.end())
              return;
            size_t idx = it->second;
            RenderTile(idx);
            MarkCardDirty(static_cast<int>(idx));
          },
          cs);
    }
  }

  if (!drewPoster) {
    const wxString firstChar = wxString::FromUTF8(item.name).Left(1).Upper();
    if (!firstChar.IsEmpty()) {
      wxFont f = wxFontInfo(dip(64)).Bold();
      mdc.SetFont(f);
      mdc.SetTextForeground(dark ? wxColour(240, 240, 240)
                                 : wxColour(80, 80, 80));
      wxCoord tw = 0, th = 0;
      mdc.GetTextExtent(firstChar, &tw, &th);
      mdc.DrawText(firstChar, (posterW - tw) / 2, (posterH - th) / 2);
    }
  }

  EnsureScaledIcons();

  const auto [favZone, watchZone] = IconZonesDip(posterW);
  if (item.isFavorite) {
    if (m_favFilledScaled.IsOk())
      mdc.DrawBitmap(m_favFilledScaled, favZone.x, favZone.y, true);
  } else {
    if (m_favOutlineScaled.IsOk())
      mdc.DrawBitmap(m_favOutlineScaled, favZone.x, favZone.y, true);
  }
  if (item.isWatchlist) {
    if (m_watchFilledScaled.IsOk())
      mdc.DrawBitmap(m_watchFilledScaled, watchZone.x, watchZone.y, true);
  } else {
    if (m_watchOutlineScaled.IsOk())
      mdc.DrawBitmap(m_watchOutlineScaled, watchZone.x, watchZone.y, true);
  }

  const int titleY = posterH;
  wxColour barBg = dark ? wxColour(40, 43, 45) : wxColour(170, 170, 170);
  mdc.SetPen(*wxTRANSPARENT_PEN);
  mdc.SetBrush(wxBrush(barBg));
  mdc.DrawRectangle(0, titleY, posterW, titleH);

  wxString title = wxString::FromUTF8(item.name);
  mdc.SetFont(wxFontInfo(dip(m_layout.titleFontPt)));

  wxCoord tw = 0, th = 0;
  mdc.GetTextExtent(title, &tw, &th);
  const int availW = posterW - 2 * dip(4);
  if (tw > availW) {
    const wxString ell = "...";
    wxCoord ew = 0;
    mdc.GetTextExtent(ell, &ew, nullptr);
    while (!title.IsEmpty()) {
      title.RemoveLast();
      mdc.GetTextExtent(title + ell, &tw, nullptr);
      if (tw <= availW) {
        title += ell;
        break;
      }
    }
  }

  mdc.SetTextForeground(dark ? wxColour(230, 230, 230) : wxColour(20, 20, 20));
  mdc.GetTextExtent(title, &tw, &th);
  mdc.DrawText(title, (posterW - tw) / 2, titleY + (titleH - th) / 2);

  mdc.SelectObject(wxNullBitmap);

  m_tileCache.Put(index, std::make_shared<wxBitmap>(std::move(bmp)));
}

wxBitmap PosterCards::GetStarBitmap(const Channel & /*ch*/) const {
  return wxBitmap();
}

void PosterCards::OnCardClick(size_t index, bool /*fav*/,
                              const wxRect & /*rect*/) {
  if (index >= m_items.size())
    return;
  if (m_onPosterSelect)
    m_onPosterSelect(m_items[index].mediaKey, index);
}

void PosterCards::OnCardActivate(size_t index) {
  if (index >= m_items.size())
    return;
  // Movies: play via m_onActivate. Series: no m_onActivate set → fall back
  // to m_onPosterSelect (drill-in to seasons), same as a single click.
  if (m_onActivate) {
    m_onActivate(m_items[index].mediaKey, index);
    return;
  }
  if (m_onPosterSelect)
    m_onPosterSelect(m_items[index].mediaKey, index);
}

void PosterCards::OnKeyDownIntercept(wxKeyEvent &evt) {
  const int key = evt.GetKeyCode();
  if (key != WXK_RETURN && key != WXK_NUMPAD_ENTER) {
    evt.Skip(); // let CardsBase::OnKeyDown handle navigation etc.
    return;
  }
  if (!IsShownOnScreen() || m_channels.empty()) {
    evt.Skip();
    return;
  }
  if (m_focusIndex < 0)
    m_focusIndex = 0;
  if (m_focusIndex >= static_cast<int>(m_channels.size())) {
    evt.Skip();
    return;
  }
  OnCardActivate(static_cast<size_t>(m_focusIndex));
  // Intentionally no Skip(): prevent base-class PlayChannel with empty URL.
}

void PosterCards::OnLeftDClick(wxMouseEvent &evt) {
  bool fav = false;
  const int idx = HitTestIndex(evt.GetPosition(), fav, nullptr);
  if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
    evt.Skip();
    return;
  }

  // Ignore dbl-click on fav/watch icon overlays (filled or outline):
  // the 1st LEFT_DOWN already toggled (or toggles on next press), and
  // activating play here would be surprising. Same zone math as OnMouseDown.
  const wxPoint pos = evt.GetPosition();
  const auto [favZone, watchZone] = IconZones(CardRectForIndex(idx));
  if (favZone.Contains(pos) || watchZone.Contains(pos)) {
    evt.Skip();
    return;
  }

  OnCardActivate(static_cast<size_t>(idx));
  evt.Skip();
}

void PosterCards::OnContextMenu(wxContextMenuEvent &evt) {
  if (!m_onContext)
    return;

  wxPoint pos = evt.GetPosition();
  if (pos == wxDefaultPosition)
    pos = wxGetMousePosition();

  const wxPoint clientPos = ScreenToClient(pos);

  bool fav = false;
  wxRect rect;
  const int idx = HitTestIndex(clientPos, fav, &rect);
  if (idx < 0 || idx >= static_cast<int>(m_items.size()))
    return;

  m_onContext(m_items[idx].mediaKey, pos);
}

void PosterCards::SetEmptyMessage(const wxString &msg) {
  if (m_emptyMessage == msg)
    return;
  m_emptyMessage = msg;
  Refresh();
}

void PosterCards::OnPaint(wxPaintEvent &evt) {
  if (!m_channels.empty() || m_emptyMessage.empty()) {
    CardsBase::OnPaint(evt);
    return;
  }

  wxAutoBufferedPaintDC dc(this);
  PrepareDC(dc);
  dc.SetBackground(wxBrush(GetBackgroundColour()));
  dc.Clear();

  const wxSize sz = GetClientSize();
  if (sz.x <= 0 || sz.y <= 0)
    return;

  wxFont f = dc.GetFont();
  f.SetPointSize(f.GetPointSize() + 4);
  dc.SetFont(f);
  dc.SetTextForeground(ShouldUseDarkColors() ? wxColour(160, 160, 160)
                                             : wxColour(120, 120, 120));

  wxCoord tw = 0, th = 0;
  dc.GetTextExtent(m_emptyMessage, &tw, &th);
  dc.DrawText(m_emptyMessage, (sz.x - tw) / 2, (sz.y - th) / 2);
}

void PosterCards::EnsureScaledIcons() {
  const double cs = m_contentScale;
  if (m_iconsScale == cs && m_favFilledScaled.IsOk() &&
      m_watchFilledScaled.IsOk() && m_favOutlineScaled.IsOk() &&
      m_watchOutlineScaled.IsOk())
    return;

  const int physSize =
      std::max(1, static_cast<int>(std::round(m_iconSizeDip * cs)));
  // Толщина контура пропорционально иконке: ~1/16 её DIP-размера,
  // минимум 2 DIP. При росте иконки (24→48→56→64) контур утолщается
  // 2→3→4→4, и визуальная «весомость» outline не падает.
  const int penDip =
      std::max(2, static_cast<int>(std::round(m_iconSizeDip / 16.0)));
  const int penPhys = std::max(1, static_cast<int>(std::round(penDip * cs)));

  auto buildFilled = [&](const wxBitmap &base128) -> wxBitmap {
    if (!base128.IsOk())
      return wxBitmap();
    wxImage img = base128.ConvertToImage();
    if (!img.IsOk())
      return wxBitmap();
    img.Rescale(physSize, physSize, wxIMAGE_QUALITY_HIGH);
    wxBitmap out(img);
    out.SetScaleFactor(cs);
    return out;
  };

  auto buildOutline = [&](bool fav) -> wxBitmap {
    // Рисуем outline сразу в целевом размере: rescale 128px-канвы с
    // pen=2 «схлопывает» контур в <1 физ. px (иконка кажется пустой/
    // микроскопической).
    wxBitmap bmp =
        fav ? CreateOutlineStarIcon(wxSize(physSize, physSize), wxNullColour,
                                    penPhys)
            : CreateOutlineBookmarkIcon(wxSize(physSize, physSize),
                                        wxNullColour, penPhys);
    if (!bmp.IsOk())
      return wxBitmap();
    bmp.SetScaleFactor(cs);
    return bmp;
  };

  m_favFilledScaled = buildFilled(m_favFilled);
  m_watchFilledScaled = buildFilled(m_watchFilled);
  m_favOutlineScaled = buildOutline(true);
  m_watchOutlineScaled = buildOutline(false);
  m_iconsScale = cs;
}

std::pair<wxRect, wxRect>
PosterCards::IconZonesDip(int cardWidthDip) const {
  const int xIcon = cardWidthDip - m_iconMarginDip - m_iconSizeDip;
  const int yFav = m_iconMarginDip;
  const int yWatch = yFav + m_iconSizeDip + m_iconGapDip;
  return {wxRect(xIcon, yFav, m_iconSizeDip, m_iconSizeDip),
          wxRect(xIcon, yWatch, m_iconSizeDip, m_iconSizeDip)};
}

wxRect PosterCards::CardRectForIndex(int idx) const {
  if (idx < 0 || m_cols <= 0)
    return wxRect();
  const int row = idx / m_cols;
  const int col = idx % m_cols;
  const int scrollY = GetScrollY();
  // Та же формула, что в OnPaint (`m_gridOffsetX + col*m_colW`,
  // `row*m_rowH`), минус scrollY — переводим в клиентские координаты.
  return wxRect(m_gridOffsetX + col * m_colW, row * m_rowH - scrollY,
                m_cardW, m_cardH);
}

std::pair<wxRect, wxRect>
PosterCards::IconZones(const wxRect &cardRect) const {
  // m_layout.cardW — DIP-ширина карточки. cardRect.width всегда равна
  // FromDIP(m_layout.cardW) для постеров (все карточки одного размера),
  // поэтому формулу берём в DIP и масштабируем в логические пиксели.
  const auto [favDip, watchDip] = IconZonesDip(m_layout.cardW);
  auto toLogical = [this, &cardRect](const wxRect &r) {
    return wxRect(cardRect.x + FromDIP(r.x), cardRect.y + FromDIP(r.y),
                  FromDIP(r.width), FromDIP(r.height));
  };
  return {toLogical(favDip), toLogical(watchDip)};
}

void PosterCards::OnMouseDown(wxMouseEvent &evt) {
  bool fav = false;
  const int idx = HitTestIndex(evt.GetPosition(), fav, nullptr);
  if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
    CardsBase::OnMouseDown(evt);
    return;
  }

  const PosterItem &item = m_items[idx];
  const wxPoint pos = evt.GetPosition();
  // Не доверяем rect из HitTestIndex: на прокрученных рядах он может
  // прийти в виртуальных координатах. Считаем клиентский rect сами.
  const auto [favZone, watchZone] = IconZones(CardRectForIndex(idx));

  if (favZone.Contains(pos)) {
    if (m_onFavoriteToggle)
      m_onFavoriteToggle(item.mediaKey);
    return;
  }
  if (watchZone.Contains(pos)) {
    if (m_onWatchlistToggle)
      m_onWatchlistToggle(item.mediaKey);
    return;
  }

  CardsBase::OnMouseDown(evt);
}

int PosterCards::GetScrollY() const {
  int sx = 0, sy = 0;
  const_cast<PosterCards *>(this)->GetViewStart(&sx, &sy);
  int px = 1, py = 1;
  const_cast<PosterCards *>(this)->GetScrollPixelsPerUnit(&px, &py);
  if (py <= 0)
    py = 1;
  return sy * py;
}

void PosterCards::SetScrollY(int y) {
  if (y <= 0) {
    Scroll(-1, 0);
    return;
  }
  int px = 1, py = 1;
  GetScrollPixelsPerUnit(&px, &py);
  if (py <= 0)
    py = 1;
  Scroll(-1, y / py);
}

void PosterCards::FocusFirst() {
  if (m_items.empty())
    return;
  if (!IsShown())
    return;
  // SelectCard handles m_focusIndex, EnsureRowVisible, invalidation, Refresh.
  // It is a no-op if m_cols == 0 (layout not ready) — widget focus still
  // applies, and CardsBase::OnKeyDown will set m_focusIndex = 0 on first key.
  SelectCard(0);
  SetFocusIgnoringChildren();
}

void PosterCards::SetTotalCount(size_t total) {
  // Reset in-flight flag: fresh total means a new page (or full reload) has
  // landed, and the next near-bottom trigger is allowed again.
  m_loadMoreInFlight = false;
  if (m_totalCount == total)
    return;
  m_totalCount = total;
  UpdateLayout();
  Refresh();
}

void PosterCards::OnNearBottom() {
  if (!m_loadMoreCallback || m_loadMoreInFlight)
    return;
  // No total known → infinite scroll not configured; never trigger.
  if (m_totalCount == 0)
    return;
  // Already fully loaded → nothing to do.
  if (m_channels.size() >= m_totalCount)
    return;
  m_loadMoreInFlight = true;
  m_loadMoreCallback();
}
