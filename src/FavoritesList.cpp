#include "FavoritesList.h"
#include "Application.h"
#include "MainFrame.h"

FavoritesList::FavoritesList(wxWindow *parent, wxWindowID id)
    : BaseChannelList(parent, id) {
  Bind(wxEVT_KEY_DOWN, &FavoritesList::OnKeyDown, this);
}

void FavoritesList::loadChannels(const std::vector<Channel> &channels) {
  int topRow = GetTopVisibleRow();

  // 1) Загрузка каналов избранного
  BeginFavoritesSync();
  LoadChannels(channels, "");
  EndFavoritesSync();

  // 2) Синхронизация флагов избранного по (name, playlist)
  if (MainFrame *parentFrame =
          dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {

    auto favChannels =
        parentFrame->getApplication()->getFavoritesManager().list();

    std::vector<std::pair<std::string, std::string>> favKeys;
    favKeys.reserve(favChannels.size());
    for (const auto &c : favChannels)
      favKeys.emplace_back(c.getName(), c.getPlaylistName());

    BeginFavoritesSync();
    GetModel()->SetFavorites(favKeys);
    EndFavoritesSync();
  }
  
  RestoreTopVisibleRow(topRow);
}

void FavoritesList::OnChannelActivated(const Channel &ch, int col) {
  if (col == 2)
    return;

  if (!m_onSelect)
    return;

  // FavoritesList — это таблица, не карточки, поэтому index/rect не
  // используются.
  m_onSelect(ch, 0, wxRect());
}

void FavoritesList::OnFavoriteToggled(const Channel &ch, bool isFav) {
  if (MainFrame *parentFrame =
          dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {
    if (!isFav) {
      parentFrame->getApplication()->getFavoritesManager().remove(
          ch.getName(), ch.getPlaylistName());
      parentFrame->refreshFavorites();
    }
  }
}

void FavoritesList::OnKeyDown(wxKeyEvent &evt) {
  int key = evt.GetKeyCode();

  wxDataViewItem item = GetSelection();
  if (!item.IsOk()) {
    evt.Skip();
    return;
  }

  int row = m_model->GetRow(item);
  if (row < 0 || row >= (int)m_model->GetCount()) {
    evt.Skip();
    return;
  }

  const Channel &ch = m_model->GetChannel(row);

  switch (key) {

  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    OnChannelActivated(ch, 0);
    return;

  case WXK_SPACE: {
    auto &fav = wxGetApp().getFavoritesManager();
    fav.remove(ch.getName(), ch.getPlaylistName());

    // Обновляем список избранных
    loadChannels(fav.list());

    // Обновляем остальные виды
    if (MainFrame *parentFrame =
            dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {
      parentFrame->refreshFavorites();
    }
  }
    return;

  default:
    evt.Skip();
    return;
  }
}
