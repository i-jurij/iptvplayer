#include "EPGPanel.h"
#include "../Channel.h"
#include "../LogControl.h"
#include "Application.h"
#include "EPGManager.h"
#include "EventIDs.h"
#include "MainFrame.h"
#include "SettingsDialog.h"
#include <ctime>
#include <wx/datetime.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>

std::string EPGPanel::s_lastChannelId;
std::string EPGPanel::s_lastChannelName;
time_t EPGPanel::s_lastDate = 0;

void EPGPanel::SaveState() {
  s_lastChannelId = m_currentChannelId;
  s_lastChannelName = m_currentChannelName;
  s_lastDate = m_currentDate;
}

void EPGPanel::RestoreState() {
  if (!s_lastChannelId.empty()) {
    m_currentChannelId = s_lastChannelId;
    m_currentChannelName = s_lastChannelName;
    m_currentDate = s_lastDate;
    m_channelNameLabel->SetLabel(wxString::FromUTF8(m_currentChannelName));
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
    // (опционально) можно также попытаться выделить канал в списке, но не
    // обязательно
  }
}

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

  if (filter.IsEmpty() || !m_channels) {
    m_filteredChannels = *m_channels;
    Reset(m_filteredChannels.size());
    return;
  }

  wxString lowerFilter = filter.Lower();
  wxArrayString parts = wxSplit(lowerFilter, ' ');

  // Удаляем пустые части
  parts.erase(std::remove_if(parts.begin(), parts.end(),
                             [](const wxString &s) { return s.IsEmpty(); }),
              parts.end());

  // Если есть несколько слов — мультисловный поиск
  if (parts.size() > 1) {
    for (const auto &ch : *m_channels) {
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

  // 1) Сначала собираем каналы, название которых начинается с needle
  std::vector<Channel> startsWith;
  std::vector<Channel> contains;

  for (const auto &ch : *m_channels) {
    wxString name = wxString::FromUTF8(ch.getName()).Lower();
    if (name.StartsWith(needle)) {
      startsWith.push_back(ch);
    } else if (name.Contains(needle)) {
      contains.push_back(ch);
    }
  }

  // 2) Формируем результат: сначала начинающиеся с needle, потом остальные
  m_filteredChannels.reserve(startsWith.size() + contains.size());
  m_filteredChannels.insert(m_filteredChannels.end(), startsWith.begin(),
                            startsWith.end());
  m_filteredChannels.insert(m_filteredChannels.end(), contains.begin(),
                            contains.end());

  // 3) Если ничего не найдено — можно добавить fuzzy-поиск (необязательно)
  if (m_filteredChannels.empty()) {
    // Fuzzy: ищем последовательность букв (без учёта регистра)
    for (const auto &ch : *m_channels) {
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

int EPGPanel::ChannelListModel::FindChannel(
    const std::string &channelId) const {
  for (size_t i = 0; i < m_filteredChannels.size(); ++i) {
    if (m_filteredChannels[i].getTvgId() == channelId) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// =========================================================================
// EPGPanel
// =========================================================================
wxBEGIN_EVENT_TABLE(EPGPanel, wxPanel)
    EVT_DATAVIEW_SELECTION_CHANGED(wxID_ANY, EPGPanel::OnChannelSelected)
        EVT_TEXT(wxID_ANY, EPGPanel::OnSearchText)
            EVT_BUTTON(wxID_ANY, EPGPanel::OnPrevDay)
                EVT_BUTTON(wxID_ANY,
                           EPGPanel::OnNextDay) EVT_BUTTON(wxID_ANY,
                                                           EPGPanel::OnToday)
                    EVT_BUTTON(wxID_ANY, EPGPanel::OnRefreshEPG)
                        EVT_BUTTON(wxID_ANY, EPGPanel::OnManageSources)
                            EVT_LIST_ITEM_SELECTED(wxID_ANY,
                                                   EPGPanel::OnProgramSelected)
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

  Bind(EVT_EPG_UPDATED, &EPGPanel::OnEPGUpdated, this);
}

EPGPanel::~EPGPanel() {
  if (m_channelListView) {
    m_channelListView->AssociateModel(nullptr);
  }
  delete m_channelModel;
  m_channelModel = nullptr;
}

void EPGPanel::SetChannels(const std::vector<Channel> &channels) {
  m_channelModel->SetChannels(channels);
  if (!m_currentChannelId.empty()) {
    int idx = m_channelModel->FindChannel(m_currentChannelId);
    if (idx >= 0) {
      wxDataViewItem item = m_channelModel->GetItemByRow(idx);
      m_channelListView->SetCurrentItem(item);
      m_channelListView->Select(item);
    }
  }
}

void EPGPanel::SetCurrentChannel(const std::string &channelId,
                                 const std::string &channelName) {
  m_currentChannelId = channelId;
  m_currentChannelName = channelName;
  m_currentDate = std::time(nullptr);

  if (!m_channelModel)
    return;

  int idx = m_channelModel->FindChannel(channelId);
  if (idx >= 0) {
    wxDataViewItem item = m_channelModel->GetItemByRow(idx);
    m_channelListView->SetCurrentItem(item);
    m_channelListView->Select(item);
    m_channelNameLabel->SetLabel(wxString::FromUTF8(channelName));
    LoadProgramsForChannel(channelId, m_currentDate);
    SaveState();
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

  m_searchCtrl =
      new wxTextCtrl(leftPanel, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxDefaultSize, wxTE_PROCESS_ENTER);
  m_searchCtrl->SetHint("Search channel...");
  leftSizer->Add(m_searchCtrl, 0, wxEXPAND | wxALL, 5);

  m_channelListView = new wxDataViewCtrl(leftPanel, wxID_ANY);
  m_channelListView->AssociateModel(m_channelModel); // исправлено
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
  m_prevDayBtn = new wxButton(rightPanel, wxID_ANY, "<");
  m_todayBtn = new wxButton(rightPanel, wxID_ANY, "Today");
  m_nextDayBtn = new wxButton(rightPanel, wxID_ANY, ">");

  navSizer->Add(m_dateLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
  navSizer->Add(m_prevDayBtn, 0, wxRIGHT, FromDIP(2));
  navSizer->Add(m_todayBtn, 0, wxRIGHT, FromDIP(2));
  navSizer->Add(m_nextDayBtn, 0);
  navSizer->AddStretchSpacer(1);
  rightSizer->Add(navSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                  FromDIP(5));

  m_programList = new wxListCtrl(rightPanel, wxID_ANY, wxDefaultPosition,
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
  m_refreshBtn = new wxButton(rightPanel, wxID_ANY, "↻ Refresh");
  bottomBtnSizer->Add(m_refreshBtn, 0, wxRIGHT, FromDIP(5));
  m_activityIndicator = new wxActivityIndicator(rightPanel, wxID_ANY);
  bottomBtnSizer->Add(m_activityIndicator, 0, wxLEFT | wxALIGN_CENTER_VERTICAL,
                      FromDIP(5));
  m_activityIndicator->Hide(); // по умолчанию скрыт
  m_manageSourcesBtn = new wxButton(rightPanel, wxID_ANY, "⚙ Manage Sources");
  bottomBtnSizer->Add(m_manageSourcesBtn, 0);
  rightSizer->Add(bottomBtnSizer, 0, wxALIGN_LEFT | wxALL, FromDIP(5));

  rightPanel->SetSizer(rightSizer);

  splitter->SplitVertically(leftPanel, rightPanel, 250);
  mainSizer->Add(splitter, 1, wxEXPAND | wxALL, FromDIP(5));

  SetSizer(mainSizer);
}

void EPGPanel::OnChannelSelected(wxDataViewEvent &event) {
  wxDataViewItem item = event.GetItem();
  if (!item.IsOk())
    return;
  unsigned int row = m_channelModel->GetRow(item);
  const Channel &ch = m_channelModel->GetChannel(row);
  if (ch.getTvgId().empty())
    return;

  m_currentChannelId = ch.getTvgId();
  m_currentChannelName = ch.getName();

  m_channelNameLabel->SetLabel(wxString::FromUTF8(m_currentChannelName));
  LoadProgramsForChannel(m_currentChannelId, m_currentDate);
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
    SetStatus("Updating...", "EPG refresh started in background.");
    m_epgManager->Refresh();
  }
}

void EPGPanel::OnManageSources(wxCommandEvent &) {
  MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this));
  if (mf) {
    SettingsDialog dlg(mf, mf->getConfigManager());
    dlg.ShowModal();
  }
}

void EPGPanel::LoadProgramsForChannel(const std::string &channelId,
                                      time_t date) {
  m_programList->DeleteAllItems();
  m_detailTitle->SetLabel("");
  m_detailDesc->SetLabel("");

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
      SetStatus("No sources",
                "No EPG sources configured. Add sources in Settings.");
    } else {
      SetStatus("Not loaded", "EPG not loaded yet. Try Refresh.");
    }
    return;
  }

  // 3) Получение программ
  auto programs = m_epgManager->GetProgramsForChannel(channelId, date);
  if (programs.empty()) {
    SetStatus("No programs", "No programs for this date");
    // Вывод сообщения в колонку Title (колонка 1)
    long item = m_programList->InsertItem(0, ""); // колонка Time пуста
    m_programList->SetItem(item, 1, "No programs for this date");
    return;
  }

  // 4) Есть программы – заполняем список, статус очищаем
  ClearStatus();

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
  if (!m_epgManager)
    return;
  auto programs =
      m_epgManager->GetProgramsForChannel(m_currentChannelId, m_currentDate);
  if (sel < 0 || sel >= (int)programs.size())
    return;
  const EpgProgram &prog = programs[sel];
  m_detailTitle->SetLabel(wxString::FromUTF8(prog.title));
  m_detailDesc->SetLabel(wxString::FromUTF8(prog.description));
}

void EPGPanel::OnEPGUpdated(wxCommandEvent &event) {
  m_activityIndicator->Stop();
  m_activityIndicator->Hide();

  int status = event.GetInt();
  wxString error = event.GetString();

  if (status == EPG_STATUS_OK) {
    SetStatus("Updated", "EPG updated successfully.");
  } else if (status == EPG_STATUS_ERROR) {
    SetStatus("Update failed", error.IsEmpty() ? "EPG update error" : error);
    LOG_ERROR("EPG update error: %s", error.ToUTF8().data());
  } else if (status == EPG_STATUS_NO_SOURCES) {
    SetStatus("No sources", "No EPG sources configured.");
  } else {
    SetStatus("", ""); // очистка
  }

  // Перезагрузить программы для текущего канала
  if (!m_currentChannelId.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

void EPGPanel::ShowMessage(const wxString &msg) {
  wxMessageBox(msg, "Info", wxOK | wxICON_INFORMATION, this);
}

void EPGPanel::RefreshCurrentChannel() {
  if (!m_currentChannelId.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

void EPGPanel::SetStatus(const wxString &brief, const wxString &detail) {
  MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this));
  if (mf) {
    mf->SetStatusText(brief, 0);
    mf->SetStatusText(detail, 1);
  }
}

void EPGPanel::ClearStatus() {
  MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this));
  if (mf) {
    mf->SetStatusText("", 0);
    mf->SetStatusText("", 1);
  }
}
