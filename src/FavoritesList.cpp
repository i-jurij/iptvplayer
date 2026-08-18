#include "FavoritesList.h"
#include "Application.h"
#include "MainFrame.h"

#include <wx/clipbrd.h>
#include <wx/dataview.h>

FavoritesList::FavoritesList(wxWindow *parent, wxWindowID id)
    : BaseChannelList(parent, id) {
  Bind(wxEVT_KEY_DOWN, &FavoritesList::OnKeyDown, this);
  Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &FavoritesList::OnContextMenu, this);
  Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &FavoritesList::OnSelectionChanged,
       this);
}

void FavoritesList::OnSelectionChanged(wxDataViewEvent &evt) {
  if (m_closing.load() || !IsShownOnScreen() || m_ignoreSelectionEvents)
    return;

  wxWindow *tlw = wxGetTopLevelParent(this);
  wxTopLevelWindow *top = wxDynamicCast(tlw, wxTopLevelWindow);
  if (!top || !top->IsActive())
    return;

  wxDataViewItem item = evt.GetItem();
  if (!item.IsOk())
    return;

  int row = m_model->GetRow(item);
  if (row < 0 || row >= (int)m_model->GetCount())
    return;

  const Channel &ch = m_model->GetChannel(row);
  if (m_onSelect) {
    m_onSelect(ch, row, wxRect());
  }
}

void FavoritesList::loadChannels(const std::vector<Channel> &channels) {
  // 1) Загрузка каналов избранного (Reset внутри SetChannels)
  BeginFavoritesSync();
  LoadFavoritesChannels(channels, "");
  EndFavoritesSync();

  // 2) Синхронизация флагов избранного
  if (MainFrame *parentFrame =
          dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {

    auto favChannels =
        parentFrame->getApplication()->getFavoritesManager().list();

    std::vector<std::pair<std::string, std::string>> favKeys;
    favKeys.reserve(favChannels.size());
    for (const auto &c : favChannels)
      favKeys.emplace_back(c.getName(), c.getPlaylistName());

    // Устанавливаем флаги избранного
    GetModel()->SetFavorites(favKeys);

    // 🔥 Перерисовать строки вручную
    for (int i = 0; i < (int)GetModel()->GetCount(); ++i)
      GetModel()->RowChanged(i);
  }
}

void FavoritesList::ShowContextMenu(const Channel &ch) {
  wxMenu menu;

  int idCopyUrl = wxNewId();
  int idCopyName = wxNewId();

  menu.Append(idCopyUrl, "Copy URL");
  menu.Append(idCopyName, "Copy Name");

  menu.Bind(
      wxEVT_MENU,
      [ ch](wxCommandEvent &) {
        if (wxTheClipboard->Open()) {
          wxTheClipboard->SetData(
              new wxTextDataObject(wxString::FromUTF8(ch.getUrl())));
          wxTheClipboard->Close();
        } else {
          wxLogDebug("ShowContextMenu: Failed to open clipboard for URL");
        }
      },
      idCopyUrl);

  menu.Bind(
      wxEVT_MENU,
      [ ch](wxCommandEvent &) {
        if (wxTheClipboard->Open()) {
          wxTheClipboard->SetData(
              new wxTextDataObject(wxString::FromUTF8(ch.getName())));
          wxTheClipboard->Close();
        } else {
          wxLogDebug("ShowContextMenu: Failed to open clipboard for Name");
        }
      },
      idCopyName);

  this->PopupMenu(&menu);
}

void FavoritesList::OnChannelActivated(const Channel &ch, int col) {
  if (col == 3) // click on fav col
    return;

  if (m_onSelect)
    m_onSelect(ch, 0, wxRect());

  MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this));
  if (mf) {
    mf->PlayChannel(ch);
  }
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
  if (!this->IsShownOnScreen()) {
    evt.Skip();
    return;
  }

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
  case WXK_UP:
  case WXK_DOWN:
  case WXK_PAGEUP:
  case WXK_PAGEDOWN:
  case WXK_HOME:
  case WXK_END:
    evt.Skip(); // пусть wxDataViewCtrl сделает своё дело (сдвинет
                // выделение/видимую область)
    // После обработки события вызываем HandleVisibleRangeChange
    CallAfterSafeById(GetId(), [this]() {
      if (!m_closing.load())
        HandleVisibleRangeChange();

      wxDataViewItem item = GetSelection();
      if (item.IsOk()) {
        int row = m_model->GetRow(item);
        if (row >= 0 && row < (int)m_model->GetCount()) {
          const Channel &ch = m_model->GetChannel(row);
          if (m_onSelect)
            m_onSelect(ch, row, wxRect());
        }
      }
    });
    return;
  default:
    evt.Skip();
    return;
  }
}

void FavoritesList::OnContextMenu(wxDataViewEvent &evt) {
  wxDataViewItem item = evt.GetItem();
  if (!item.IsOk()) {
    evt.Skip();
    return;
  }

  int row = m_model->GetRow(item);
  if (row < 0 || row >= (int)m_model->GetCount()) {
    evt.Skip();
    return;
  }

  // Выделяем строку, по которой был клик
  UnselectAll();
  Select(item);
  SetCurrentItem(item);

  const Channel &ch = m_model->GetChannel(row);
  ShowContextMenu(ch);
}
