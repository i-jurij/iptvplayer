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
               wxSize(900, 600), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
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

  // Кнопки Add/Remove
  wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
  m_addBtn = new wxButton(this, wxID_ANY, "Add mapping ->");
  m_removeBtn = new wxButton(this, wxID_ANY, "Remove mapping");
  m_addBtn->Enable(false);
  m_removeBtn->Enable(false);
  btnSizer->Add(m_addBtn, 0, wxALL, 5);
  btnSizer->Add(m_removeBtn, 0, wxALL, 5);
  mainSizer->Add(btnSizer, 0, wxALIGN_CENTER);

  // Нижний список: текущие маппинги
  wxStaticBox *mapBox =
      new wxStaticBox(this, wxID_ANY, "Current Manual Mappings");
  wxStaticBoxSizer *mapSizer = new wxStaticBoxSizer(mapBox, wxVERTICAL);
  m_mappingList = new wxListCtrl(mapBox, wxID_ANY, wxDefaultPosition,
                                 wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  m_mappingList->InsertColumn(0, "Playlist tvg-id", wxLIST_FORMAT_LEFT, 150);
  m_mappingList->InsertColumn(1, "Playlist Name", wxLIST_FORMAT_LEFT, 200);
  m_mappingList->InsertColumn(2, "EPG ID", wxLIST_FORMAT_LEFT, 150);
  m_mappingList->InsertColumn(3, "EPG Name", wxLIST_FORMAT_LEFT, 200);
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

  // Привязка событий через Bind (без таблицы)
  m_playlistList->Bind(wxEVT_LIST_ITEM_SELECTED,
                       &ManualMappingDialog::OnPlaylistSelected, this);
  m_epgList->Bind(wxEVT_LIST_ITEM_SELECTED, &ManualMappingDialog::OnEpgSelected,
                  this);
  m_mappingList->Bind(wxEVT_LIST_ITEM_SELECTED,
                      &ManualMappingDialog::OnMappingSelected, this);
  m_addBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnAddMapping, this);
  m_removeBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnRemoveMapping, this);

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
    bool isManual = false;
    std::string epgId;
    if (m_epgMgr->GetMappingEntry(playlistId, tvgId, epgId, isManual) &&
        isManual) {
      wxString tvgIdWx = wxString::FromUTF8(tvgId);
      wxString chNameWx = wxString::FromUTF8(ch.getName());
      wxString epgIdWx = wxString::FromUTF8(epgId);
      wxString epgNameWx = wxString::FromUTF8(m_epgMgr->GetEpgName(epgId));
      long item = m_mappingList->InsertItem(idx, tvgIdWx);
      m_mappingList->SetItem(item, 1, chNameWx);
      m_mappingList->SetItem(item, 2, epgIdWx);
      m_mappingList->SetItem(item, 3, epgNameWx);
      m_mappingList->SetItemData(item, idx);
      ++idx;
    }
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

  // Сохраняем ID для подсветки после обновления
  std::string addedTvg = m_selectedPlaylistTvgId;
  std::string addedEpg = m_selectedEpgId;

  m_epgMgr->SetManualMapping(m_selectedPlaylistTvgId, m_selectedEpgId);
  PopulateMappings();
  SelectMapping(addedTvg, addedEpg);

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

  m_epgMgr->RemoveManualMapping(playlistId, tvgId);
  PopulateMappings();
  m_selectedMappingIndex = -1;
  UpdateButtons();
}

void ManualMappingDialog::OnPlaylistSelected(wxListEvent &event) {
  long idx = event.GetIndex();
  if (idx == -1)
    return;
  m_selectedPlaylistTvgId = m_playlistList->GetItemText(idx, 1).ToUTF8().data();
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
  m_removeBtn->Enable(m_selectedMappingIndex != -1);
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
