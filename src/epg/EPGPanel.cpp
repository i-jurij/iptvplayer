#include "EPGPanel.h"
#include "../Channel.h"
#include "../LogControl.h"
#include "Application.h"
#include "EPGManager.h"
#include "EventIDs.h"
#include "MainFrame.h"
#include "SettingsDialog.h"

#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/string.h>

#include <ctime>

std::string EPGPanel::s_lastChannelId;
std::string EPGPanel::s_lastChannelName;
std::string EPGPanel::s_lastPlaylistName;
time_t EPGPanel::s_lastDate = 0;

// =========================================================================
// Модель списка каналов
// =========================================================================
EPGPanel::ChannelListModel::ChannelListModel() {}

void EPGPanel::ChannelListModel::SetChannels(
    const std::vector<Channel> &channels) {
  m_channels = &channels;
  m_filterText.Clear();
  m_filteredChannels = *m_channels;
  Reset(m_filteredChannels.size());
}

void EPGPanel::ChannelListModel::Filter(const wxString &filter) {
  m_filterText = filter;
  m_filteredChannels.clear();

  if (!m_currentSource) {
    Reset(0);
    return;
  }

  if (filter.IsEmpty()) {
    m_filteredChannels = *m_currentSource;
    Reset(m_filteredChannels.size());
    return;
  }

  wxString lowerFilter = filter.Lower();
  wxArrayString parts = wxSplit(lowerFilter, ' ');
  parts.erase(std::remove_if(parts.begin(), parts.end(),
                             [](const wxString &s) { return s.IsEmpty(); }),
              parts.end());

  // Мультисловный поиск
  if (parts.size() > 1) {
    for (const auto &ch : *m_currentSource) {
      wxString name = wxString::FromUTF8(ch.getName()).Lower();
      bool allMatch = true;
      for (const auto &p : parts) {
        if (!name.Contains(p)) {
          allMatch = false;
          break;
        }
      }
      if (allMatch)
        m_filteredChannels.push_back(ch);
    }
    Reset(m_filteredChannels.size());
    return;
  }

  // Одно слово — улучшенный поиск
  const wxString &needle = lowerFilter;
  std::vector<Channel> startsWith;
  std::vector<Channel> contains;

  for (const auto &ch : *m_currentSource) {
    wxString name = wxString::FromUTF8(ch.getName()).Lower();
    if (name.StartsWith(needle)) {
      startsWith.push_back(ch);
    } else if (name.Contains(needle)) {
      contains.push_back(ch);
    }
  }

  m_filteredChannels.reserve(startsWith.size() + contains.size());
  m_filteredChannels.insert(m_filteredChannels.end(), startsWith.begin(),
                            startsWith.end());
  m_filteredChannels.insert(m_filteredChannels.end(), contains.begin(),
                            contains.end());

  // Fuzzy fallback
  if (m_filteredChannels.empty()) {
    for (const auto &ch : *m_currentSource) {
      wxString name = wxString::FromUTF8(ch.getName()).Lower();
      size_t j = 0;
      for (size_t i = 0; i < name.Length() && j < needle.Length(); ++i) {
        if (name[i] == needle[j])
          ++j;
      }
      if (j == needle.Length()) {
        m_filteredChannels.push_back(ch);
      }
    }
  }

  Reset(m_filteredChannels.size());
}

unsigned int EPGPanel::ChannelListModel::GetCount() const {
  return static_cast<unsigned int>(m_filteredChannels.size());
}

void EPGPanel::ChannelListModel::GetValueByRow(wxVariant &variant,
                                               unsigned int row,
                                               unsigned int col) const {
  if (row >= m_filteredChannels.size()) {
    variant = "";
    return;
  }
  const Channel &ch = m_filteredChannels[row];
  if (col == 0) {
    variant = wxString::FromUTF8(ch.getName());
  } else {
    variant = "";
  }
}

bool EPGPanel::ChannelListModel::SetValueByRow(const wxVariant &,
                                               unsigned int,
                                               unsigned int) {
  return false;
}

wxString EPGPanel::ChannelListModel::GetColumnType(unsigned int) const {
  return "string";
}

unsigned int
EPGPanel::ChannelListModel::GetRow(const wxDataViewItem &item) const {
  return reinterpret_cast<uintptr_t>(item.GetID()) - 1;
}

wxDataViewItem
EPGPanel::ChannelListModel::GetItemByRow(unsigned int row) const {
  return wxDataViewItem(reinterpret_cast<void *>(row + 1));
}

void EPGPanel::ChannelListModel::Clear() {
  m_channels = nullptr;
  m_filteredChannels.clear();
  Reset(0);
}

const Channel &EPGPanel::ChannelListModel::GetChannel(unsigned int row) const {
  static Channel empty;
  if (row >= m_filteredChannels.size())
    return empty;
  return m_filteredChannels[row];
}

int EPGPanel::ChannelListModel::FindChannel(const Channel &ch) const {
  // 1) Поиск по (playlistName + name)
  if (!ch.getPlaylistName().empty()) {
    for (size_t i = 0; i < m_filteredChannels.size(); ++i) {
      if (m_filteredChannels[i].getName() == ch.getName() &&
          m_filteredChannels[i].getPlaylistName() == ch.getPlaylistName()) {
        return static_cast<int>(i);
      }
    }
  }
  // 2) Поиск по tvgId (если есть)
  if (!ch.getTvgId().empty()) {
    for (size_t i = 0; i < m_filteredChannels.size(); ++i) {
      if (m_filteredChannels[i].getTvgId() == ch.getTvgId())
        return static_cast<int>(i);
    }
  }
  // 3) Fallback по имени
  for (size_t i = 0; i < m_filteredChannels.size(); ++i) {
    if (m_filteredChannels[i].getName() == ch.getName())
      return static_cast<int>(i);
  }
  return -1;
}

void EPGPanel::ChannelListModel::SetSource(const std::vector<Channel> *source) {
  m_currentSource = source;
  if (m_currentSource) {
    m_filteredChannels = *m_currentSource;
  } else {
    m_filteredChannels.clear();
  }
  m_filterText.Clear();
  Reset(m_filteredChannels.size());
}

// =========================================================================
// EPGPanel
// =========================================================================
wxBEGIN_EVENT_TABLE(EPGPanel, wxPanel)
    EVT_DATAVIEW_SELECTION_CHANGED(ID_CHANNEL_LIST, EPGPanel::OnChannelSelected)
    EVT_DATAVIEW_ITEM_ACTIVATED(ID_CHANNEL_LIST, EPGPanel::OnChannelActivated)
    EVT_TEXT(ID_SEARCH_CTRL, EPGPanel::OnSearchText)
    EVT_BUTTON(ID_PREV_DAY, EPGPanel::OnPrevDay)
    EVT_BUTTON(ID_NEXT_DAY, EPGPanel::OnNextDay)
    EVT_BUTTON(ID_TODAY, EPGPanel::OnToday)
    EVT_BUTTON(ID_REFRESH_EPG, EPGPanel::OnRefreshEPG)
    EVT_BUTTON(ID_MANAGE_SOURCES, EPGPanel::OnManageSources)
    EVT_LIST_ITEM_SELECTED(ID_PROGRAM_LIST, EPGPanel::OnProgramSelected)
wxEND_EVENT_TABLE()

EPGPanel::EPGPanel(wxWindow *parent)
    : wxPanel(parent, wxID_ANY), m_currentDate(std::time(nullptr)),
      m_channelModel(new ChannelListModel()) {
  Application *app = static_cast<Application *>(wxTheApp);
  if (app) {
    m_epgManager = app->GetEPGManager();
  } else {
    m_epgManager = nullptr;
  }

  SetupUI();
}

EPGPanel::~EPGPanel() {
  if (m_channelListView) {
    m_channelListView->AssociateModel(nullptr);
  }
  delete m_channelModel;
  m_channelModel = nullptr;
}

void EPGPanel::SetChannels(const std::vector<Channel> &channels) {
  m_playlistChannels = channels;
  // Если текущий режим Playlist – обновляем модель, иначе просто храним
  if (m_currentMode == MODE_PLAYLIST) {
    m_channelModel->SetSource(&m_playlistChannels);
    // Восстанавливаем выделение
    SelectCurrentChannelInList();
  }
  // Если режим Favorites, но playlist пуст – можно переключиться на Favorites
  if (m_currentMode == MODE_PLAYLIST && m_playlistChannels.empty() &&
      !m_favoriteChannels.empty()) {
    SwitchMode(MODE_FAVORITES);
  }
  UpdateModeButtons(m_currentMode);
}

void EPGPanel::SetFavoriteChannels(const std::vector<Channel> &channels) {
  m_favoriteChannels = channels;
  if (m_currentMode == MODE_FAVORITES) {
    m_channelModel->SetSource(&m_favoriteChannels);
    SelectCurrentChannelInList();
  }
  if (m_currentMode == MODE_PLAYLIST && m_playlistChannels.empty() &&
      !m_favoriteChannels.empty()) {
    SwitchMode(MODE_FAVORITES);
  }
  UpdateModeButtons(m_currentMode);
}

void EPGPanel::SetCurrentChannel(Channel channel) {
  // Сохраняем канал
  m_currentChannel = channel;
  m_currentChannelId = channel.getTvgId();
  m_currentChannelName = channel.getName();
  m_currentDate = std::time(nullptr);

  // Определяем, в каком списке находится канал
  if (IsChannelInSource(channel, m_playlistChannels)) {
    if (m_currentMode != MODE_PLAYLIST) {
      SwitchMode(MODE_PLAYLIST);
    }
  } else if (IsChannelInSource(channel, m_favoriteChannels)) {
    if (m_currentMode != MODE_FAVORITES) {
      SwitchMode(MODE_FAVORITES);
    }
  } else {
    // Канал не найден ни в одном списке – оставляем текущий режим, но пробуем
    // выделить по имени (может быть, он появится после обновления)
    LOG_WARN("EPGPanel: Channel '%s' not found in any source",
             channel.getName().c_str());
  }

  // Теперь выделяем в текущем источнике
  SelectCurrentChannelInList();

  // Обновляем лейбл и загружаем программы
  m_channelNameLabel->SetLabel(wxString::FromUTF8(channel.getName()));
  LoadProgramsForChannel(channel.getTvgId(), m_currentDate);
  SaveState();
}

bool EPGPanel::IsChannelInSource(const Channel &ch,
                                 const std::vector<Channel> &source) const {
  for (const auto &c : source) {
    if (c.getName() == ch.getName() &&
        c.getPlaylistName() == ch.getPlaylistName())
      return true;
    if (!ch.getTvgId().empty() && c.getTvgId() == ch.getTvgId())
      return true;
  }
  return false;
}

void EPGPanel::ShowProgressControls(bool show) {
  m_progressGauge->Show(show);
  m_progressText->Show(show);
  m_cancelBtn->Show(show);
  if (show) {
    m_progressGauge->SetValue(0);
    m_progressText->SetLabel("Downloading...");
    m_prevDownloaded = 0.0;
    m_prevTime = std::chrono::steady_clock::now();
    m_progressTimer.Start(500);
  } else {
    m_progressTimer.Stop();
  }
  if (auto *parent = m_progressGauge->GetParent()) {
    parent->Layout();
  }
}

void EPGPanel::SetupUI() {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  wxSplitterWindow *splitter =
      new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxSP_3D | wxSP_LIVE_UPDATE);
  splitter->SetMinimumPaneSize(200);

  // ---- Левая панель ----
  wxPanel *leftPanel = new wxPanel(splitter, wxID_ANY);
  wxBoxSizer *leftSizer = new wxBoxSizer(wxVERTICAL);

  // Создаём панель для кнопок переключения
  wxPanel *modePanel = new wxPanel(leftPanel, wxID_ANY);
  wxBoxSizer *modeSizer = new wxBoxSizer(wxHORIZONTAL);

  m_btnPlaylist = new wxToggleButton(modePanel, wxID_ANY, "Playlist");
  m_btnFavorites = new wxToggleButton(modePanel, wxID_ANY, "Favorites");

  modeSizer->Add(m_btnPlaylist, 0, wxRIGHT, 5);
  modeSizer->Add(m_btnFavorites, 0);

  modePanel->SetSizer(modeSizer);
  leftSizer->Add(modePanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

  // Привязываем события
  m_btnPlaylist->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
    if (m_btnPlaylist->GetValue()) {
      SwitchMode(MODE_PLAYLIST);
    } else {
      // Если кнопка сброшена, но другая не активна – принудительно включаем её
      if (!m_btnFavorites->GetValue()) {
        m_btnPlaylist->SetValue(true);
      }
    }
  });

  m_btnFavorites->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
    if (m_btnFavorites->GetValue()) {
      SwitchMode(MODE_FAVORITES);
    } else {
      if (!m_btnPlaylist->GetValue()) {
        m_btnFavorites->SetValue(true);
      }
    }
  });

  m_searchCtrl =
      new wxTextCtrl(leftPanel, ID_SEARCH_CTRL, wxEmptyString,
                     wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
  m_searchCtrl->SetHint("Search channel...");
  leftSizer->Add(m_searchCtrl, 0, wxEXPAND | wxALL, 5);

  m_channelListView = new wxDataViewCtrl(leftPanel, ID_CHANNEL_LIST);
  m_channelListView->AssociateModel(m_channelModel);
  m_channelListView->AppendTextColumn("Channel", 0, wxDATAVIEW_CELL_INERT, 200,
                                      wxALIGN_LEFT);
  leftSizer->Add(m_channelListView, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 5);

  leftPanel->SetSizer(leftSizer);

  // ---- Правая панель ----
  wxPanel *rightPanel = new wxPanel(splitter, wxID_ANY);
  wxBoxSizer *rightSizer = new wxBoxSizer(wxVERTICAL);

  m_channelNameLabel =
      new wxStaticText(rightPanel, wxID_ANY, "No channel selected");
  wxFont titleFont = m_channelNameLabel->GetFont();
  titleFont.SetWeight(wxFONTWEIGHT_BOLD);
  m_channelNameLabel->SetFont(titleFont);
  rightSizer->Add(m_channelNameLabel, 0, wxALL, FromDIP(12));

  wxBoxSizer *navSizer = new wxBoxSizer(wxHORIZONTAL);
  m_dateLabel = new wxStaticText(rightPanel, wxID_ANY, "");
  m_prevDayBtn = new wxButton(rightPanel, ID_PREV_DAY, "<");
  m_todayBtn = new wxButton(rightPanel, ID_TODAY, "Today");
  m_nextDayBtn = new wxButton(rightPanel, ID_NEXT_DAY, ">");

  navSizer->Add(m_dateLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
  navSizer->Add(m_prevDayBtn, 0, wxRIGHT, FromDIP(2));
  navSizer->Add(m_todayBtn, 0, wxRIGHT, FromDIP(2));
  navSizer->Add(m_nextDayBtn, 0);
  navSizer->AddStretchSpacer(1);
  rightSizer->Add(navSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                  FromDIP(5));

  m_programList = new wxListCtrl(rightPanel, ID_PROGRAM_LIST, wxDefaultPosition,
                                 wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  m_programList->InsertColumn(0, "Time", wxLIST_FORMAT_LEFT, 100);
  m_programList->InsertColumn(1, "Title", wxLIST_FORMAT_LEFT, 300);
  m_programList->InsertColumn(2, "Category", wxLIST_FORMAT_LEFT, 150);
  rightSizer->Add(m_programList, 1, wxEXPAND | wxALL, FromDIP(5));

  wxStaticBox *detailBox = new wxStaticBox(rightPanel, wxID_ANY, "Details");
  wxStaticBoxSizer *detailSizer = new wxStaticBoxSizer(detailBox, wxVERTICAL);
  m_detailTitle = new wxStaticText(rightPanel, wxID_ANY, "");
  m_detailDesc = new wxStaticText(rightPanel, wxID_ANY, "");
  m_detailTitle->SetFont(m_detailTitle->GetFont().MakeBold());
  detailSizer->Add(m_detailTitle, 0, wxALL, FromDIP(5));
  detailSizer->Add(m_detailDesc, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
  rightSizer->Add(detailSizer, 0, wxEXPAND | wxALL, FromDIP(5));

  wxBoxSizer *bottomBtnSizer = new wxBoxSizer(wxHORIZONTAL);
  m_refreshBtn = new wxButton(rightPanel, ID_REFRESH_EPG, "↻ Refresh");
  bottomBtnSizer->Add(m_refreshBtn, 0, wxRIGHT, FromDIP(5));
  m_activityIndicator = new wxActivityIndicator(rightPanel, wxID_ANY);
  bottomBtnSizer->Add(m_activityIndicator, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL,
                      FromDIP(5));
  m_activityIndicator->Hide(); // по умолчанию скрыт
  m_manageSourcesBtn =
      new wxButton(rightPanel, ID_MANAGE_SOURCES, "⚙ Manage Sources");
  bottomBtnSizer->Add(m_manageSourcesBtn, 0);
  rightSizer->Add(bottomBtnSizer, 0, wxALIGN_LEFT | wxALL, FromDIP(5));

  // ---- Прогресс загрузки (скрыт по умолчанию) ----
  m_progressGauge = new wxGauge(rightPanel, wxID_ANY, 100, wxDefaultPosition,
                                wxSize(150, -1));
  m_progressText = new wxStaticText(rightPanel, wxID_ANY, "");
  m_cancelBtn = new wxButton(rightPanel, ID_CANCEL_DOWNLOAD, "Cancel");
  m_cancelBtn->Bind(wxEVT_BUTTON, &EPGPanel::OnCancelDownload, this);

  wxBoxSizer *progressSizer = new wxBoxSizer(wxHORIZONTAL);
  progressSizer->Add(m_progressGauge, 0, wxRIGHT, 5);
  progressSizer->Add(m_progressText, 0, wxRIGHT, 10);
  progressSizer->Add(m_cancelBtn, 0);
  rightSizer->Add(progressSizer, 0, wxALIGN_LEFT | wxALL, 5);

  // Изначально скрываем
  m_progressGauge->Hide();
  m_progressText->Hide();
  m_cancelBtn->Hide();

  // Привязываем таймер
  m_progressTimer.SetOwner(this, wxID_HIGHEST + 301);
  Bind(wxEVT_TIMER, &EPGPanel::OnProgressTimer, this, wxID_HIGHEST + 301);

  // set right panel
  rightPanel->SetSizer(rightSizer);

  splitter->SplitVertically(leftPanel, rightPanel, 250);
  mainSizer->Add(splitter, 1, wxEXPAND | wxALL, FromDIP(5));

  SetSizer(mainSizer);
}

void EPGPanel::OnChannelSelected(wxDataViewEvent &) {}

void EPGPanel::OnChannelActivated(wxDataViewEvent &event) {
  wxDataViewItem item = event.GetItem();
  if (!item.IsOk())
    return;

  unsigned int row = m_channelModel->GetRow(item);
  const Channel &ch = m_channelModel->GetChannel(row);

  m_currentChannel = ch;
  m_currentChannelId = ch.getTvgId();
  m_currentChannelName = ch.getName();

  m_channelNameLabel->SetLabel(wxString::FromUTF8(m_currentChannelName));
    
  LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  SaveState();
}

void EPGPanel::OnSearchText(wxCommandEvent &) {
  wxString text = m_searchCtrl->GetValue();
  m_channelModel->Filter(text);
  m_channelListView->Refresh();
}

void EPGPanel::OnPrevDay(wxCommandEvent &) {
  m_currentDate -= 24 * 3600;
  if (!m_currentChannelId.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

void EPGPanel::OnNextDay(wxCommandEvent &) {
  m_currentDate += 24 * 3600;
  if (!m_currentChannelId.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

void EPGPanel::OnToday(wxCommandEvent &) {
  m_currentDate = std::time(nullptr);
  if (!m_currentChannelId.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

void EPGPanel::OnRefreshEPG(wxCommandEvent &) {
  if (m_epgManager) {
    m_activityIndicator->Show();
    m_activityIndicator->Start();
    // Принудительно обновляем layout, чтобы индикатор появился в правильном месте
    if (auto *parent = m_activityIndicator->GetParent()) {
      parent->Layout();
    }

    ShowProgressControls(true);

    SetStatus("Updating...", "EPG refresh started in background.");
    m_epgManager->Refresh();
  }
}

void EPGPanel::OnManageSources(wxCommandEvent &) {
  wxString urlOrPath, name;
  bool isFile;
  int result = SettingsDialog::ShowAddEpgSourceDialog(wxTheApp->GetTopWindow(),
                                                      urlOrPath, name, isFile);

  if (result == -1) {
    return; // отмена
  }

  if (result == 0) {
    SetStatus("Source added", "EPG source added, refresh started.");
  } else if (result == 1) {
    SetStatus("Warning", "EPG source already exists.");
  } else {
    SetStatus("Error", "Failed to add EPG source.");
  }
}

void EPGPanel::LoadProgramsForChannel(const std::string &channelId,
                                      time_t date) {
  m_programList->DeleteAllItems();
  m_detailTitle->SetLabel("");
  m_detailDesc->SetLabel("");

  // ---- Если есть ошибка загрузки/парсинга, показываем её в колонке Title ----
  if (m_hasError && !m_lastError.IsEmpty()) {
    long item = m_programList->InsertItem(0, "");
    m_programList->SetItem(item, 1, "⚠ " + m_lastError);
    return;
  }

  // 1) Проверка менеджера
  if (!m_epgManager) {
    LOG_ERROR("EPGPanel: EPGManager is null");
    SetStatus("Error", "EPG manager not available");
    return;
  }

  // 2) Проверка загрузки и источников
  auto sources = m_epgManager->GetSources();
  if (!m_epgManager->IsLoaded()) {
    if (sources.empty()) {
      SetStatus("Warning",
                "No EPG sources configured. Add sources in Settings.");
    } else {
      SetStatus("Warning", "EPG not loaded yet. Try Refresh.");
    }
    return;
  }

  // ---- ПРОВЕРКА МАППИНГА ----
  if (!m_epgManager->HasMapping()) {
    SetStatus("Warning", "No EPG channels matched to playlist.");
    long item = m_programList->InsertItem(0, "");
    m_programList->SetItem(item, 1, "⚠ No EPG channels matched");
    return;
  }

  // 3) Получение программ
  auto programs = m_epgManager->GetProgramsForChannel(
      channelId, m_currentChannelName, date);
  m_currentPrograms = programs;
  if (programs.empty()) {
    SetStatus("Warning", "No programs for this date");
    // Вывод сообщения в колонку Title (колонка 1)
    long item = m_programList->InsertItem(0, ""); // колонка Time пуста
    m_programList->SetItem(item, 1, "No programs for this date");
    return;
  }

  // 4) Есть программы – заполняем список, статус очищаем
  ClearStatus();
  SaveState();

  wxDateTime dt(date);
  m_dateLabel->SetLabel(dt.Format("%A, %d %B %Y"));

  int idx = 0;
  for (const auto &prog : programs) {
    wxDateTime start(prog.startTime);
    wxDateTime stop(prog.stopTime);
    wxString timeRange = start.Format("%H:%M") + " - " + stop.Format("%H:%M");
    long item = m_programList->InsertItem(idx, timeRange);
    m_programList->SetItem(item, 1, wxString::FromUTF8(prog.title));
    m_programList->SetItem(item, 2, wxString::FromUTF8(prog.category));
    m_programList->SetItemData(item, idx);
    ++idx;
  }
}

void EPGPanel::OnProgramSelected(wxListEvent &event) {
  long sel = event.GetIndex();
  if (sel == -1)
    return;
  if (sel < 0 || sel >= (int)m_currentPrograms.size())
    return;

  const EpgProgram &prog = m_currentPrograms[sel];
  m_detailTitle->SetLabel(wxString::FromUTF8(prog.title));
  m_detailDesc->SetLabel(wxString::FromUTF8(prog.description));
}

void EPGPanel::OnEpgUpdateFinished(int status, const wxString &error) {
  LOG_DEBUG("EPGPanel::OnEpgUpdateFinished: status=%d, error=%s", status,
            error.ToUTF8().data());

  m_activityIndicator->Stop();
  m_activityIndicator->Hide();
  if (auto *parent = m_activityIndicator->GetParent()) {
    parent->Layout();
  }

  if (status == EPG_STATUS_OK) {
    SetStatus("Updated", "EPG updated successfully.");
    m_hasError = false;
    m_lastError.Clear();
  } else if (status == EPG_STATUS_ERROR) {
    SetStatus("Update failed", error.IsEmpty() ? "EPG update error" : error);
    LOG_ERROR("EPG update error: %s", error.ToUTF8().data());
    m_lastError = error;
    m_hasError = true;
  } else if (status == EPG_STATUS_NO_SOURCES) {
    SetStatus("Warning", "No EPG sources configured.");
    m_hasError = false;
    m_lastError.Clear();
  } else {
    SetStatus("", ""); // очистка
    m_hasError = false;
    m_lastError.Clear();
  }

  // Перезагрузить программы для текущего канала
  if (!m_currentChannelId.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }

  ShowProgressControls(false);
}

void EPGPanel::ShowMessage(const wxString &msg) {
  wxMessageBox(msg, "Info", wxOK | wxICON_INFORMATION, this);
}

void EPGPanel::SetStatus(const wxString &brief, const wxString &detail) {
  MainFrame *mf = dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow());
  if (mf) {
    mf->SetStatusText(brief, 0);
    mf->SetStatusText(detail, 1);
  } else {
    LOG_ERROR("EPGPanel: Cannot find MainFrame to set status");
  }
}

void EPGPanel::ClearStatus() {
  MainFrame *mf = dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow());
  if (mf) {
    mf->SetStatusText("", 0);
    mf->SetStatusText("", 1);
  }
}

void EPGPanel::SwitchMode(Mode mode) {
  if (m_currentMode == mode)
    return;
  m_currentMode = mode;
  UpdateModeButtons(mode);

  const std::vector<Channel> *source = nullptr;
  if (mode == MODE_PLAYLIST) {
    source = &m_playlistChannels;
  } else {
    source = &m_favoriteChannels;
  }

  m_channelModel->SetSource(source);
  // Сброс поиска
  if (m_searchCtrl) {
    m_searchCtrl->SetValue(wxEmptyString);
    m_channelModel->Filter(wxEmptyString);
  }

  // Восстановить выделение, если текущий канал есть в новом источнике
  SelectCurrentChannelInList();

  wxString modeName = (mode == MODE_PLAYLIST) ? "Playlist" : "Favorites";
  SetStatus("", wxString::Format("EPG mode: %s", modeName));
}

void EPGPanel::UpdateModeButtons(Mode mode) {
  if (m_btnPlaylist && m_btnFavorites) {
    m_btnPlaylist->SetValue(mode == MODE_PLAYLIST);
    m_btnFavorites->SetValue(mode == MODE_FAVORITES);
  }
}

void EPGPanel::SelectCurrentChannelInList() {
  if (!m_channelModel || m_currentChannel.getName().empty())
    return;

  int idx = m_channelModel->FindChannel(m_currentChannel);
  if (idx >= 0) {
    wxDataViewItem item = m_channelModel->GetItemByRow(idx);
    m_channelListView->SetCurrentItem(item);
    m_channelListView->Select(item);
    m_channelListView->EnsureVisible(item);
    m_channelListView->Refresh();
  } else {
    m_channelListView->UnselectAll();
  }
}

void EPGPanel::SaveState() {
  s_lastChannelId = m_currentChannelId;
  s_lastChannelName = m_currentChannelName;
  s_lastPlaylistName = m_currentChannel.getPlaylistName();
  s_lastDate = m_currentDate;
}

void EPGPanel::RestoreState() {
  if (!s_lastChannelId.empty() || !s_lastChannelName.empty()) {
    Channel ch;
    ch.setTvgId(s_lastChannelId);
    ch.setName(s_lastChannelName);
    if (!s_lastPlaylistName.empty()) {
      ch.setPlaylistName(s_lastPlaylistName);
    }
    m_currentDate = s_lastDate; // восстанавливаем дату
    SetCurrentChannel(
        ch); // автоматически выбирает режим, выделяет, загружает программы
  } else {
    // Нет сохранённого канала – сбрасываем UI
    m_channelNameLabel->SetLabel("No channel selected");
    m_programList->DeleteAllItems();
    m_detailTitle->SetLabel("");
    m_detailDesc->SetLabel("");
    if (m_channelListView) {
      m_channelListView->UnselectAll();
    }
  }
}

static wxString FormatSize(double bytes) {
  const char *units[] = {"B", "KB", "MB", "GB"};
  int i = 0;
  while (bytes >= 1024 && i < 3) {
    bytes /= 1024;
    i++;
  }
  return wxString::Format("%.1f %s", bytes, units[i]);
}

void EPGPanel::OnProgressTimer(wxTimerEvent &) {
  if (!m_epgManager) {
    ShowProgressControls(false);
    return;
  }
  const auto &prog = m_epgManager->GetDownloadProgress();
  if (prog.abort.load()) {
    ShowProgressControls(false);
    return;
  }

  double total = prog.totalBytes.load();
  double downloaded = prog.downloadedBytes.load();

  if (total > 0) {
    int percent = static_cast<int>((downloaded / total) * 100);
    m_progressGauge->SetValue(percent);
  } else {
    // Если неизвестен общий размер, показываем неопределённый прогресс
    // (мигающий)
    m_progressGauge->Pulse();
  }

  // Формируем текст с размером и скоростью
  wxString text = FormatSize(downloaded);
  if (total > 0) {
    text += " / " + FormatSize(total);
  }

  // Скорость
  auto now = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration<double>(now - m_prevTime).count();
  if (elapsed > 0.5) {
    double speed =
        (downloaded - m_prevDownloaded) / elapsed; // bytes per second
    if (speed > 0) {
      text += "  " + FormatSize(speed) + "/s";
    }
    m_prevDownloaded = downloaded;
    m_prevTime = now;
  }

  m_progressText->SetLabel(text);
}

void EPGPanel::OnCancelDownload(wxCommandEvent &) {
  if (m_epgManager) {
    m_epgManager->AbortDownload();
    ShowProgressControls(false);
    SetStatus("Cancelled", "EPG download cancelled by user.");
  }
}
