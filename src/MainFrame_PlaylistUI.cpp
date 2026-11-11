#include "ConfigManager.h"
#include "EventIDs.h"
#include "MainFrame.h"
#include "Utils.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/statline.h>

void MainFrame::adjustPlaylistColumns() {
  if (!m_playlistList)
    return;

  int clientW = m_playlistList->GetClientSize().GetWidth();
  if (clientW <= 0)
    return;

  clientW -= wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
  clientW -= FromDIP(4);
  if (clientW <= 60)
    return;

  wxClientDC dc(m_playlistList);
  dc.SetFont(m_playlistList->GetFont());
  const int pad = FromDIP(20);

  auto getHeader = [&](int col) -> wxString {
    wxListItem item;
    item.SetMask(wxLIST_MASK_TEXT);
    if (m_playlistList->GetColumn(col, item))
      return item.GetText();
    return wxString();
  };

  auto measure = [&](int col) -> int {
    int w = dc.GetTextExtent(getHeader(col)).GetWidth() + pad;
    for (long r = 0; r < m_playlistList->GetItemCount(); ++r) {
      int cw =
          dc.GetTextExtent(m_playlistList->GetItemText(r, col)).GetWidth() +
          pad;
      if (cw > w)
        w = cw;
    }
    return w;
  };

  const int kMinNumW = FromDIP(40);
  const int kMinChannelsW = FromDIP(60);
  const int kMinStatusW = FromDIP(80);
  const int kMinExpiresW = FromDIP(80);
  const int kMinAutoW = FromDIP(50);
  const int kMinLastUpdW = FromDIP(120);

  const int w0 = std::max(measure(0), kMinNumW);
  const int w3 = std::max(measure(3), kMinChannelsW);
  const int w4 = std::max(measure(4), kMinStatusW);
  const int w5 = std::max(measure(5), kMinExpiresW);
  const int w6 = std::max(measure(6), kMinAutoW);
  const int w7 = std::max(measure(7), kMinLastUpdW);

  const int fixedSum = w0 + w3 + w4 + w5 + w6 + w7;

  const int kMinTitleW = FromDIP(120);
  const int kMaxTitleW = FromDIP(400);
  const int kMinUrlW = FromDIP(180);

  int titleW = std::clamp(measure(1), kMinTitleW, kMaxTitleW);
  int urlW = std::max(measure(2), kMinUrlW);

  const int needed = fixedSum + titleW + urlW;
  if (needed > clientW) {
    int overflow = needed - clientW;

    const int urlCanShrink = std::max(0, urlW - kMinUrlW);
    const int shrinkUrl = std::min(overflow, urlCanShrink);
    urlW -= shrinkUrl;
    overflow -= shrinkUrl;

    if (overflow > 0) {
      const int titleCanShrink = std::max(0, titleW - kMinTitleW);
      const int shrinkTitle = std::min(overflow, titleCanShrink);
      titleW -= shrinkTitle;
    }
  }

  m_playlistList->SetColumnWidth(0, w0);
  m_playlistList->SetColumnWidth(1, titleW);
  m_playlistList->SetColumnWidth(2, urlW);
  m_playlistList->SetColumnWidth(3, w3);
  m_playlistList->SetColumnWidth(4, w4);
  m_playlistList->SetColumnWidth(5, w5);
  m_playlistList->SetColumnWidth(6, w6);
  m_playlistList->SetColumnWidth(7, w7);
}

void MainFrame::createPlaylistPanel() {
  // Если уже создана — ничего не делаем
  if (m_playlistPanel)
    return;

  m_playlistPanel = new wxPanel(m_notebook, wxID_ANY);

  auto *panelSizer = new wxBoxSizer(wxVERTICAL);

  // Заголовок
  auto *title = new wxStaticText(m_playlistPanel, wxID_ANY, _U("Playlists"));
  {
    auto titleFont = title->GetFont();
    titleFont.SetPointSize(12);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
  }
  auto *titleRow = new wxBoxSizer(wxHORIZONTAL);
  titleRow->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

  m_playlistsInfoLabel = new wxStaticText(m_playlistPanel, wxID_ANY, "");
  {
    wxFont f = m_playlistsInfoLabel->GetFont();
    f.SetPointSize(f.GetPointSize() - 1);
    m_playlistsInfoLabel->SetFont(f);
  }
  titleRow->Add(m_playlistsInfoLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                FromDIP(8));
  titleRow->AddStretchSpacer(1);
  titleRow->SetMinSize(wxSize(-1, GetHeaderRowHeight()));
  panelSizer->Add(titleRow, 0, wxEXPAND | wxALL, FromDIP(8));

  // Верхние кнопки
  auto *topBtnSizer = createPlaylistTopButtons();
  panelSizer->Add(topBtnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

  // Список плейлистов
  createPlaylistList();
  panelSizer->Add(m_playlistList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                  FromDIP(6));

  // Нижние кнопки
  auto *bottomBtnSizer = createPlaylistButtons();
  panelSizer->Add(bottomBtnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

  m_playlistPanel->SetSizer(panelSizer);

  int topBtnWidth = topBtnSizer->GetMinSize().GetWidth();
  int bottomBtnWidth = bottomBtnSizer->GetMinSize().GetWidth();
  int minWidth = std::max(topBtnWidth, bottomBtnWidth) + 12;

  m_playlistPanel->SetMinSize(wxSize(minWidth, -1));
}

wxSizer *MainFrame::createPlaylistTopButtons() {
  auto *topBtnSizer = new wxBoxSizer(wxHORIZONTAL);

  auto *addIptvBtn = new wxButton(m_playlistPanel, ID_ADD_IPTV_PLAYLIST,
                                  _U("Add from IPTV-Org..."));
  topBtnSizer->Add(addIptvBtn, 0, wxRIGHT, FromDIP(6));

  auto *addUrlBtn =
      new wxButton(m_playlistPanel, ID_ADD_PLAYLIST_URL,
                   _U("Add from URL..."));
  topBtnSizer->Add(addUrlBtn, 0, wxRIGHT, FromDIP(6));

  auto *addXtreamBtn =
      new wxButton(m_playlistPanel, ID_ADD_XTREAM_PLAYLIST,
                   _U("Add Xtream..."));
  topBtnSizer->Add(addXtreamBtn, 0, wxRIGHT, FromDIP(6));

  auto *addFileBtn =
      new wxButton(m_playlistPanel, ID_ADD_PLAYLIST_FILE,
                   _U("Add from File..."));
  topBtnSizer->Add(addFileBtn, 0);

  auto *vbox = new wxBoxSizer(wxVERTICAL);
  vbox->Add(topBtnSizer, 0, wxEXPAND | wxALL, FromDIP(6));
  return vbox;
}

void MainFrame::onPlaylistsAutoUpdateAll(wxCommandEvent &) {
  auto *cfg = getConfigManager();
  if (!cfg || !m_playlistsAutoUpdateAllCheck)
    return;
  bool checked = m_playlistsAutoUpdateAllCheck->GetValue();
  cfg->setSetting("auto_update", checked ? "true" : "false");
  cfg->saveSettings();
  SetStatusText(checked ? _U("All playlists will be auto-updated at startup.")
                        : _U("Only playlists with their own auto-update flag "
                             "will be updated."),
                0);
}

wxSizer *MainFrame::createPlaylistButtons() {
  auto *btnSizer = new wxBoxSizer(wxHORIZONTAL);

  m_infoBtn = new wxButton(m_playlistPanel, ID_PLAYLIST_INFO, _U("Info"));
  m_openBtn = new wxButton(m_playlistPanel, ID_OPEN_PLAYLIST, _U("Open"));
  m_updateBtn = new wxButton(m_playlistPanel, ID_UPDATE_PLAYLIST, _U("Update"));
  m_editBtn = new wxButton(m_playlistPanel, ID_EDIT_PLAYLIST, _U("Edit"));
  m_removeBtn = new wxButton(m_playlistPanel, ID_REMOVE_PLAYLIST, _U("Remove"));

  m_infoBtn->Enable(false);
  m_openBtn->Enable(false);
  m_updateBtn->Enable(false);
  m_editBtn->Enable(false);
  m_removeBtn->Enable(false);

  btnSizer->Add(m_infoBtn, 0, wxRIGHT, FromDIP(6));
  btnSizer->Add(m_openBtn, 0, wxRIGHT, FromDIP(6));
  btnSizer->Add(m_updateBtn, 0, wxRIGHT, FromDIP(6));
  btnSizer->Add(m_editBtn, 0, wxRIGHT, FromDIP(6));
  btnSizer->Add(m_removeBtn, 0, wxRIGHT, FromDIP(10));

  auto *separator =
      new wxStaticLine(m_playlistPanel, wxID_ANY, wxDefaultPosition,
                       wxDefaultSize, wxLI_VERTICAL);
  separator->SetMinSize(wxSize(-1, FromDIP(24)));
  btnSizer->Add(separator, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

  m_updateAllBtn =
      new wxButton(m_playlistPanel, ID_UPDATE_ALL_PLAYLISTS, _U("Update All"));
  btnSizer->Add(m_updateAllBtn, 0, wxRIGHT, FromDIP(10));

  m_playlistsAutoUpdateAllCheck =
      new wxCheckBox(m_playlistPanel, wxID_ANY, _U("Auto-update all on startup"));
  m_playlistsAutoUpdateAllCheck->SetToolTip(
      _U("Update all playlists at startup, ignoring individual flags."));
  if (auto *cfg = getConfigManager()) {
    bool v = (cfg->getSetting("auto_update", "false") == "true");
    m_playlistsAutoUpdateAllCheck->SetValue(v);
  }
  m_playlistsAutoUpdateAllCheck->Bind(
      wxEVT_CHECKBOX, &MainFrame::onPlaylistsAutoUpdateAll, this);
  btnSizer->Add(m_playlistsAutoUpdateAllCheck, 0, wxALIGN_CENTER_VERTICAL);

  auto *vbox = new wxBoxSizer(wxVERTICAL);
  vbox->Add(btnSizer, 0, wxEXPAND | wxALL, FromDIP(6));
  return vbox;
}

void MainFrame::createPlaylistList() {
  // This function existed previously; ensure it's implemented here or left
  // as-is.
  if (!m_playlistPanel)
    return;

  m_playlistList =
      new wxListCtrl(m_playlistPanel, ID_PLAYLIST_LIST, wxDefaultPosition,
                     wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);

  m_playlistList->InsertColumn(0, _U("#"), wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(1, _U("Title"), wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(2, _U("Source"), wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(3, _U("Channels"), wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(4, _U("Status"), wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(5, _U("Expires"), wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(6, _U("Auto update"), wxLIST_FORMAT_CENTER);
  m_playlistList->InsertColumn(7, _U("Last Update"), wxLIST_FORMAT_LEFT);

  // Синхронизация состояния при выборе элемента
  m_playlistList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent &evt) {
    long item = evt.GetIndex();
    if (item >= 0) {
      m_selectedPlaylistIndex =
          static_cast<int>(m_playlistList->GetItemData(item));
      enablePlaylistButtons(true);
      // Обновляем статусбар с названием
      wxString title = m_playlistList->GetItemText(item, 1);
      SetStatusText(wxString::Format(_U("Playlist selected: %s"), title), 1);
    }
  });

  // Снятие выделения
  m_playlistList->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent &) {
    if (m_playlistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                    wxLIST_STATE_SELECTED) == -1) {
      m_selectedPlaylistIndex = -1;
      enablePlaylistButtons(false);
      SetStatusText(_U("No playlist selected"), 1);
    } else {
      long cur = m_playlistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);
      if (cur != -1) {
        m_selectedPlaylistIndex =
            static_cast<int>(m_playlistList->GetItemData(cur));
        enablePlaylistButtons(true);
      }
    }
  });

  m_playlistPanel->Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) {
    evt.Skip();
    adjustPlaylistColumns();
  });

  adjustPlaylistColumns();

  // Bind Delete key
  m_playlistList->Bind(wxEVT_KEY_DOWN, &MainFrame::onPlDelKeyDown, this);
}
