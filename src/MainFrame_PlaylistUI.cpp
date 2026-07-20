// src/MainFrame_PlaylistUI.cpp
#include "MainFrame.h"
#include "EventIDs.h"

#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>

void MainFrame::adjustTitleColumnWidth() {
  if (!m_playlistList)
    return;

  wxClientDC dc(m_playlistList);
  dc.SetFont(m_playlistList->GetFont());

  wxListItem colItem;
  colItem.SetMask(wxLIST_MASK_TEXT);
  if (m_playlistList->GetColumn(1, colItem)) {
    wxString titleHeader = colItem.GetText();
    int headerWidth = dc.GetTextExtent(titleHeader).GetWidth() + 20;
    int maxTitleWidth = dc.GetTextExtent(wxString('X', 50)).GetWidth();

    m_playlistList->SetColumnWidth(1, wxLIST_AUTOSIZE);
    int current = m_playlistList->GetColumnWidth(1);

    int longestTitleWidth = 0;
    wxListItem item;
    item.SetMask(wxLIST_MASK_TEXT);
    item.SetColumn(1);
    for (long row = 0; row < m_playlistList->GetItemCount(); ++row) {
      item.SetId(row);
      if (m_playlistList->GetItem(item)) {
        int textWidth = dc.GetTextExtent(item.GetText()).GetWidth();
        if (textWidth > longestTitleWidth) {
          longestTitleWidth = textWidth;
        }
      }
    }

    int targetWidth = std::max({current, headerWidth, longestTitleWidth});
    if (targetWidth > maxTitleWidth)
      targetWidth = maxTitleWidth;
    m_playlistList->SetColumnWidth(1, targetWidth);
  }

  colItem.SetMask(wxLIST_MASK_TEXT);
  if (m_playlistList->GetColumn(2, colItem)) {
    wxString sourceHeader = colItem.GetText();
    int headerWidth2 = dc.GetTextExtent(sourceHeader).GetWidth() + 20;
    int maxSourceWidth = dc.GetTextExtent(wxString('X', 150)).GetWidth();

    m_playlistList->SetColumnWidth(2, wxLIST_AUTOSIZE);
    int current = m_playlistList->GetColumnWidth(2);

    int longestSourceWidth = 0;
    wxListItem item;
    item.SetMask(wxLIST_MASK_TEXT);
    item.SetColumn(2);
    for (long row = 0; row < m_playlistList->GetItemCount(); ++row) {
      item.SetId(row);
      if (m_playlistList->GetItem(item)) {
        int textWidth = dc.GetTextExtent(item.GetText()).GetWidth();
        if (textWidth > longestSourceWidth) {
          longestSourceWidth = textWidth;
        }
      }
    }

    int targetWidth = std::max({current, headerWidth2, longestSourceWidth});
    if (targetWidth > maxSourceWidth)
      targetWidth = maxSourceWidth;
    m_playlistList->SetColumnWidth(2, targetWidth);
  }
}

void MainFrame::createPlaylistPanel() {
  // Если уже создана — ничего не делаем
  if (m_playlistPanel)
    return;

  m_playlistPanel = new wxPanel(m_notebook, wxID_ANY);

  auto *panelSizer = new wxBoxSizer(wxVERTICAL);

  // Заголовок
  auto *title = new wxStaticText(m_playlistPanel, wxID_ANY, "Playlists");
  auto titleFont = title->GetFont();
  titleFont.SetPointSize(12);
  titleFont.SetWeight(wxFONTWEIGHT_BOLD);
  title->SetFont(titleFont);
  panelSizer->Add(title, 0, wxALL, 12);

  // Верхние кнопки
  auto *topBtnSizer = createPlaylistTopButtons();
  panelSizer->Add(topBtnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

  // Список плейлистов
  createPlaylistList();
  panelSizer->Add(m_playlistList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

  // Нижние кнопки
  auto *bottomBtnSizer = createPlaylistButtons();
  panelSizer->Add(bottomBtnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

  m_playlistPanel->SetSizer(panelSizer);

  int topBtnWidth = topBtnSizer->GetMinSize().GetWidth();
  int bottomBtnWidth = bottomBtnSizer->GetMinSize().GetWidth();
  int minWidth = std::max(topBtnWidth, bottomBtnWidth) + 12;

  m_playlistPanel->SetMinSize(wxSize(minWidth, -1));
}

wxSizer *MainFrame::createPlaylistTopButtons() {
  auto *topBtnSizer = new wxBoxSizer(wxHORIZONTAL);

  auto *addIptvBtn = new wxButton(m_playlistPanel, ID_ADD_IPTV_PLAYLIST,
                                  "Add from IPTV-Org...");
  topBtnSizer->Add(addIptvBtn, 0, wxRIGHT, 5);

  auto *addFileBtn =
      new wxButton(m_playlistPanel, ID_ADD_PLAYLIST_FILE, "Add from File...");
  topBtnSizer->Add(addFileBtn, 0, wxRIGHT, 5);

  auto *addUrlBtn =
      new wxButton(m_playlistPanel, ID_ADD_PLAYLIST_URL, "Add from URL...");
  topBtnSizer->Add(addUrlBtn, 0, wxRIGHT, 5);

  m_updateAllBtn =
      new wxButton(m_playlistPanel, ID_UPDATE_ALL_PLAYLISTS, "Update All");
  topBtnSizer->Add(m_updateAllBtn, 0);

  auto *vbox = new wxBoxSizer(wxVERTICAL);
  vbox->Add(topBtnSizer, 0, wxEXPAND | wxALL, 5);
  return vbox;
}

wxSizer *MainFrame::createPlaylistButtons() {
  auto *btnSizer = new wxBoxSizer(wxHORIZONTAL);

  m_openBtn = new wxButton(m_playlistPanel, ID_OPEN_PLAYLIST, "Open");
  m_updateBtn = new wxButton(m_playlistPanel, ID_UPDATE_PLAYLIST, "Update");
  m_editBtn = new wxButton(m_playlistPanel, ID_EDIT_PLAYLIST, "Edit");
  m_removeBtn = new wxButton(m_playlistPanel, ID_REMOVE_PLAYLIST, "Remove");

  m_openBtn->Enable(false);
  m_updateBtn->Enable(false);
  m_editBtn->Enable(false);
  m_removeBtn->Enable(false);

  btnSizer->Add(m_openBtn, 0, wxRIGHT, 5);
  btnSizer->Add(m_updateBtn, 0, wxRIGHT, 5);
  btnSizer->Add(m_editBtn, 0, wxRIGHT, 5);
  btnSizer->Add(m_removeBtn, 0);

  auto *vbox = new wxBoxSizer(wxVERTICAL);
  vbox->Add(btnSizer, 0, wxEXPAND | wxALL, 5);
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

  m_playlistList->InsertColumn(0, "#", wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(1, "Title", wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(2, "Source", wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(3, "Channels", wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(4, "Auto update", wxLIST_FORMAT_LEFT);
  m_playlistList->InsertColumn(5, "Last Update", wxLIST_FORMAT_LEFT);

  // Синхронизация состояния при выборе элемента
  m_playlistList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent &evt) {
    long item = evt.GetIndex();
    if (item >= 0) {
      m_selectedPlaylistIndex =
          static_cast<int>(m_playlistList->GetItemData(item));
      enablePlaylistButtons(true);
      // Обновляем статусбар с названием
      wxString title = m_playlistList->GetItemText(item, 1);
      SetStatusText(wxString::Format("Playlist selected: %s", title), 1);
    }
  });

  // Снятие выделения
  m_playlistList->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent &) {
    if (m_playlistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                    wxLIST_STATE_SELECTED) == -1) {
      m_selectedPlaylistIndex = -1;
      enablePlaylistButtons(false);
      SetStatusText("No playlist selected", 1);
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
    int clientWidth = m_playlistList->GetClientSize().GetWidth();
    if (clientWidth <= 0)
      return;
    for (int i = 0; i < m_playlistList->GetColumnCount(); ++i) {
      m_playlistList->SetColumnWidth(i, wxLIST_AUTOSIZE_USEHEADER);
    }
    adjustTitleColumnWidth();
  });

  adjustTitleColumnWidth();

  // Bind Delete key
  m_playlistList->Bind(wxEVT_KEY_DOWN, &MainFrame::onPlDelKeyDown, this);
}
