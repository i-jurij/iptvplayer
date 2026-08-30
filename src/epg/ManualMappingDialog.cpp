#include "ManualMappingDialog.h"
#include "Channel.h"
#include "MainFrame.h"
#include "epg/EPGManager.h"
#include <algorithm>
#include <cctype>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

ManualMappingDialog::ManualMappingDialog(wxWindow *parent, EPGManager *epgMgr,
                                         MainFrame *mainFrame)
    : wxDialog(parent, wxID_ANY, _("Manual Mapping"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_epgMgr(epgMgr), m_mainFrame(mainFrame), m_playlistSortCol(-1),
      m_playlistSortAsc(true), m_epgSortCol(-1), m_epgSortAsc(true),
      m_mappingSortCol(-1), m_mappingSortAsc(true), m_selectedMappingIndex(-1),
      m_preselectedTvgId(""), m_preselectedChannelName("") {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // ---- Верхняя часть: два списка с поиском ----
  wxBoxSizer *topSizer = new wxBoxSizer(wxHORIZONTAL);

  // Левый блок: Playlist Channels + поиск
  wxStaticBox *leftBox =
      new wxStaticBox(this, wxID_ANY, _("Playlist Channels"));
  wxStaticBoxSizer *leftSizer = new wxStaticBoxSizer(leftBox, wxVERTICAL);

  // Строка поиска
  wxBoxSizer *searchLeftSizer = new wxBoxSizer(wxHORIZONTAL);
  searchLeftSizer->Add(new wxStaticText(leftBox, wxID_ANY, _("Search:")), 0,
                       wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_playlistSearch =
      new wxTextCtrl(leftBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxDefaultSize, wxTE_PROCESS_ENTER);
  searchLeftSizer->Add(m_playlistSearch, 1, wxEXPAND);
  leftSizer->Add(searchLeftSizer, 0, wxEXPAND | wxALL, FromDIP(5));

  m_playlistList = new wxListCtrl(leftBox, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  m_playlistList->InsertColumn(0, _("Name"), wxLIST_FORMAT_LEFT, FromDIP(200));
  m_playlistList->InsertColumn(1, _("tvg-id"), wxLIST_FORMAT_LEFT,
                               FromDIP(150));
  leftSizer->Add(m_playlistList, 1, wxEXPAND | wxALL, FromDIP(5));
  topSizer->Add(leftSizer, 1, wxEXPAND | wxALL, FromDIP(5));

  // Правый блок: EPG Channels + поиск
  wxStaticBox *rightBox = new wxStaticBox(this, wxID_ANY, _("EPG Channels"));
  wxStaticBoxSizer *rightSizer = new wxStaticBoxSizer(rightBox, wxVERTICAL);

  wxBoxSizer *searchRightSizer = new wxBoxSizer(wxHORIZONTAL);
  searchRightSizer->Add(new wxStaticText(rightBox, wxID_ANY, _("Search:")), 0,
                        wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_epgSearch =
      new wxTextCtrl(rightBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxDefaultSize, wxTE_PROCESS_ENTER);
  searchRightSizer->Add(m_epgSearch, 1, wxEXPAND);
  rightSizer->Add(searchRightSizer, 0, wxEXPAND | wxALL, FromDIP(5));

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
  m_mappingList->InsertColumn(5, _("Key"), wxLIST_FORMAT_LEFT, 0);
  mapSizer->Add(m_mappingList, 1, wxEXPAND | wxALL, FromDIP(5));

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

  // Загрузка данных
  LoadPlaylistChannels();
  LoadEpgChannels();
  LoadMappings();
  AdjustMappingColumns();

  // Привязка событий выделения
  m_playlistList->Bind(wxEVT_LIST_ITEM_SELECTED,
                       &ManualMappingDialog::OnPlaylistSelected, this);
  m_epgList->Bind(wxEVT_LIST_ITEM_SELECTED, &ManualMappingDialog::OnEpgSelected,
                  this);
  m_mappingList->Bind(wxEVT_LIST_ITEM_SELECTED,
                      &ManualMappingDialog::OnMappingSelected, this);

  // Привязка событий кнопок
  m_addBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnAddMapping, this);
  m_removeBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnRemoveMapping, this);
  m_ignoreBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnIgnore, this);
  m_unignoreBtn->Bind(wxEVT_BUTTON, &ManualMappingDialog::OnUnignore, this);

  // Привязка событий клика по заголовкам колонок
  m_playlistList->Bind(wxEVT_LIST_COL_CLICK,
                       &ManualMappingDialog::OnPlaylistColClick, this);
  m_epgList->Bind(wxEVT_LIST_COL_CLICK, &ManualMappingDialog::OnEpgColClick,
                  this);
  m_mappingList->Bind(wxEVT_LIST_COL_CLICK,
                      &ManualMappingDialog::OnMappingColClick, this);

  // Привязка событий поиска
  m_playlistSearch->Bind(wxEVT_TEXT, &ManualMappingDialog::OnPlaylistSearch,
                         this);
  m_epgSearch->Bind(wxEVT_TEXT, &ManualMappingDialog::OnEpgSearch, this);

  // Изменение размера
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

// ------------------- Загрузка / обновление данных -------------------

void ManualMappingDialog::LoadPlaylistChannels() {
  m_playlistItems.clear();
  if (!m_mainFrame)
    return;
  auto channels = m_mainFrame->GetCurrentChannels();
  for (const auto &ch : channels) {
    PlaylistItem item;
    item.tvgId = ch.getTvgId();
    item.name = ch.getName();
    m_playlistItems.push_back(item);
  }
  // Сортируем согласно текущим настройкам сортировки (если есть)
  if (m_playlistSortCol >= 0)
    SortPlaylist();
  RefreshPlaylistList();
}

void ManualMappingDialog::RefreshPlaylistList() {
  m_playlistList->DeleteAllItems();
  long idx = 0;
  for (const auto &item : m_playlistItems) {
    if (!PlaylistMatchesFilter(item))
      continue;
    wxString name = wxString::FromUTF8(item.name);
    wxString tvgId = wxString::FromUTF8(item.tvgId);
    long itemIndex = m_playlistList->InsertItem(idx, name);
    m_playlistList->SetItem(itemIndex, 1, tvgId);
    m_playlistList->SetItemData(itemIndex, idx);
    ++idx;
  }
  // Восстанавливаем выделение, если выбранный элемент всё ещё видим
  if (!m_selectedPlaylistTvgId.empty() || !m_selectedPlaylistName.empty())
    HighlightPlaylistChannel(m_selectedPlaylistTvgId, m_selectedPlaylistName);
}

void ManualMappingDialog::LoadEpgChannels() {
  m_epgItems.clear();
  if (!m_epgMgr)
    return;
  auto epgChannels = m_epgMgr->GetAllEpgChannels();
  for (const auto &pair : epgChannels) {
    EpgItem item;
    item.id = pair.first;
    item.name = pair.second;
    m_epgItems.push_back(item);
  }
  if (m_epgSortCol >= 0)
    SortEpg();
  RefreshEpgList();
}

void ManualMappingDialog::RefreshEpgList() {
  m_epgList->DeleteAllItems();
  long idx = 0;
  for (const auto &item : m_epgItems) {
    if (!EpgMatchesFilter(item))
      continue;
    wxString id = wxString::FromUTF8(item.id);
    wxString name = wxString::FromUTF8(item.name);
    long itemIndex = m_epgList->InsertItem(idx, id);
    m_epgList->SetItem(itemIndex, 1, name);
    m_epgList->SetItemData(itemIndex, idx);
    ++idx;
  }
  if (!m_selectedEpgId.empty())
    HighlightEpgChannel(m_selectedEpgId);
}

void ManualMappingDialog::LoadMappings() {
  m_mappingItems.clear();
  if (!m_epgMgr || !m_mainFrame)
    return;

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    RefreshMappingList();
    m_selectedMappingIndex = -1;
    m_selectedMappingKey.clear();
    UpdateButtons();
    return;
  }

  auto allMappings = m_epgMgr->GetAllMappingsForPlaylist(playlistId);
  auto channels = m_mainFrame->GetCurrentChannels();

  for (const auto &ch : channels) {
    std::string tvgId = ch.getTvgId();
    std::string name = ch.getName();
    if (tvgId.empty() && name.empty())
      continue;

    std::string epgId;
    bool isManual = false;
    bool ignored = false;
    std::string usedKey;

    if (!tvgId.empty()) {
      auto it = allMappings.find(tvgId);
      if (it != allMappings.end()) {
        epgId = it->second.epgId;
        isManual = it->second.isManual;
        ignored = it->second.ignored;
        usedKey = tvgId;
      }
    }
    if (epgId.empty() && !name.empty()) {
      std::string normKey = "name:" + m_epgMgr->NormalizeName(name);
      auto it = allMappings.find(normKey);
      if (it != allMappings.end()) {
        epgId = it->second.epgId;
        isManual = it->second.isManual;
        ignored = it->second.ignored;
        usedKey = normKey;
      }
    }
    if (epgId.empty() && !name.empty()) {
      std::string rawKey = "name:" + name;
      auto it = allMappings.find(rawKey);
      if (it != allMappings.end()) {
        epgId = it->second.epgId;
        isManual = it->second.isManual;
        ignored = it->second.ignored;
        usedKey = rawKey;
      }
    }

    if (epgId.empty())
      continue;

    MappingItem item;
    item.playlistTvgId = tvgId;
    item.playlistName = name;
    item.epgId = epgId;
    item.epgName = m_epgMgr->GetEpgName(epgId);
    item.status = isManual ? "Manual" : (ignored ? "Ignored" : "Auto");
    item.key = usedKey;
    m_mappingItems.push_back(item);
  }

  if (m_mappingSortCol >= 0)
    SortMappings();
  RefreshMappingList();
  m_selectedMappingIndex = -1;
  m_selectedMappingKey.clear();
  UpdateButtons();
}

void ManualMappingDialog::RefreshMappingList() {
  m_mappingList->DeleteAllItems();
  long idx = 0;
  for (const auto &item : m_mappingItems) {
    wxString tvgIdWx = wxString::FromUTF8(item.playlistTvgId);
    wxString chNameWx = wxString::FromUTF8(item.playlistName);
    wxString epgIdWx = wxString::FromUTF8(item.epgId);
    wxString epgNameWx = wxString::FromUTF8(item.epgName);
    wxString statusWx = wxString::FromUTF8(item.status);
    wxString keyWx = wxString::FromUTF8(item.key);

    long itemIndex = m_mappingList->InsertItem(idx, tvgIdWx);
    m_mappingList->SetItem(itemIndex, 1, chNameWx);
    m_mappingList->SetItem(itemIndex, 2, epgIdWx);
    m_mappingList->SetItem(itemIndex, 3, epgNameWx);
    m_mappingList->SetItem(itemIndex, 4, statusWx);
    m_mappingList->SetItem(itemIndex, 5, keyWx);
    m_mappingList->SetItemData(itemIndex, idx);
    ++idx;
  }
  AdjustMappingColumns();
  // Восстанавливаем выделение, если ключ известен
  if (!m_selectedMappingKey.empty())
    HighlightMappingByKey(m_selectedMappingKey);
}

// ------------------- Фильтрация -------------------

bool ManualMappingDialog::PlaylistMatchesFilter(
    const PlaylistItem &item) const {
  wxString filter = m_playlistSearch->GetValue().Lower();
  if (filter.IsEmpty())
    return true;
  wxString name = wxString::FromUTF8(item.name).Lower();
  wxString tvgId = wxString::FromUTF8(item.tvgId).Lower();
  return name.Contains(filter) || tvgId.Contains(filter);
}

bool ManualMappingDialog::EpgMatchesFilter(const EpgItem &item) const {
  wxString filter = m_epgSearch->GetValue().Lower();
  if (filter.IsEmpty())
    return true;
  wxString id = wxString::FromUTF8(item.id).Lower();
  wxString name = wxString::FromUTF8(item.name).Lower();
  return id.Contains(filter) || name.Contains(filter);
}

// ------------------- Сортировка -------------------

void ManualMappingDialog::SortPlaylist() {
  if (m_playlistSortCol < 0)
    return;
  std::sort(m_playlistItems.begin(), m_playlistItems.end(),
            [this](const PlaylistItem &a, const PlaylistItem &b) {
              int result = 0;
              if (m_playlistSortCol == 0)
                result = a.name.compare(b.name);
              else if (m_playlistSortCol == 1)
                result = a.tvgId.compare(b.tvgId);
              return m_playlistSortAsc ? (result < 0) : (result > 0);
            });
}

void ManualMappingDialog::SortEpg() {
  if (m_epgSortCol < 0)
    return;
  std::sort(m_epgItems.begin(), m_epgItems.end(),
            [this](const EpgItem &a, const EpgItem &b) {
              int result = 0;
              if (m_epgSortCol == 0)
                result = a.id.compare(b.id);
              else if (m_epgSortCol == 1)
                result = a.name.compare(b.name);
              return m_epgSortAsc ? (result < 0) : (result > 0);
            });
}

void ManualMappingDialog::SortMappings() {
  if (m_mappingSortCol < 0)
    return;
  std::sort(m_mappingItems.begin(), m_mappingItems.end(),
            [this](const MappingItem &a, const MappingItem &b) {
              int result = 0;
              switch (m_mappingSortCol) {
              case 0:
                result = a.playlistTvgId.compare(b.playlistTvgId);
                break;
              case 1:
                result = a.playlistName.compare(b.playlistName);
                break;
              case 2:
                result = a.epgId.compare(b.epgId);
                break;
              case 3:
                result = a.epgName.compare(b.epgName);
                break;
              case 4:
                result = a.status.compare(b.status);
                break;
              default:
                result = 0;
              }
              return m_playlistSortAsc ? (result < 0) : (result > 0);
            });
}

// ------------------- Выделение элементов -------------------

void ManualMappingDialog::HighlightPlaylistChannel(const std::string &tvgId,
                                                   const std::string &name) {
  // Снимаем выделение
  for (long i = 0; i < m_playlistList->GetItemCount(); ++i)
    m_playlistList->SetItemState(i, 0, wxLIST_STATE_SELECTED);

  // Ищем по tvgId среди видимых
  if (!tvgId.empty()) {
    for (long i = 0; i < m_playlistList->GetItemCount(); ++i) {
      if (m_playlistList->GetItemText(i, 1) == tvgId) {
        m_playlistList->SetItemState(i, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
        m_playlistList->EnsureVisible(i);
        return;
      }
    }
  }
  // Ищем по имени
  if (!name.empty()) {
    for (long i = 0; i < m_playlistList->GetItemCount(); ++i) {
      if (m_playlistList->GetItemText(i, 0) == name) {
        m_playlistList->SetItemState(i, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
        m_playlistList->EnsureVisible(i);
        return;
      }
    }
  }
  // Если не найдено, сбрасываем выделение
  m_selectedPlaylistTvgId.clear();
  m_selectedPlaylistName.clear();
}

void ManualMappingDialog::HighlightEpgChannel(const std::string &epgId) {
  for (long i = 0; i < m_epgList->GetItemCount(); ++i)
    m_epgList->SetItemState(i, 0, wxLIST_STATE_SELECTED);

  if (epgId.empty()) {
    m_selectedEpgId.clear();
    return;
  }
  for (long i = 0; i < m_epgList->GetItemCount(); ++i) {
    if (m_epgList->GetItemText(i, 0) == epgId) {
      m_epgList->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
      m_epgList->EnsureVisible(i);
      m_selectedEpgId = epgId;
      return;
    }
  }
  // Если не найдено, сбрасываем
  m_selectedEpgId.clear();
}

void ManualMappingDialog::HighlightMappingByKey(const std::string &key) {
  for (long i = 0; i < m_mappingList->GetItemCount(); ++i)
    m_mappingList->SetItemState(i, 0, wxLIST_STATE_SELECTED);

  m_selectedMappingIndex = -1;
  m_selectedMappingKey.clear();
  if (key.empty())
    return;

  for (long i = 0; i < m_mappingList->GetItemCount(); ++i) {
    if (m_mappingList->GetItemText(i, 5) == key) {
      m_mappingList->SetItemState(i, wxLIST_STATE_SELECTED,
                                  wxLIST_STATE_SELECTED);
      m_mappingList->EnsureVisible(i);
      m_selectedMappingIndex = i;
      m_selectedMappingKey = key;
      return;
    }
  }
  // Если не найдено, сбрасываем
  m_selectedMappingKey.clear();
}

void ManualMappingDialog::SelectPlaylistChannel(const std::string &tvgId,
                                                const std::string &name) {
  // Снимаем выделение с EPG и Mapping
  for (long i = 0; i < m_epgList->GetItemCount(); ++i)
    m_epgList->SetItemState(i, 0, wxLIST_STATE_SELECTED);
  for (long i = 0; i < m_mappingList->GetItemCount(); ++i)
    m_mappingList->SetItemState(i, 0, wxLIST_STATE_SELECTED);

  m_selectedPlaylistTvgId = tvgId;
  m_selectedPlaylistName = name;

  std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
  if (playlistId.empty()) {
    UpdateButtons();
    return;
  }

  std::string key;
  if (!tvgId.empty())
    key = tvgId;
  else if (!name.empty())
    key = "name:" + m_epgMgr->NormalizeName(name);

  std::string epgId;
  bool isManual = false;
  bool found = false;

  if (!key.empty()) {
    found = m_epgMgr->GetMappingEntry(playlistId, key, epgId, isManual);
    if (!found && !name.empty()) {
      std::string rawKey = "name:" + name;
      found = m_epgMgr->GetMappingEntry(playlistId, rawKey, epgId, isManual);
      if (found)
        key = rawKey;
    }
  }

  if (found) {
    HighlightMappingByKey(key);
    if (isManual)
      HighlightEpgChannel(epgId);
    else
      m_selectedEpgId.clear();
  } else {
    m_selectedMappingKey.clear();
    m_selectedMappingIndex = -1;
    m_selectedEpgId.clear();
  }

  UpdateButtons();
}

// ------------------- Обработчики сортировки по заголовкам -------------------

void ManualMappingDialog::OnPlaylistColClick(wxListEvent &event) {
  int col = event.GetColumn();
  if (col == m_playlistSortCol)
    m_playlistSortAsc = !m_playlistSortAsc;
  else {
    m_playlistSortCol = col;
    m_playlistSortAsc = true;
  }

  SortPlaylist();
  RefreshPlaylistList();
  // выделение восстановится в RefreshPlaylistList через
  // HighlightPlaylistChannel
  UpdateButtons();
}

void ManualMappingDialog::OnEpgColClick(wxListEvent &event) {
  int col = event.GetColumn();
  if (col == m_epgSortCol)
    m_epgSortAsc = !m_epgSortAsc;
  else {
    m_epgSortCol = col;
    m_epgSortAsc = true;
  }

  SortEpg();
  RefreshEpgList();
  UpdateButtons();
}

void ManualMappingDialog::OnMappingColClick(wxListEvent &event) {
  int col = event.GetColumn();
  if (col == m_mappingSortCol)
    m_mappingSortAsc = !m_mappingSortAsc;
  else {
    m_mappingSortCol = col;
    m_mappingSortAsc = true;
  }

  SortMappings();
  RefreshMappingList();
  UpdateButtons();
}

// ------------------- Обработчики поиска -------------------

void ManualMappingDialog::OnPlaylistSearch(wxCommandEvent &) {
  RefreshPlaylistList();
  UpdateButtons();
}

void ManualMappingDialog::OnEpgSearch(wxCommandEvent &) {
  RefreshEpgList();
  UpdateButtons();
}

// ------------------- Обработчики выбора элементов -------------------

void ManualMappingDialog::OnPlaylistSelected(wxListEvent &event) {
  long idx = event.GetIndex();
  if (idx == -1)
    return;
  std::string tvgId = m_playlistList->GetItemText(idx, 1).ToUTF8().data();
  std::string name = m_playlistList->GetItemText(idx, 0).ToUTF8().data();
  SelectPlaylistChannel(tvgId, name);
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
  if (m_selectedMappingIndex != -1) {
    m_selectedMappingKey =
        m_mappingList->GetItemText(m_selectedMappingIndex, 5).ToUTF8().data();
  } else {
    m_selectedMappingKey.clear();
  }
  UpdateButtons();
}

// ------------------- Кнопочные обработчики -------------------

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

  LoadMappings();

  // Выделяем созданный маппинг
  std::string key;
  if (!m_selectedPlaylistTvgId.empty())
    key = m_selectedPlaylistTvgId;
  else if (!m_selectedPlaylistName.empty())
    key = "name:" + m_epgMgr->NormalizeName(m_selectedPlaylistName);

  if (!key.empty()) {
    HighlightMappingByKey(key);
    if (!m_selectedEpgId.empty())
      HighlightEpgChannel(m_selectedEpgId);
  }

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
  LoadMappings();
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
  LoadMappings();
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
  LoadMappings();
  UpdateButtons();
}

// ------------------- Вспомогательные методы -------------------

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

void ManualMappingDialog::HighlightPreselected() {
  if (m_preselectedTvgId.empty() && m_preselectedChannelName.empty())
    return;

  bool foundInPlaylist = false;
  std::string foundTvgId, foundName;

  if (!m_preselectedTvgId.empty()) {
    for (long i = 0; i < m_playlistList->GetItemCount(); ++i) {
      if (m_playlistList->GetItemText(i, 1) == m_preselectedTvgId) {
        foundTvgId = m_preselectedTvgId;
        foundName = m_playlistList->GetItemText(i, 0).ToUTF8().data();
        foundInPlaylist = true;
        break;
      }
    }
  }
  if (!foundInPlaylist && !m_preselectedChannelName.empty()) {
    for (long i = 0; i < m_playlistList->GetItemCount(); ++i) {
      if (m_playlistList->GetItemText(i, 0) == m_preselectedChannelName) {
        foundTvgId = m_playlistList->GetItemText(i, 1).ToUTF8().data();
        foundName = m_preselectedChannelName;
        foundInPlaylist = true;
        break;
      }
    }
  }

  if (foundInPlaylist) {
    HighlightPlaylistChannel(foundTvgId, foundName);
    SelectPlaylistChannel(foundTvgId, foundName);
  }

  UpdateButtons();
}
