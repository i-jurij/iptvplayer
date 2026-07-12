#include "FavoritesCards.h"
#include "Application.h"
#include "MainFrame.h"
#include "TypeAheadSearch.h"

#include "star_filled_png.h"

#include <memory>

FavoritesCards::FavoritesCards(wxWindow *parent) : CardsBase(parent) {
  m_favFilled = wxBitmap::NewFromPNGData(star_filled_png, star_filled_png_len);

  m_search = std::make_unique<TypeAheadSearch>(
      this, [this]() { return (int)m_channels.size(); },
      [this](int i) { return wxString::FromUTF8(m_channels[i].getName()); },
      [this](int i) {
        if (i < 0 || i >= (int)m_channels.size() || m_cols <= 0)
          return;
        int row = i / m_cols;
        int col = i % m_cols;
        EnsureRowVisible(row);
        wxPoint center = GetCardClientCenter(col, row);
        m_lastMouseClientPos = center;
        UpdateHoverAtPoint(center);
      });

  Bind(wxEVT_CHAR, [this](wxKeyEvent &e) { m_search->OnChar(e); });
}

void FavoritesCards::SetChannels(const std::vector<Channel> &channels) {
  SetChannelsBase(channels);
}

void FavoritesCards::SyncFavorites(
    const std::vector<std::pair<std::string, std::string>> &favKeys) {
  m_favorites.clear();

  for (const auto &p : favKeys) {
    m_favorites.insert(p.first + "|" + p.second);
  }

  WarmUpTiles();
  Refresh();
}

wxBitmap FavoritesCards::GetStarBitmap(const Channel & /*ch*/) const {
  return m_favFilled;
}

void FavoritesCards::OnCardClick(size_t index, bool fav, const wxRect &rect) {
  const Channel &ch = m_channels[index];

  if (fav) {
    if (MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {
      mf->getApplication()->getFavoritesManager().remove(
          ch.getName(), ch.getPlaylistName());
      mf->refreshFavorites();
    }

    // TILE-ONLY обновление
    MarkCardDirty((int)index);
    RenderTile(index);
    InvalidateCardClientRectByIndex((int)index);

    return;
  }

  if (m_onSelect)
    m_onSelect(ch, index, rect);
}
