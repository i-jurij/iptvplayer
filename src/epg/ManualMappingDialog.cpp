#include "ManualMappingDialog.h"
#include "Channel.h"
#include "MainFrame.h"
#include "epg/EPGManager.h"
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

ManualMappingDialog::ManualMappingDialog(wxWindow *parent, EPGManager *epgMgr,
                                         MainFrame *mainFrame)
    : wxDialog(parent, wxID_ANY, "Manual Mapping", wxDefaultPosition,
               wxSize(950, 650), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_epgMgr(epgMgr), m_mainFrame(mainFrame), m_selectedMappingIndex(-1) {

  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // Верхняя часть: два списка
  wxBoxSizer *topSizer = new wxBoxSizer(wxHORIZONTAL);

  // Левый список: каналы плейлиста
  wxStaticBox *leftBox = new wxStaticBox(this, wxID_ANY, "Playlist Channels");
  wxStaticBoxSizer *leftSizer = new wxStaticBoxSizer(leftBox, wxVERTICAL);
  m_playlistList = new wxListCtrl(leftBox, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  m_playlistList->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 200);
  m_playlistList->InsertColumn(1, "tvg-id", wxLIST_FORMAT_LEFT, 150);
  leftSizer->Add(m_playlistList, 1, wxEXPAND | wxALL, 5);
  topSizer->Add(leftSizer, 1, wxEXPAND | wxALL, 5);

  // Правый список: EPG-каналы
  wxStaticBox *rightBox = new wxStaticBox(this, wxID_ANY, "EPG Channels");
  wxStaticBoxSizer *rightSizer = new wxStaticBoxSizer(rightBox, wxVERTICAL);
  m_epgList = new wxListCtrl(rightBox, wxID_ANY, wxDefaultPosition,
                             wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  m_epgList->InsertColumn(0, "ID", wxLIST_FORMAT_LEFT, 150);
  m_epgList->InsertColumn(1, "Display Name", wxLIST_FORMAT_LEFT, 200);
  rightSizer->Add(m_epgList, 1, wxEXPAND | wxALL, 5);
  topSizer->Add(rightSizer, 1, wxEXPAND | wxALL, 5);

  mainSizer->Add(topSizer, 2, wxEXPAND);

  // Кнопки Add/Remove/Ignore/Unignore
  wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
  m_addBtn = new wxButton(this, wxID_ANY, "Add mapping ->");
  m_removeBtn = new wxButton(this, wxID_ANY, "Remove mapping");
  m_ignoreBtn = new wxButton(this, wxID_ANY, "Ignore");
  m_unignoreBtn = new wxButton(this, wxID_ANY, "Unignore");

  m_addBtn->Enable(false);
  m_removeBtn->Enable(false);
  m_ignoreBtn->Enable(false);
  m_unignoreBtn->Enable(false);

  btnSizer->Add(m_addBtn, 0, wxALL, 5);
  btnSizer->Add(m_removeBtn, 0, wxALL, 5);
  btnSizer->Add(m_ignoreBtn, 0, wxALL, 5);
  btnSizer->Add(m_unignoreBtn, 0, wxALL, 5);
  mainSizer->Add(btnSizer, 0, wxALIGN_CENTER);

  // Нижний список: текущие маппинги (все)
  wxStaticBox *mapBox = new wxStaticBox(this, wxID_ANY, "Current Mappings");
  wxStaticBoxSizer *mapSizer = new wxStaticBoxSizer(mapBox, wxVERTICAL);
  m_mappingList = new wxListCtrl(mapBox, wxID_ANY, wxDefaultPosition,
                                 wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  m_mappingList->InsertColumn(0, "Playlist tvg-id", wxLIST_FORMAT_LEFT, 150);
  m_mappingList->InsertColumn(1, "Playlist Name", wxLIST_FORMAT_LEFT, 200);
  m_mappingList->InsertColumn(2, "EPG ID", wxLIST_FORMAT_LEFT, 150);
  m_mappingList->InsertColumn(3, "EPG Name", wxLIST_FORMAT_LEFT, 200);
  m_mappingList->InsertColumn(4, "Status", wxLIST_FORMAT_LEFT, 80);
  mapSizer->Add(m_mappingList, 1, wxEXPAND | wxALL, 5);
  mainSizer->Add(mapSizer, 1, wxEXPAND | wxALL, 5);

  // Кнопка Close (OK/Cancel)
  wxSizer *closeSizer = CreateButtonSizer(wxOK | wxCANCEL);
  if (closeSizer)
    mainSizer->Add(closeSizer, 0, wxALL | wxALIGN_RIGHT, 10);

  SetSizerAndFit(mainSizer);
  CentreOnParent();

  // Заполняем списки
  PopulatePlaylistChannels();
  PopulateEpgChannels();
  PopulateMappings();

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

  UpdateButtons();
}

ManualMappingDialog::~ManualMappingDialog() {}

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
    if (tvgId.empty())
      continue;

    std::string epgId;
    bool isManual = false;
    bool hasMapping =
        m_epgMgr->GetMappingEntry(playlistId, tvgId, epgId, isManual);

    if (!hasMapping)
      continue;

    wxString status;
    if (isManual) {
      status = "Manual";
    } else {
      bool ignored = m_epgMgr->IsIgnored(playlistId, tvgId);
      status = ignored ? "Ignored" : "Auto";
    }

    wxString tvgIdWx = wxString::FromUTF8(tvgId);
    wxString chNameWx = wxString::FromUTF8(ch.getName());
    wxString epgIdWx = wxString::FromUTF8(epgId);
    wxString epgNameWx = wxString::FromUTF8(m_epgMgr->GetEpgName(epgId));

    long item = m_mappingList->InsertItem(idx, tvgIdWx);
    m_mappingList->SetItem(item, 1, chNameWx);
    m_mappingList->SetItem(item, 2, epgIdWx);
    m_mappingList->SetItem(item, 3, epgNameWx);
    m_mappingList->SetItem(item, 4, status);
    m_mappingList->SetItemData(item, idx);
    ++idx;
  }
}

void ManualMappingDialog::OnAddMapping(wxCommandEvent &) {
  if (m_selectedPlaylistTvgId.empty() || m_selectedEpgId.empty())
    return;
  if (!m_epgMgr || !m_mainFrame)
    return;

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    wxMessageBox("No current playlist.", "Error", wxOK | wxICON_ERROR, this);
    return;
  }

  m_epgMgr->SetManualMapping(m_selectedPlaylistTvgId, m_selectedEpgId);

  PopulateMappings();
  SelectMapping(m_selectedPlaylistTvgId, m_selectedEpgId);

  m_selectedPlaylistTvgId.clear();
  m_selectedEpgId.clear();
  UpdateButtons();
}

void ManualMappingDialog::OnRemoveMapping(wxCommandEvent &) {
  if (m_selectedMappingIndex == -1)
    return;
  if (!m_mappingList)
    return;

  wxString tvgIdWx = m_mappingList->GetItemText(m_selectedMappingIndex, 0);
  wxString statusWx = m_mappingList->GetItemText(m_selectedMappingIndex, 4);
  std::string tvgId = tvgIdWx.ToUTF8().data();
  if (tvgId.empty())
    return;
  if (!m_epgMgr || !m_mainFrame)
    return;

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    wxMessageBox("No current playlist.", "Error", wxOK | wxICON_ERROR, this);
    return;
  }

  if (statusWx == "Manual") {
    m_epgMgr->RemoveChannelMapping(tvgId);
  } else {
    // Auto или Ignored — удаляем запись полностью
    m_epgMgr->RemoveMappingEntry(playlistId, tvgId);
  }

  PopulateMappings();
  m_selectedMappingIndex = -1;
  UpdateButtons();
}

void ManualMappingDialog::OnIgnore(wxCommandEvent &) {
  if (m_selectedMappingIndex == -1)
    return;
  if (!m_mappingList)
    return;

  wxString tvgIdWx = m_mappingList->GetItemText(m_selectedMappingIndex, 0);
  std::string tvgId = tvgIdWx.ToUTF8().data();
  if (tvgId.empty())
    return;
  if (!m_epgMgr || !m_mainFrame)
    return;

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    wxMessageBox("No current playlist.", "Error", wxOK | wxICON_ERROR, this);
    return;
  }

  m_epgMgr->IgnoreAutoMapping(playlistId, tvgId);
  PopulateMappings();
  m_selectedMappingIndex = -1;
  UpdateButtons();
}

void ManualMappingDialog::OnUnignore(wxCommandEvent &) {
  if (m_selectedMappingIndex == -1)
    return;
  if (!m_mappingList)
    return;

  wxString tvgIdWx = m_mappingList->GetItemText(m_selectedMappingIndex, 0);
  std::string tvgId = tvgIdWx.ToUTF8().data();
  if (tvgId.empty())
    return;
  if (!m_epgMgr || !m_mainFrame)
    return;

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    wxMessageBox("No current playlist.", "Error", wxOK | wxICON_ERROR, this);
    return;
  }

  m_epgMgr->UnignoreAutoMapping(playlistId, tvgId);
  PopulateMappings();
  m_selectedMappingIndex = -1;
  UpdateButtons();
}

void ManualMappingDialog::OnPlaylistSelected(wxListEvent &event) {
  long idx = event.GetIndex();
  if (idx == -1)
    return;
  m_selectedPlaylistTvgId = m_playlistList->GetItemText(idx, 1).ToUTF8().data();

  // Если для этого tvgId есть ручной маппинг, подсветим в EPG и mapping
  std::string epgId;
  bool isManual = false;
  if (m_epgMgr->GetMappingEntry(m_mainFrame->GetCurrentPlaylistId(),
                                m_selectedPlaylistTvgId, epgId, isManual) &&
      isManual) {
    HighlightEpgChannel(epgId);
    HighlightMapping(m_selectedPlaylistTvgId);
  } else {
    // Снимаем подсветку в EPG
    for (long i = 0; i < m_epgList->GetItemCount(); ++i) {
      m_epgList->SetItemState(i, 0, wxLIST_STATE_SELECTED);
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
  bool hasPlaylist = !m_selectedPlaylistTvgId.empty();
  bool hasEpg = !m_selectedEpgId.empty();
  m_addBtn->Enable(hasPlaylist && hasEpg);

  bool hasSelection = (m_selectedMappingIndex != -1);
  m_removeBtn->Enable(hasSelection);

  // Для Ignore/Unignore анализируем статус выбранной записи
  bool enableIgnore = false;
  bool enableUnignore = false;
  if (hasSelection && m_mappingList) {
    wxString status = m_mappingList->GetItemText(m_selectedMappingIndex, 4);
    if (status == "Auto")
      enableIgnore = true;
    else if (status == "Ignored")
      enableUnignore = true;
  }
  m_ignoreBtn->Enable(enableIgnore);
  m_unignoreBtn->Enable(enableUnignore);
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
