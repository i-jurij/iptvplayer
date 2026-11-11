#include "xtream/PosterCards.h"

#include "BookmarkIcon.h"
#include "LogControl.h"
#include "StarIcon.h"
#include "Utils_Platform.h"
#include "Utils_UI.h"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>

#include <algorithm>
#include <cmath>

namespace {

struct PosterLayoutDips {
  int posterW = 0;
  int posterH = 0;
  int titleH = 0;
  int titleFontPt = 10;
  int gapX = 14;
  int gapY = 14;
};

PosterLayoutDips ComputePosterLayout(int displayW_Dip) {
  PosterLayoutDips L;
  if (displayW_Dip <= 1920) {
    L.posterW = 220;
    L.posterH = 330;
    L.titleFontPt = 10;
  } else if (displayW_Dip <= 2560) {
    L.posterW = 260;
    L.posterH = 390;
    L.titleFontPt = 11;
  } else {
    L.posterW = 300;
    L.posterH = 450;
    L.titleFontPt = 12;
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
  m_favFilled = CreateFilledStarIcon(wxSize(128, 128));
  m_watchFilled = CreateFilledBookmarkIcon(wxSize(128, 128));
}

void PosterCards::SetItems(const std::vector<PosterItem> &items) {
  std::vector<PosterItem> copy = items;
  SetItems(std::move(copy));
}

void PosterCards::SetItems(std::vector<PosterItem> &&items) {
  m_items = std::move(items);
  m_requestedPosters.clear();

  m_channels.clear();
  m_channels.reserve(m_items.size());
  for (const auto &item : m_items)
    m_channels.push_back(ConvertToChannel(item));

  ClearAllCaches(true, true);

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

  m_colW = m_cardW + m_gapX;
  m_rowH = m_cardH + m_gapY;

  m_cols = std::max(1, clientW / std::max(1, m_colW));
  if (!m_channels.empty())
    m_cols = std::min(m_cols, static_cast<int>(m_channels.size()));

  const int totalGridWidth =
      (m_cols > 0) ? (m_cols * m_cardW + (m_cols - 1) * m_gapX) : 0;
  m_gridOffsetX = std::max(0, (clientW - totalGridWidth) / 2);

  const int totalW = std::max(clientW, totalGridWidth);
  const size_t rows = m_channels.empty() ? 1
                                         : (m_channels.size() + m_cols - 1) /
                                               static_cast<size_t>(m_cols);
  const int totalH = static_cast<int>(rows) * m_rowH;
  SetVirtualSize(totalW, totalH);

  m_contentScale = GetContentScale(this);

  ClearAllCaches(false, true);
  m_requestedPosters.clear();

  m_iconsScale = 0.0;
  m_favFilledScaled = wxBitmap();
  m_watchFilledScaled = wxBitmap();

  Refresh(false);

  if (m_hoverIndex >= static_cast<int>(m_channels.size())) {
    const int old = m_hoverIndex;
    m_hoverIndex = -1;
    if (old >= 0)
      InvalidateCardClientRectByIndex(old);
  }
}

void PosterCards::RenderTile(size_t index) {
  if (index >= m_channels.size() || index >= m_items.size())
    return;

  auto dip = [this](int v) { return wxWindow::FromDIP(v, this); };

  const double cs = m_contentScale;
  const int physCardW = std::max(1, static_cast<int>(std::round(m_cardW * cs)));
  const int physCardH = std::max(1, static_cast<int>(std::round(m_cardH * cs)));

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
  if (!item.posterUrl.empty() && !m_playlistName.empty()) {
    const int physW = std::max(1, static_cast<int>(std::round(m_cardW * cs)));
    const int physH = std::max(1, static_cast<int>(std::round(m_logoH * cs)));
    const int scale100 =
        std::max(100, static_cast<int>(std::round(cs * 100.0)));
    const std::string key = LogoCache::MakePosterKey(
        m_playlistName, item.mediaKey, physW, physH, scale100);

    auto cached = LogoCache::GetCachedPosterPtr(key);
    if (cached && cached->IsOk()) {
      const wxSize ls = cached->GetLogicalSize();
      const int dx = (posterW - ls.x) / 2;
      const int dy = (posterH - ls.y) / 2;
      mdc.DrawBitmap(*cached, dx, dy, true);
      drewPoster = true;
    } else if (m_requestedPosters.insert(key).second) {
      LogoCache::GetPosterAsync(
          m_playlistName, item.mediaKey, item.posterUrl, physW, physH, posterBg,
          [this, key, index](LogoCache::LogoBitmapPtr bmp) {
            if (this->IsBeingDeleted())
              return;
            m_requestedPosters.erase(key);
            if (!bmp || !bmp->IsOk())
              return;
            RenderTile(index);
            MarkCardDirty(static_cast<int>(index));
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

  if (item.isFavorite || item.isWatchlist) {
    EnsureScaledIcons();

    const int xIcon = posterW - kIconMarginDip - kIconSizeDip;
    int yIcon = kIconMarginDip;

    if (item.isFavorite && m_favFilledScaled.IsOk()) {
      mdc.DrawBitmap(m_favFilledScaled, xIcon, yIcon, true);
      yIcon += kIconSizeDip + kIconGapDip;
    }
    if (item.isWatchlist && m_watchFilledScaled.IsOk())
      mdc.DrawBitmap(m_watchFilledScaled, xIcon, yIcon, true);
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

  m_tileCache[index] = std::make_shared<wxBitmap>(std::move(bmp));
  AddTileToLRU(index, m_tileCache[index]);
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
      m_watchFilledScaled.IsOk())
    return;

  auto makeScaled = [cs](const wxBitmap &base, int sizeDip) -> wxBitmap {
    if (!base.IsOk())
      return wxBitmap();
    const int phys = std::max(1, static_cast<int>(std::round(sizeDip * cs)));
    wxImage img = base.ConvertToImage();
    if (!img.IsOk())
      return wxBitmap();
    img.Rescale(phys, phys, wxIMAGE_QUALITY_HIGH);
    wxBitmap bmp(img);
    bmp.SetScaleFactor(cs);
    return bmp;
  };

  m_favFilledScaled = makeScaled(m_favFilled, 24);
  m_watchFilledScaled = makeScaled(m_watchFilled, 24);
  m_iconsScale = cs;
}

void PosterCards::OnMouseDown(wxMouseEvent &evt) {
  bool fav = false;
  wxRect rect;
  const int idx = HitTestIndex(evt.GetPosition(), fav, &rect);
  if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
    CardsBase::OnMouseDown(evt);
    return;
  }

  const PosterItem &item = m_items[idx];
  auto dip = [this](int v) { return wxWindow::FromDIP(v, this); };

  const int iconSize = dip(kIconSizeDip);
  const int xIcon = rect.x + rect.width - dip(kIconMarginDip + kIconSizeDip);
  int yIcon = rect.y + dip(kIconMarginDip);

  const wxPoint pos = evt.GetPosition();

  if (item.isFavorite) {
    const wxRect favZone(xIcon, yIcon, iconSize, iconSize);
    if (favZone.Contains(pos)) {
      if (m_onFavoriteToggle)
        m_onFavoriteToggle(item.mediaKey);
      return;
    }
    yIcon += iconSize + dip(kIconGapDip);
  }

  if (item.isWatchlist) {
    const wxRect watchZone(xIcon, yIcon, iconSize, iconSize);
    if (watchZone.Contains(pos)) {
      if (m_onWatchlistToggle)
        m_onWatchlistToggle(item.mediaKey);
      return;
    }
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
