#include "ChannelCards.h"
#include "Application.h"
#include "MainFrame.h"
#include "StarIcon.h"

ChannelCards::ChannelCards(wxWindow *parent) : CardsBase(parent) {
  m_favFilled  = CreateFilledStarIcon(wxSize(256, 256));
  m_favOutline = CreateOutlineStarIcon(wxSize(256, 256));

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

  Bind(wxEVT_LEFT_DCLICK, &ChannelCards::OnDoubleClick, this);
}

void ChannelCards::OnDoubleClick(wxMouseEvent &evt) {
  wxPoint pos = evt.GetPosition();
  bool fav = false;
  wxRect rect;
  int idx = HitTestIndex(pos, fav, &rect);
  if (idx < 0 || fav) {
    evt.Skip();
    return;
  }
  const Channel &ch = m_channels[idx];
  if (m_onSelect)
    m_onSelect(ch, idx, rect);
  MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this));
  if (mf) {
    mf->PlayChannel(ch);
  }
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
  return isFav ? m_favFilled : m_favOutline;
}

void ChannelCards::OnCardClick(size_t index, bool fav, const wxRect &rect) {
  const Channel &ch = m_channels[index];

  if (fav) {
    if (MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {
      auto &fm = mf->getApplication()->getFavoritesManager();

      if (fm.isFavorite(ch))
        fm.remove(ch);
      else
        fm.add(ch);

      mf->refreshFavorites();
    }

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
