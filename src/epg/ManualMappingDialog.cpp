#include "ManualMappingDialog.h"
#include "Channel.h"
#include "MainFrame.h"
#include "epg/EPGManager.h"
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

ManualMappingDialog::ManualMappingDialog(wxWindow *parent, EPGManager *epgMgr,
                                         MainFrame *mainFrame)
    : wxDialog(parent, wxID_ANY, _("Manual Mapping"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_epgMgr(epgMgr), m_mainFrame(mainFrame), m_preselectedTvgId(""),
      m_selectedMappingIndex(-1), m_preselectedChannelName("") {

  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // ---- Верхняя часть: два списка ----
  wxBoxSizer *topSizer = new wxBoxSizer(wxHORIZONTAL);

  // Левый список: каналы плейлиста
  wxStaticBox *leftBox =
      new wxStaticBox(this, wxID_ANY, _("Playlist Channels"));
  wxStaticBoxSizer *leftSizer = new wxStaticBoxSizer(leftBox, wxVERTICAL);
  m_playlistList = new wxListCtrl(leftBox, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  m_playlistList->InsertColumn(0, _("Name"), wxLIST_FORMAT_LEFT, FromDIP(200));
  m_playlistList->InsertColumn(1, _("tvg-id"), wxLIST_FORMAT_LEFT,
                               FromDIP(150));
  leftSizer->Add(m_playlistList, 1, wxEXPAND | wxALL, FromDIP(5));
  topSizer->Add(leftSizer, 1, wxEXPAND | wxALL, FromDIP(5));

  // Правый список: EPG-каналы
  wxStaticBox *rightBox = new wxStaticBox(this, wxID_ANY, _("EPG Channels"));
  wxStaticBoxSizer *rightSizer = new wxStaticBoxSizer(rightBox, wxVERTICAL);
  m_epgList = new wxListCtrl(rightBox, wxID_ANY, wxDefaultPosition,
                             wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  m_epgList->InsertColumn(0, _("ID"), wxLIST_FORMAT_LEFT, FromDIP(150));
  m_epgList->InsertColumn(1, _("Display Name"), wxLIST_FORMAT_LEFT,
                          FromDIP(200));
  rightSizer->Add(m_epgList, 1, wxEXPAND | wxALL, FromDIP(5));
  topSizer->Add(rightSizer, 1, wxEXPAND | wxALL, FromDIP(5));

  mainSizer->Add(topSizer, 2, wxEXPAND);

  // ---- Кнопка Add ----
  wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
  m_addBtn = new wxButton(this, wxID_ANY, _("Add mapping ->"));
  m_addBtn->Enable(false);
  btnSizer->Add(m_addBtn, 0, wxALL, FromDIP(5));
  mainSizer->Add(btnSizer, 0, wxALIGN_CENTER);

  // ---- Список маппингов ----
  wxStaticBox *mapBox = new wxStaticBox(this, wxID_ANY, _("Current Mappings"));
  wxStaticBoxSizer *mapSizer = new wxStaticBoxSizer(mapBox, wxVERTICAL);

  m_mappingList = new wxListCtrl(mapBox, wxID_ANY, wxDefaultPosition,
                                 wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  m_mappingList->InsertColumn(0, _("Playlist tvg-id"), wxLIST_FORMAT_LEFT,
                              FromDIP(150));
  m_mappingList->InsertColumn(1, _("Playlist Name"), wxLIST_FORMAT_LEFT,
                              FromDIP(200));
  m_mappingList->InsertColumn(2, _("EPG ID"), wxLIST_FORMAT_LEFT, FromDIP(150));
  m_mappingList->InsertColumn(3, _("EPG Name"), wxLIST_FORMAT_LEFT,
                              FromDIP(200));
  m_mappingList->InsertColumn(4, _("Status"), wxLIST_FORMAT_LEFT, FromDIP(80));
  m_mappingList->InsertColumn(5, _("Key"), wxLIST_FORMAT_LEFT, 0); // скрытая
  mapSizer->Add(m_mappingList, 1, wxEXPAND | wxALL, FromDIP(5));

  // Кнопки управления
  wxBoxSizer *manageSizer = new wxBoxSizer(wxHORIZONTAL);
  m_removeBtn = new wxButton(mapBox, wxID_ANY, _("Remove mapping"));
  m_ignoreBtn = new wxButton(mapBox, wxID_ANY, _("Ignore"));
  m_unignoreBtn = new wxButton(mapBox, wxID_ANY, _("Unignore"));
  m_removeBtn->Enable(false);
  m_ignoreBtn->Enable(false);
  m_unignoreBtn->Enable(false);

  manageSizer->Add(m_removeBtn, 0, wxALL, FromDIP(5));
  manageSizer->Add(m_ignoreBtn, 0, wxALL, FromDIP(5));
  manageSizer->Add(m_unignoreBtn, 0, wxALL, FromDIP(5));
  mapSizer->Add(manageSizer, 0, wxALIGN_CENTER);

  mainSizer->Add(mapSizer, 1, wxEXPAND | wxALL, FromDIP(5));

  // ---- Кнопка Close ----
  wxSizer *closeSizer = CreateButtonSizer(wxCANCEL);
  if (closeSizer) {
    wxWindowList &children = GetChildren();
    for (wxWindow *child : children) {
      wxButton *btn = wxDynamicCast(child, wxButton);
      if (btn && btn->GetId() == wxID_CANCEL) {
        btn->SetLabel(_("Close"));
        break;
      }
    }
    mainSizer->Add(closeSizer, 0, wxALL | wxALIGN_RIGHT, FromDIP(10));
  }

  SetSizerAndFit(mainSizer);
  CentreOnParent();

  // Заполнение списков
  PopulatePlaylistChannels();
  PopulateEpgChannels();
  PopulateMappings();
  AdjustMappingColumns();

  // Привязка событий
  m_playlistList->Bind(wxEVT_LIST_ITEM_SELECTED,
                       &ManualMappingDialog::OnPlaylistSelected, this);
  m_epgList->Bind(wxEVT_LIST_ITEM_SELECTED, &ManualMappingDialog::OnEpgSelected,
                  this);
  m_mappingList->Bind(wxEVT_LIST_ITEM_SELECTED,
                      &ManualMappingDialog::OnMappingSelected, this);
  m_addBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnAddMapping, this);
  m_removeBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnRemoveMapping, this);
  m_ignoreBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnIgnore, this);
  m_unignoreBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnUnignore, this);

  Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) {
    AdjustMappingColumns();
    evt.Skip();
  });

  HighlightPreselected();
  UpdateButtons();
}

ManualMappingDialog::~ManualMappingDialog() {}

void ManualMappingDialog::SetPreselectedChannel(const Channel &channel) {
  m_preselectedTvgId = channel.getTvgId();
  m_preselectedChannelName = channel.getName();
  HighlightPreselected();
}

void ManualMappingDialog::HighlightPreselected() {
  if (m_preselectedTvgId.empty() && m_preselectedChannelName.empty())
    return;

  bool foundInPlaylist = false;
  if (!m_preselectedTvgId.empty()) {
    for (long i = 0; i < m_playlistList->GetItemCount(); ++i) {
      wxString tvgId = m_playlistList->GetItemText(i, 1);
      if (tvgId == m_preselectedTvgId) {
        m_playlistList->SetItemState(i, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
        m_playlistList->EnsureVisible(i);
        m_selectedPlaylistTvgId = m_preselectedTvgId;
        m_selectedPlaylistName =
            m_playlistList->GetItemText(i, 0).ToUTF8().data();
        foundInPlaylist = true;
        break;
      }
    }
  }
  if (!foundInPlaylist && !m_preselectedChannelName.empty()) {
    for (long i = 0; i < m_playlistList->GetItemCount(); ++i) {
      wxString name = m_playlistList->GetItemText(i, 0);
      if (name == m_preselectedChannelName) {
        m_playlistList->SetItemState(i, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
        m_playlistList->EnsureVisible(i);
        m_selectedPlaylistTvgId =
            m_playlistList->GetItemText(i, 1).ToUTF8().data();
        m_selectedPlaylistName = m_preselectedChannelName;
        foundInPlaylist = true;
        break;
      }
    }
  }

  std::vector<std::string> keysToTry;
  if (!m_preselectedTvgId.empty())
    keysToTry.push_back(m_preselectedTvgId);
  if (!m_preselectedChannelName.empty()) {
    keysToTry.push_back("name:" +
                        m_epgMgr->NormalizeName(m_preselectedChannelName));
    keysToTry.push_back("name:" + m_preselectedChannelName);
  }

  bool foundInMapping = false;
  for (const auto &key : keysToTry) {
    for (long i = 0; i < m_mappingList->GetItemCount(); ++i) {
      wxString keyWx = m_mappingList->GetItemText(i, 5);
      if (keyWx == key) {
        m_mappingList->SetItemState(i, wxLIST_STATE_SELECTED,
                                    wxLIST_STATE_SELECTED);
        m_mappingList->EnsureVisible(i);
        m_selectedMappingIndex = i;
        foundInMapping = true;
        break;
      }
    }
    if (foundInMapping)
      break;
  }

  UpdateButtons();
}

void ManualMappingDialog::PopulatePlaylistChannels() {
  m_playlistList->DeleteAllItems();
  if (!m_mainFrame)
    return;

  auto channels = m_mainFrame->GetCurrentChannels();
  long idx = 0;
  for (const auto &ch : channels) {
    wxString name = wxString::FromUTF8(ch.getName());
    wxString tvgId = wxString::FromUTF8(ch.getTvgId());
    long item = m_playlistList->InsertItem(idx, name);
    m_playlistList->SetItem(item, 1, tvgId);
    m_playlistList->SetItemData(item, idx);
    ++idx;
  }
}

void ManualMappingDialog::PopulateEpgChannels() {
  m_epgList->DeleteAllItems();
  if (!m_epgMgr)
    return;

  auto epgChannels = m_epgMgr->GetAllEpgChannels();
  long idx = 0;
  for (const auto &pair : epgChannels) {
    wxString id = wxString::FromUTF8(pair.first);
    wxString name = wxString::FromUTF8(pair.second);
    long item = m_epgList->InsertItem(idx, id);
    m_epgList->SetItem(item, 1, name);
    m_epgList->SetItemData(item, idx);
    ++idx;
  }
}

void ManualMappingDialog::PopulateMappings() {
  m_mappingList->DeleteAllItems();
  if (!m_epgMgr || !m_mainFrame)
    return;

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty())
    return;

  auto channels = m_mainFrame->GetCurrentChannels();
  long idx = 0;
  for (const auto &ch : channels) {
    std::string tvgId = ch.getTvgId();
    std::string name = ch.getName();
    if (tvgId.empty() && name.empty())
      continue;

    std::string epgId;
    bool isManual = false;
    std::string usedKey;

    if (!tvgId.empty() &&
        m_epgMgr->GetMappingEntry(playlistId, tvgId, epgId, isManual)) {
      usedKey = tvgId;
    } else if (!name.empty() &&
               m_epgMgr->GetMappingEntry(
                   playlistId, "name:" + m_epgMgr->NormalizeName(name), epgId,
                   isManual)) {
      usedKey = "name:" + m_epgMgr->NormalizeName(name);
    } else if (!name.empty() &&
               m_epgMgr->GetMappingEntry(playlistId, "name:" + name, epgId,
                                         isManual)) {
      usedKey = "name:" + name;
    }

    if (epgId.empty())
      continue;

    wxString status;
    if (isManual) {
      status = "Manual";
    } else {
      bool ignored = m_epgMgr->IsIgnored(playlistId, usedKey);
      status = ignored ? "Ignored" : "Auto";
    }

    wxString tvgIdWx = wxString::FromUTF8(tvgId);
    wxString chNameWx = wxString::FromUTF8(name);
    wxString epgIdWx = wxString::FromUTF8(epgId);
    wxString epgNameWx = wxString::FromUTF8(m_epgMgr->GetEpgName(epgId));
    wxString keyWx = wxString::FromUTF8(usedKey);

    long item = m_mappingList->InsertItem(idx, tvgIdWx);
    m_mappingList->SetItem(item, 1, chNameWx);
    m_mappingList->SetItem(item, 2, epgIdWx);
    m_mappingList->SetItem(item, 3, epgNameWx);
    m_mappingList->SetItem(item, 4, status);
    m_mappingList->SetItem(item, 5, keyWx);
    m_mappingList->SetItemData(item, idx);
    ++idx;
  }
}

void ManualMappingDialog::OnAddMapping(wxCommandEvent &) {
  if (m_selectedPlaylistTvgId.empty() && m_selectedPlaylistName.empty())
    return;
  if (!m_epgMgr || !m_mainFrame)
    return;

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    wxMessageBox(_("No current playlist."), _("Error"), wxOK | wxICON_ERROR,
                 this);
    return;
  }

  m_epgMgr->SetManualMapping(playlistId, m_selectedPlaylistTvgId,
                             m_selectedEpgId, m_selectedPlaylistName);

  PopulateMappings();
  SelectMapping(m_selectedPlaylistTvgId, m_selectedEpgId);

  m_selectedPlaylistTvgId.clear();
  m_selectedPlaylistName.clear();
  m_selectedEpgId.clear();
  UpdateButtons();
}

void ManualMappingDialog::OnRemoveMapping(wxCommandEvent &) {
  if (m_selectedMappingIndex == -1)
    return;
  wxString keyWx = m_mappingList->GetItemText(m_selectedMappingIndex, 5);
  if (keyWx.IsEmpty())
    return;
  std::string key = keyWx.ToUTF8().data();

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    wxMessageBox(_("No current playlist."), _("Error"), wxOK | wxICON_ERROR,
                 this);
    return;
  }

  m_epgMgr->RemoveMappingEntry(playlistId, key);
  PopulateMappings();
  m_selectedMappingIndex = -1;
  UpdateButtons();
}

void ManualMappingDialog::OnIgnore(wxCommandEvent &) {
  if (m_selectedMappingIndex == -1)
    return;

  wxString status = m_mappingList->GetItemText(m_selectedMappingIndex, 4);
  if (status == "Manual") {
    wxMessageBox(_("Cannot ignore a manual mapping."), _("Info"),
                 wxOK | wxICON_INFORMATION, this);
    return;
  }

  wxString keyWx = m_mappingList->GetItemText(m_selectedMappingIndex, 5);
  if (keyWx.IsEmpty()) {
    wxMessageBox(_("Internal error: missing mapping key."), _("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }
  std::string key = keyWx.ToUTF8().data();

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    wxMessageBox(_("No current playlist."), _("Error"), wxOK | wxICON_ERROR,
                 this);
    return;
  }

  m_epgMgr->IgnoreAutoMapping(playlistId, key);
  PopulateMappings();
  m_selectedMappingIndex = -1;
  UpdateButtons();
}

void ManualMappingDialog::OnUnignore(wxCommandEvent &) {
  if (m_selectedMappingIndex == -1)
    return;

  wxString status = m_mappingList->GetItemText(m_selectedMappingIndex, 4);
  if (status != "Ignored") {
    wxMessageBox(_("Only ignored mappings can be unignored."), _("Info"),
                 wxOK | wxICON_INFORMATION, this);
    return;
  }

  wxString keyWx = m_mappingList->GetItemText(m_selectedMappingIndex, 5);
  if (keyWx.IsEmpty()) {
    wxMessageBox(_("Internal error: missing mapping key."), _("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }
  std::string key = keyWx.ToUTF8().data();

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    wxMessageBox(_("No current playlist."), _("Error"), wxOK | wxICON_ERROR,
                 this);
    return;
  }

  m_epgMgr->UnignoreAutoMapping(playlistId, key);
  PopulateMappings();
  m_selectedMappingIndex = -1;
  UpdateButtons();
}

void ManualMappingDialog::OnPlaylistSelected(wxListEvent &event) {
  long idx = event.GetIndex();
  if (idx == -1)
    return;
  m_selectedPlaylistTvgId = m_playlistList->GetItemText(idx, 1).ToUTF8().data();
  m_selectedPlaylistName = m_playlistList->GetItemText(idx, 0).ToUTF8().data();

  for (long i = 0; i < m_epgList->GetItemCount(); ++i) {
    m_epgList->SetItemState(i, 0, wxLIST_STATE_SELECTED);
  }
  for (long i = 0; i < m_mappingList->GetItemCount(); ++i) {
    m_mappingList->SetItemState(i, 0, wxLIST_STATE_SELECTED);
  }

  std::string epgId;
  bool isManual = false;
  if (m_epgMgr->GetMappingEntry(m_mainFrame->GetCurrentPlaylistId(),
                                m_selectedPlaylistTvgId, epgId, isManual)) {
    if (isManual) {
      HighlightEpgChannel(epgId);
    }
    HighlightMapping(m_selectedPlaylistTvgId);
  }

  UpdateButtons();
}

void ManualMappingDialog::OnEpgSelected(wxListEvent &event) {
  long idx = event.GetIndex();
  if (idx == -1)
    return;
  m_selectedEpgId = m_epgList->GetItemText(idx, 0).ToUTF8().data();
  UpdateButtons();
}

void ManualMappingDialog::OnMappingSelected(wxListEvent &event) {
  m_selectedMappingIndex = event.GetIndex();
  UpdateButtons();
}

void ManualMappingDialog::UpdateButtons() {
  bool hasPlaylist =
      !m_selectedPlaylistTvgId.empty() || !m_selectedPlaylistName.empty();
  bool hasEpg = !m_selectedEpgId.empty();
  m_addBtn->Enable(hasPlaylist && hasEpg);

  bool hasSelection = (m_selectedMappingIndex != -1);
  m_removeBtn->Enable(hasSelection);

  if (hasSelection && m_mappingList) {
    wxString status = m_mappingList->GetItemText(m_selectedMappingIndex, 4);
    m_ignoreBtn->Enable(status == "Auto");
    m_unignoreBtn->Enable(status == "Ignored");
  } else {
    m_ignoreBtn->Enable(false);
    m_unignoreBtn->Enable(false);
  }
}

void ManualMappingDialog::SelectMapping(const std::string &tvgId,
                                        const std::string &epgId) {
  for (long i = 0; i < m_mappingList->GetItemCount(); ++i) {
    wxString t = m_mappingList->GetItemText(i, 0);
    wxString e = m_mappingList->GetItemText(i, 2);
    if (t == tvgId && e == epgId) {
      m_mappingList->SetItemState(i, wxLIST_STATE_SELECTED,
                                  wxLIST_STATE_SELECTED);
      m_mappingList->EnsureVisible(i);
      break;
    }
  }
}

void ManualMappingDialog::HighlightEpgChannel(const std::string &epgId) {
  for (long i = 0; i < m_epgList->GetItemCount(); ++i) {
    wxString itemEpgId = m_epgList->GetItemText(i, 0);
    if (itemEpgId == epgId) {
      m_epgList->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
      m_epgList->EnsureVisible(i);
      break;
    }
  }
}

void ManualMappingDialog::HighlightMapping(const std::string &tvgId) {
  for (long i = 0; i < m_mappingList->GetItemCount(); ++i) {
    wxString itemTvg = m_mappingList->GetItemText(i, 0);
    if (itemTvg == tvgId) {
      m_mappingList->SetItemState(i, wxLIST_STATE_SELECTED,
                                  wxLIST_STATE_SELECTED);
      m_mappingList->EnsureVisible(i);
      break;
    }
  }
}

void ManualMappingDialog::AdjustMappingColumns() {
  if (!m_mappingList)
    return;
  int totalWidth;
  m_mappingList->GetClientSize(&totalWidth, nullptr);
  if (totalWidth <= 0)
    return;

  int colWidths[5] = {15, 30, 15, 25, 15};
  int sum = 0;
  for (int w : colWidths)
    sum += w;
  int used = 0;
  for (int i = 0; i < 5; ++i) {
    int width = totalWidth * colWidths[i] / sum;
    if (i == 4)
      width = totalWidth - used;
    m_mappingList->SetColumnWidth(i, width);
    used += width;
  }
  m_mappingList->SetColumnWidth(5, 0);
}
