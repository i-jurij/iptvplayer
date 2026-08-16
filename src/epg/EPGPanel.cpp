#include "EPGPanel.h"
#include "../Channel.h"
#include "../LogControl.h"
#include "Application.h"
#include "EPGManager.h"
#include "EventIDs.h"
#include "MainFrame.h"
#include "SettingsDialog.h"

#include <wx/artprov.h>
#include <wx/datetime.h>
#include <wx/event.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/string.h>

#include <ctime>

// Статические переменные для сохранения состояния
std::string EPGPanel::s_lastChannelId;
std::string EPGPanel::s_lastChannelName;
std::string EPGPanel::s_lastPlaylistName;
time_t EPGPanel::s_lastDate = 0;

// ----------------------------------------------------------------------------
// Таблица событий (только то, что осталось)
// ----------------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(EPGPanel, wxPanel)
    EVT_BUTTON(wxID_ANY, EPGPanel::OnPrevDay) // ID назначаются вручную, но
                                              // можно использовать wxID_ANY
    EVT_BUTTON(wxID_ANY, EPGPanel::OnToday)
        EVT_BUTTON(wxID_ANY, EPGPanel::OnNextDay)
            EVT_GRID_SELECT_CELL(EPGPanel::OnProgramSelected)
                EVT_SIZE(EPGPanel::OnProgramListResize) wxEND_EVENT_TABLE()

    // ----------------------------------------------------------------------------
    // Конструктор / Деструктор
    // ----------------------------------------------------------------------------
    EPGPanel::EPGPanel(wxWindow *parent)
    : wxPanel(parent, wxID_ANY),
      m_currentDate(EpgTime::GetStartOfDay(wxDateTime::Now().GetTicks())),
      m_epgManager(nullptr), m_isActive(false), m_hasError(false) {
  // Получаем EPGManager из приложения
  Application *app = static_cast<Application *>(wxTheApp);
  if (app) {
    m_epgManager = app->GetEPGManager();
  }

  SetupUI();

  // Восстанавливаем последнее состояние
  RestoreState();

  // Подписываемся на обновления EPG (если нужно, но MainFrame будет вызывать
  // OnEpgUpdateFinished)
}

EPGPanel::~EPGPanel() {
  // Сохраняем состояние перед закрытием
  SaveState();
}

// ----------------------------------------------------------------------------
// SetupUI – создание правой панели (программа)
// ----------------------------------------------------------------------------
void EPGPanel::SetupUI() {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // ---- Верхняя строка: название + дата + навигация ----
  wxBoxSizer *topSizer = new wxBoxSizer(wxHORIZONTAL);

  m_channelNameLabel = new wxStaticText(this, wxID_ANY, "No channel selected");
  wxFont titleFont = m_channelNameLabel->GetFont();
  titleFont.SetWeight(wxFONTWEIGHT_BOLD);
  m_channelNameLabel->SetFont(titleFont);
  topSizer->Add(m_channelNameLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                FromDIP(20));

  m_dateLabel = new wxStaticText(this, wxID_ANY, "");
  topSizer->Add(m_dateLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(15));

  m_prevDayBtn = new wxButton(this, wxID_ANY, "\u2190");
  m_prevDayBtn->SetToolTip("Previous day");
  topSizer->Add(m_prevDayBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));

  m_todayBtn = new wxButton(this, wxID_ANY, "Today");
  topSizer->Add(m_todayBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));

  m_nextDayBtn = new wxButton(this, wxID_ANY, "\u2192");
  m_nextDayBtn->SetToolTip("Next day");
  topSizer->Add(m_nextDayBtn, 0, wxALIGN_CENTER_VERTICAL);

  int btnHeight = FromDIP(30);
  m_prevDayBtn->SetMinSize(wxSize(FromDIP(40), btnHeight));
  m_todayBtn->SetMinSize(wxSize(-1, btnHeight));
  m_nextDayBtn->SetMinSize(wxSize(FromDIP(40), btnHeight));

  mainSizer->Add(topSizer, 0, wxEXPAND);

  // Привязка событий навигации (в таблице событий уже есть, но можно и здесь)
  m_prevDayBtn->Bind(wxEVT_BUTTON, &EPGPanel::OnPrevDay, this);
  m_todayBtn->Bind(wxEVT_BUTTON, &EPGPanel::OnToday, this);
  m_nextDayBtn->Bind(wxEVT_BUTTON, &EPGPanel::OnNextDay, this);

  // ---- Сетка программ ----
  m_programGrid = new wxGrid(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
  m_programGrid->CreateGrid(0, 3);
  m_programGrid->SetColLabelSize(0);
  m_programGrid->SetRowLabelSize(0);
  m_programGrid->SetDefaultRenderer(new wxGridCellAutoWrapStringRenderer);
  m_programGrid->SetDefaultCellOverflow(false);
  m_programGrid->SetSelectionMode(wxGrid::wxGridSelectRows);
  m_programGrid->EnableEditing(false);
  m_programGrid->DisableDragRowSize();
  m_programGrid->SetColSize(0, FromDIP(100));
  m_programGrid->SetColSize(1, FromDIP(300));
  m_programGrid->SetColSize(2, FromDIP(150));
  m_programGrid->Bind(wxEVT_GRID_SELECT_CELL, &EPGPanel::OnProgramSelected,
                      this);
  m_programGrid->Bind(wxEVT_SIZE, &EPGPanel::OnProgramListResize, this);
  mainSizer->Add(m_programGrid, 1, wxEXPAND | wxALL, FromDIP(5));

  // ---- Блок деталей ----
  wxStaticBox *detailBox = new wxStaticBox(this, wxID_ANY, "Details");
  wxStaticBoxSizer *detailSizer = new wxStaticBoxSizer(detailBox, wxVERTICAL);

  m_detailTitle = new wxStaticText(detailBox, wxID_ANY, "");
  m_detailTitle->SetFont(m_detailTitle->GetFont().MakeBold());
  detailSizer->Add(m_detailTitle, 0, wxALL, FromDIP(5));

  m_detailDesc =
      new wxTextCtrl(detailBox, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
  m_detailDesc->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
  m_detailDesc->SetMinSize(wxSize(-1, FromDIP(80)));
  m_detailDesc->SetMaxSize(wxSize(-1, FromDIP(200)));
  detailSizer->Add(m_detailDesc, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                   FromDIP(5));

  mainSizer->Add(detailSizer, 0, wxEXPAND | wxALL, FromDIP(5));

  SetSizer(mainSizer);

  UpdateDateLabel();
}

// ----------------------------------------------------------------------------
// Методы управления каналом и загрузкой программ
// ----------------------------------------------------------------------------
void EPGPanel::SetChannel(const Channel &channel) {
  m_currentChannel = channel;
  m_currentChannelId = channel.getTvgId();
  m_currentChannelName = channel.getName();

  // Обновляем заголовок
  m_channelNameLabel->SetLabel(wxString::FromUTF8(m_currentChannelName));

  // Загружаем программы на текущую дату
  LoadProgramsForChannel(m_currentChannelId, m_currentDate);

  // Сохраняем состояние
  SaveState();
}

void EPGPanel::LoadProgramsForChannel(const std::string &channelId,
                                      time_t date) {
  // Очистка сетки
  m_programGrid->ClearGrid();
  if (m_programGrid->GetNumberRows() > 0)
    m_programGrid->DeleteRows(0, m_programGrid->GetNumberRows());

  m_detailTitle->SetLabel("");
  m_detailDesc->SetValue("");

  // Если есть ошибка — показать сообщение
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
    m_programGrid->AppendRows(1);
    m_programGrid->SetCellValue(0, 0, "");
    m_programGrid->SetCellValue(0, 1, "⚠ EPG manager not available");
    m_programGrid->SetCellValue(0, 2, "");
    AdjustProgramColumns();
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

  // Получаем программы
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
  UpdateDateLabel();

  int row = 0;
  for (const auto &prog : programs) {
    wxDateTime start(prog.startTime);
    wxDateTime stop(prog.stopTime);
    wxString timeRange = start.Format("%H:%M") + " - " + stop.Format("%H:%M");

    m_programGrid->AppendRows(1);
    m_programGrid->SetCellValue(row, 0, timeRange);
    m_programGrid->SetCellValue(row, 1, wxString::FromUTF8(prog.title));
    m_programGrid->SetCellValue(row, 2, wxString::FromUTF8(prog.category));
    ++row;
  }

  AdjustProgramColumns();
}

// ----------------------------------------------------------------------------
// Навигация по дням
// ----------------------------------------------------------------------------
void EPGPanel::OnPrevDay(wxCommandEvent &) {
  m_currentDate -= 24 * 3600;
  UpdateDateLabel();
  if (!m_currentChannelId.empty() || !m_currentChannelName.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

void EPGPanel::OnNextDay(wxCommandEvent &) {
  m_currentDate += 24 * 3600;
  UpdateDateLabel();
  if (!m_currentChannelId.empty() || !m_currentChannelName.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

void EPGPanel::OnToday(wxCommandEvent &) {
  m_currentDate = EpgTime::GetStartOfDay(wxDateTime::Now().GetTicks());
  UpdateDateLabel();
  if (!m_currentChannelId.empty() || !m_currentChannelName.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

// ----------------------------------------------------------------------------
// Обновление даты
// ----------------------------------------------------------------------------
void EPGPanel::UpdateDateLabel() {
  wxDateTime dt(m_currentDate);
  m_dateLabel->SetLabel(dt.Format("%d %b %Y"));
}

// ----------------------------------------------------------------------------
// Обработка выбора программы в сетке
// ----------------------------------------------------------------------------
void EPGPanel::OnProgramSelected(wxGridEvent &event) {
  int row = event.GetRow();
  if (row < 0 || row >= (int)m_currentPrograms.size())
    return;

  const EpgProgram &prog = m_currentPrograms[row];
  m_detailTitle->SetLabel(wxString::FromUTF8(prog.title));
  m_detailDesc->SetValue(wxString::FromUTF8(prog.description));
}

// ----------------------------------------------------------------------------
// Автоподбор ширины колонок
// ----------------------------------------------------------------------------
void EPGPanel::AdjustProgramColumns() {
  if (!m_programGrid || m_programGrid->GetNumberRows() == 0)
    return;

  int totalWidth;
  m_programGrid->GetClientSize(&totalWidth, nullptr);

  int maxTimeWidth = 0;
  wxClientDC dc(m_programGrid);
  dc.SetFont(m_programGrid->GetDefaultCellFont());
  for (int row = 0; row < m_programGrid->GetNumberRows(); ++row) {
    wxString text = m_programGrid->GetCellValue(row, 0);
    wxSize extent = dc.GetTextExtent(text);
    if (extent.GetWidth() > maxTimeWidth)
      maxTimeWidth = extent.GetWidth();
  }
  maxTimeWidth += FromDIP(20);
  if (maxTimeWidth < FromDIP(80))
    maxTimeWidth = FromDIP(80);

  int catWidth = FromDIP(150);
  int titleWidth = totalWidth - maxTimeWidth - catWidth;
  if (titleWidth < FromDIP(50))
    titleWidth = FromDIP(50);

  m_programGrid->SetColSize(0, maxTimeWidth);
  m_programGrid->SetColSize(1, titleWidth);
  m_programGrid->SetColSize(2, catWidth);
  m_programGrid->AutoSizeRows();
}

void EPGPanel::OnProgramListResize(wxSizeEvent &event) {
  AdjustProgramColumns();
  event.Skip();
}

// ----------------------------------------------------------------------------
// Вспомогательные методы для статуса
// ----------------------------------------------------------------------------
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

void EPGPanel::ShowMessage(const wxString &msg) {
  wxMessageBox(msg, "Info", wxOK | wxICON_INFORMATION, this);
}

// ----------------------------------------------------------------------------
// Сохранение/восстановление состояния
// ----------------------------------------------------------------------------
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
    m_currentDate = s_lastDate;
    SetChannel(ch);
  } else {
    // Сброс
    m_channelNameLabel->SetLabel("No channel selected");
    m_currentDate = EpgTime::GetStartOfDay(wxDateTime::Now().GetTicks());
    UpdateDateLabel();
    m_programGrid->ClearGrid();
    if (m_programGrid->GetNumberRows() > 0)
      m_programGrid->DeleteRows(0, m_programGrid->GetNumberRows());
    m_detailTitle->SetLabel("");
    m_detailDesc->SetValue("");
  }
}

// ----------------------------------------------------------------------------
// Обработка завершения обновления EPG (вызывается из MainFrame)
// ----------------------------------------------------------------------------
void EPGPanel::OnEpgUpdateFinished(int status, const wxString &error) {
  LOG_DEBUG("EPGPanel::OnEpgUpdateFinished: status=%d, error=%s", status,
            error.ToUTF8().data());

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
    SetStatus("", "");
    m_hasError = false;
    m_lastError.Clear();
  }

  // Перезагрузить программы для текущего канала
  if (!m_currentChannelId.empty()) {
    LoadProgramsForChannel(m_currentChannelId, m_currentDate);
  }
}

// ----------------------------------------------------------------------------
// Индикация прогресса матчинга (вызывается из MainFrame)
// ----------------------------------------------------------------------------
void EPGPanel::ShowMatchProgress(bool show) {
  // Здесь можно показать что-то, но в новой панели мы не показываем прогресс,
  // так как он управляется глобально. Оставляем заглушку.
  if (show) {
    SetStatus("Matching", "Matching channels...");
  } else {
    ClearStatus();
  }
}

void EPGPanel::UpdateMatchProgress(int matched, int total, int progress) {
  if (m_isActive) {
    SetStatus("Matching", wxString::Format("Matched %d/%d (%d%%)", matched,
                                           progress, progress * 100 / total));
  }
}
