#include "FavoritesCards.h"
#include "Application.h"
#include "MainFrame.h"

#include "star_filled_png.h"

FavoritesCards::FavoritesCards(wxWindow *parent) : CardsBase(parent) {
  m_favFilled = wxBitmap::NewFromPNGData(star_filled_png, star_filled_png_len);
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
