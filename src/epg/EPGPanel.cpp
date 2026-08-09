#include "EPGPanel.h"
#include "../Channel.h"
#include "../LogControl.h"
#include "Application.h"
#include "EPGManager.h"
#include "EventIDs.h"
#include "MainFrame.h"
#include "SettingsDialog.h"
#include "epg/EpgSourceManagerPanel.h"

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

bool EPGPanel::ChannelListModel::SetValueByRow(const wxVariant &, unsigned int,
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
        EVT_DATAVIEW_ITEM_ACTIVATED(ID_CHANNEL_LIST,
                                    EPGPanel::OnChannelActivated)
            EVT_TEXT(ID_SEARCH_CTRL, EPGPanel::OnSearchText)
                EVT_BUTTON(ID_PREV_DAY, EPGPanel::OnPrevDay)
                    EVT_BUTTON(ID_NEXT_DAY, EPGPanel::OnNextDay)
                        EVT_BUTTON(ID_TODAY, EPGPanel::OnToday)
                            EVT_BUTTON(ID_REFRESH_EPG, EPGPanel::OnRefreshEPG)
                                EVT_BUTTON(ID_MANAGE_SOURCES,
                                           EPGPanel::OnManageSources)
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

  if (m_epgManager) {
    m_epgManager->SetOnRefreshStarted([this]() {
      // Колбэк выполняется в основном потоке через CallAfter
      if (!m_refreshing) {
        m_refreshing = true;
        m_refreshBtn->Enable(false);
        m_activityIndicator->Show();
        m_activityIndicator->Start();
        ShowProgressControls(true);
        SetStatus("Updating...", "EPG refresh started.");
        if (auto *parent = m_activityIndicator->GetParent()) {
          parent->Layout();
        }
      }
    });
  }
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

  // ---- Создаём wxGrid для программ ----
  m_programGrid =
      new wxGrid(rightPanel, ID_PROGRAM_LIST, wxDefaultPosition, wxDefaultSize);
  m_programGrid->CreateGrid(0, 3);
  /*
    m_programGrid->SetColLabelValue(0, "Time");
    m_programGrid->SetColLabelValue(1, "Title");
    m_programGrid->SetColLabelValue(2, "Category");
  */
  m_programGrid->SetColLabelSize(0);
  // Скрыть колонку с номерами строк
  m_programGrid->SetRowLabelSize(0);

  // Отключаем перенос для колонки Time
  wxGridCellAttr *attrTime = new wxGridCellAttr();
  attrTime->SetRenderer(new wxGridCellStringRenderer());
  m_programGrid->SetColAttr(0, attrTime);
  // Включаем перенос текста для остальных
  m_programGrid->SetDefaultRenderer(new wxGridCellAutoWrapStringRenderer);
  m_programGrid->SetDefaultCellOverflow(false);

  // ---- Настройка выделения ----
  m_programGrid->SetSelectionMode(wxGrid::wxGridSelectRows);
  m_programGrid->SetSelectionBackground(
      wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
  m_programGrid->SetSelectionForeground(
      wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
  m_programGrid->SetCellHighlightPenWidth(FromDIP(1));
  m_programGrid->SetCellHighlightColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
  /*
    // Заголовки – системные цвета
    m_programGrid->SetLabelBackgroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    m_programGrid->SetLabelTextColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
  */

  // Запрещаем редактирование
  m_programGrid->EnableEditing(false);
  m_programGrid->DisableDragRowSize();
  // Начальные ширины (будут пересчитаны позже)
  m_programGrid->SetColSize(0, 100);
  m_programGrid->SetColSize(1, 300);
  m_programGrid->SetColSize(2, 150);
  // Привязываем события (вместо таблицы событий)
  m_programGrid->Bind(wxEVT_GRID_SELECT_CELL, &EPGPanel::OnProgramSelected,
                      this);
  m_programGrid->Bind(wxEVT_SIZE, &EPGPanel::OnProgramListResize, this);
  // Добавляем в правую панель (вместо старого m_programList)
  rightSizer->Add(m_programGrid, 1, wxEXPAND | wxALL, FromDIP(5));

  // ---- Блок деталей ----
  wxStaticBox *detailBox = new wxStaticBox(rightPanel, wxID_ANY, "Details");
  wxStaticBoxSizer *detailSizer = new wxStaticBoxSizer(detailBox, wxVERTICAL);

  m_detailTitle = new wxStaticText(rightPanel, wxID_ANY, "");
  m_detailTitle->SetFont(m_detailTitle->GetFont().MakeBold());
  detailSizer->Add(m_detailTitle, 0, wxALL, FromDIP(5));

  // Создаём многострочный текст с прокруткой
  m_detailDesc =
      new wxTextCtrl(rightPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
  m_detailDesc->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
  m_detailDesc->SetForegroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
  m_detailDesc->SetMinSize(wxSize(-1, FromDIP(80))); // минимальная высота
  m_detailDesc->SetMaxSize(wxSize(-1, FromDIP(200)));
  detailSizer->Add(m_detailDesc, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                   FromDIP(5));

  rightSizer->Add(detailSizer, 0, wxEXPAND | wxALL, FromDIP(5));

  // ---- Нижняя панель с кнопками и прогрессом в одной строке ----
  wxBoxSizer *bottomRowSizer = new wxBoxSizer(wxHORIZONTAL);

  m_refreshBtn = new wxButton(rightPanel, ID_REFRESH_EPG, "↻ Refresh");
  bottomRowSizer->Add(m_refreshBtn, 0, wxRIGHT, FromDIP(5));

  m_activityIndicator = new wxActivityIndicator(rightPanel, wxID_ANY);
  bottomRowSizer->Add(m_activityIndicator, 0,
                      wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(5));
  m_activityIndicator->Hide();

  m_manageSourcesBtn =
      new wxButton(rightPanel, ID_MANAGE_SOURCES, "⚙ Manage Sources");
  bottomRowSizer->Add(m_manageSourcesBtn, 0);

  // Прогресс-бар, текст и кнопка Cancel (изначально скрыты)
  m_progressGauge = new wxGauge(rightPanel, wxID_ANY, 100, wxDefaultPosition,
                                wxSize(150, -1));
  m_progressText = new wxStaticText(rightPanel, wxID_ANY, "");
  m_cancelBtn = new wxButton(rightPanel, ID_CANCEL_DOWNLOAD, "Cancel");
  m_cancelBtn->Bind(wxEVT_BUTTON, &EPGPanel::OnCancelDownload, this);

  bottomRowSizer->Add(m_progressGauge, 0, wxRIGHT, FromDIP(5));
  bottomRowSizer->Add(m_progressText, 0, wxRIGHT, FromDIP(10));
  bottomRowSizer->Add(m_cancelBtn, 0);

  // Изначально скрыты
  m_progressGauge->Hide();
  m_progressText->Hide();
  m_cancelBtn->Hide();

  rightSizer->Add(bottomRowSizer, 0, wxEXPAND | wxALL, FromDIP(5));

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
  if (m_refreshing) {
    SetStatus("Info", "EPG update already in progress.");
    return;
  }
  if (m_epgManager) {
    // Колбэк начала обновления сработает автоматически
    m_epgManager->Refresh();
  }
}

void EPGPanel::OnManageSources(wxCommandEvent &) {
  if (!m_epgManager) {
    wxMessageBox("EPG Manager not available.", "Error", wxOK | wxICON_ERROR);
    return;
  }

  wxDialog dlg(this, wxID_ANY, "Manage EPG Sources", wxDefaultPosition,
               wxSize(700, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  EpgSourceManagerPanel *panel =
      new EpgSourceManagerPanel(&dlg, m_epgManager, false);
  mainSizer->Add(panel, 1, wxEXPAND | wxALL, 10);

  wxSizer *btnSizer = dlg.CreateButtonSizer(wxOK | wxCANCEL);
  if (btnSizer)
    mainSizer->Add(btnSizer, 0, wxALL | wxALIGN_RIGHT, 10);

  dlg.SetSizerAndFit(mainSizer);
  dlg.CentreOnParent();

  if (dlg.ShowModal() == wxID_OK) {
    // После закрытия обновляем программы
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

void EPGPanel::AdjustProgramColumns() {
  if (!m_programGrid || m_programGrid->GetNumberRows() == 0)
    return;

  int totalWidth;
  m_programGrid->GetClientSize(&totalWidth, nullptr);

  // ---- Ручной расчёт ширины колонки Time ----
  int maxTimeWidth = 0;
  wxClientDC dc(m_programGrid);
  dc.SetFont(m_programGrid->GetDefaultCellFont());
  for (int row = 0; row < m_programGrid->GetNumberRows(); ++row) {
    wxString text = m_programGrid->GetCellValue(row, 0);
    wxSize extent = dc.GetTextExtent(text);
    if (extent.GetWidth() > maxTimeWidth)
      maxTimeWidth = extent.GetWidth();
  }
  // Добавляем запас, пропорциональный DPI
  maxTimeWidth += FromDIP(20);
  // Минимальная ширина на всякий случай
  if (maxTimeWidth < FromDIP(80))
    maxTimeWidth = FromDIP(80);

  // Category – фиксированная 150 (тоже с учётом DPI, если нужно)
  int catWidth = FromDIP(150);

  // Title – всё оставшееся место
  int titleWidth = totalWidth - maxTimeWidth - catWidth;
  if (titleWidth < FromDIP(50))
    titleWidth = FromDIP(50);

  m_programGrid->SetColSize(0, maxTimeWidth);
  m_programGrid->SetColSize(1, titleWidth);
  m_programGrid->SetColSize(2, catWidth);

  // Автовысота строк (для Title и Category с переносом)
  m_programGrid->AutoSizeRows();
}

void EPGPanel::OnProgramListResize(wxSizeEvent &event) {
  AdjustProgramColumns();
  event.Skip();
}

void EPGPanel::LoadProgramsForChannel(const std::string &channelId,
                                      time_t date) {
  // Очистка сетки
  m_programGrid->ClearGrid();
  if (m_programGrid->GetNumberRows() > 0)
    m_programGrid->DeleteRows(0, m_programGrid->GetNumberRows());

  m_detailTitle->SetLabel("");
  m_detailDesc->SetValue("");
  // ---- Если есть ошибка ----
  if (m_hasError && !m_lastError.IsEmpty()) {
    m_programGrid->AppendRows(1);
    m_programGrid->SetCellValue(0, 0, "");
    m_programGrid->SetCellValue(0, 1, "⚠ " + m_lastError);
    m_programGrid->SetCellValue(0, 2, "");
    AdjustProgramColumns();
    return;
  }

  if (!m_epgManager) {
    LOG_ERROR("EPGPanel: EPGManager is null");
    SetStatus("Error", "EPG manager not available");
    return;
  }

  auto sources = m_epgManager->GetSources();
  if (!m_epgManager->IsLoaded()) {
    if (sources.empty()) {
      SetStatus("Warning",
                "No EPG sources configured. Add sources in Settings.");
    } else {
      SetStatus("Warning", "EPG not loaded yet. Try Refresh.");
    }
    m_programGrid->AppendRows(1);
    m_programGrid->SetCellValue(0, 0, "");
    m_programGrid->SetCellValue(0, 1, "⚠ No EPG data loaded");
    m_programGrid->SetCellValue(0, 2, "");
    AdjustProgramColumns();
    return;
  }

  if (!m_epgManager->HasMapping()) {
    SetStatus("Warning", "No EPG channels matched to playlist.");
    m_programGrid->AppendRows(1);
    m_programGrid->SetCellValue(0, 0, "");
    m_programGrid->SetCellValue(0, 1, "⚠ No EPG channels matched");
    m_programGrid->SetCellValue(0, 2, "");
    AdjustProgramColumns();
    return;
  }

  auto programs = m_epgManager->GetProgramsForChannel(
      channelId, m_currentChannelName, date);
  m_currentPrograms = programs;

  if (programs.empty()) {
    SetStatus("Warning", "No programs for this date");
    m_programGrid->AppendRows(1);
    m_programGrid->SetCellValue(0, 0, "");
    m_programGrid->SetCellValue(0, 1, "No programs for this date");
    m_programGrid->SetCellValue(0, 2, "");
    AdjustProgramColumns();
    return;
  }

  ClearStatus();
  SaveState();

  wxDateTime dt(date);
  m_dateLabel->SetLabel(dt.Format("%A, %d %B %Y"));
  if (auto *parent = m_dateLabel->GetParent()) {
    parent->Layout();
  }

  int row = 0;
  for (const auto &prog : programs) {
    wxDateTime start = GetLocalDateTime(prog.startTime);
    wxDateTime stop = GetLocalDateTime(prog.stopTime);
    wxString timeRange = start.Format("%H:%M") + " - " + stop.Format("%H:%M");

    m_programGrid->AppendRows(1);
    m_programGrid->SetCellValue(row, 0, timeRange);
    m_programGrid->SetCellValue(row, 1, wxString::FromUTF8(prog.title));
    m_programGrid->SetCellValue(row, 2, wxString::FromUTF8(prog.category));
    ++row;
  }

  AdjustProgramColumns();
}

void EPGPanel::OnProgramSelected(wxGridEvent &event) {
  int row = event.GetRow();
  if (row < 0 || row >= (int)m_currentPrograms.size())
    return;

  const EpgProgram &prog = m_currentPrograms[row];
  m_detailTitle->SetLabel(wxString::FromUTF8(prog.title));
  m_detailDesc->SetValue(wxString::FromUTF8(prog.description));
}

void EPGPanel::OnEpgUpdateFinished(int status, const wxString &error) {
  LOG_DEBUG("EPGPanel::OnEpgUpdateFinished: status=%d, error=%s", status,
            error.ToUTF8().data());

  m_refreshing = false;
  m_refreshBtn->Enable(true);
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
    m_programGrid->ClearGrid();
    if (m_programGrid->GetNumberRows() > 0) {
      m_programGrid->DeleteRows(0, m_programGrid->GetNumberRows());
    }
    m_detailTitle->SetLabel("");
    m_detailDesc->SetValue("");
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
