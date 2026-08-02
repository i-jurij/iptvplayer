#include "ChannelCards.h"
#include "Application.h"
#include "MainFrame.h"

#include "star_filled_png.h"
#include "star_outline_png.h"

ChannelCards::ChannelCards(wxWindow *parent) : CardsBase(parent) {
  m_favFilled = wxBitmap::NewFromPNGData(star_filled_png, star_filled_png_len);
  m_favOutline =
      wxBitmap::NewFromPNGData(star_outline_png, star_outline_png_len);

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

void ChannelCards::SetChannels(const std::vector<Channel> &channels,
                               const std::string &playlistName) {
  m_playlistName = playlistName;

  std::vector<Channel> tmp = channels;
  for (auto &ch : tmp)
    ch.setPlaylistName(playlistName);

  SetChannelsBase(tmp);
}

wxBitmap ChannelCards::GetStarBitmap(const Channel &ch) const {
  bool isFav = false;
  if (MainFrame *mf = dynamic_cast<MainFrame *>(
          wxGetTopLevelParent(const_cast<ChannelCards *>(this)))) {
    isFav = mf->getApplication()->getFavoritesManager().isFavorite(ch);
  }

  if (isFav)
    return m_favFilled;

  wxBitmap bmp = m_favOutline;
  if (!bmp.IsOk())
    return bmp;

  wxImage img = bmp.ConvertToImage();
  if (!img.IsOk())
    return bmp;

  if (!img.HasAlpha())
    img.InitAlpha();

  const unsigned char R = 150;
  const unsigned char G = 150;
  const unsigned char B = 150;

  int w = img.GetWidth();
  int h = img.GetHeight();

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      unsigned char a = img.GetAlpha(x, y);
      if (a == 0)
        continue;
      img.SetRGB(x, y, R, G, B);
    }
  }

  return wxBitmap(img);
}

void ChannelCards::OnCardClick(size_t index, bool fav, const wxRect &rect) {
  const Channel &ch = m_channels[index];

  if (fav) {
    if (MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {
      auto &fm = mf->getApplication()->getFavoritesManager();

      bool isFav = fm.isFavorite(ch);
      if (isFav)
        fm.remove(ch.getName(), ch.getPlaylistName());
      else
        fm.add(ch);

      mf->refreshFavorites();
    }

    // TILE-ONLY обновление
    RenderTile(index);
    MarkCardDirty((int)index);

    return;
  }

  if (m_onSelect)
    m_onSelect(ch, index, rect);
}

bool ChannelCards::RemoveChannel(const std::string &name,
                                 const std::string &playlistName) {
  // Вызываем базовый метод удаления
  return CardsBase::RemoveChannel(name, playlistName);
}
